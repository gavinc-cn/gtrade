"""Flask Blueprint — /research/* 路由，挂载到现有 web_server。

审批流程：用户访问 /research/approval → 查看详情 → 点击批准/拒绝
→ POST 更新 MySQL 状态（Prefect flow 轮询到状态变更后自动继续）。
"""

import json
from flask import Blueprint, jsonify, request, render_template_string
from autoresearch.db import catalog
from autoresearch.config import load_config, get as cfg

load_config()
catalog.init_db()

research_bp = Blueprint("research", __name__, url_prefix="/research")

_LIST_HTML = """<!DOCTYPE html><html><head><title>策略审批</title>
<style>body{font-family:sans-serif;max-width:900px;margin:40px auto}
table{width:100%;border-collapse:collapse}td,th{border:1px solid #ddd;padding:8px}</style>
</head><body><h2>待审批策略</h2>
<table><tr><th>ID</th><th>名称</th><th>来源</th><th>状态</th><th>操作</th></tr>
{% for p in pipelines %}<tr>
<td>{{p.pipeline_id[:8]}}</td><td>{{p.name}}</td>
<td>{{p.source[:40]}}</td><td>{{p.status}}</td>
<td><a href="/research/approval/{{p.pipeline_id}}">查看详情</a></td>
</tr>{% endfor %}</table></body></html>"""

_DETAIL_HTML = """<!DOCTYPE html><html><head><title>策略详情</title>
<style>body{font-family:sans-serif;max-width:900px;margin:40px auto}pre{background:#f4f4f4;padding:12px}
.btn-a{background:green;color:white;padding:8px 20px;border:none;cursor:pointer}
.btn-r{background:red;color:white;padding:8px 20px;border:none;cursor:pointer}
</style></head><body>
<h2>{{p.name}}</h2><p>来源: {{p.source}}</p><p>状态: <b>{{p.status}}</b></p>
<h3>规格</h3><pre>{{spec}}</pre>
<h3>最优参数</h3><pre>{{best_params}}</pre>
<h3>回测指标</h3><ul>
<li>Sharpe: {{metrics.get('sharpe')}}</li><li>MaxDD: {{metrics.get('max_dd')}}</li>
<li>Calmar: {{metrics.get('calmar')}}</li><li>WinRate: {{metrics.get('win_rate')}}</li>
</ul>
<form method="POST" action="/research/approval/{{p.pipeline_id}}/approve">
<input name="comment" placeholder="批注（可选）" style="width:300px">
<button class="btn-a">批准上线</button></form><br>
<form method="POST" action="/research/approval/{{p.pipeline_id}}/reject">
<input name="comment" placeholder="拒绝原因" style="width:300px" required>
<button class="btn-r">拒绝</button></form></body></html>"""


@research_bp.route("/approval")
def list_approvals():
    return render_template_string(_LIST_HTML,
        pipelines=catalog.list_pipelines(status="pending_approval"))


@research_bp.route("/approval/<pipeline_id>")
def approval_detail(pipeline_id):
    p = catalog.get_pipeline(pipeline_id)
    if not p:
        return jsonify({"error": "not found"}), 404
    from sqlalchemy import text
    from autoresearch.db.catalog import _get_engine
    with _get_engine().connect() as con:
        row = con.execute(text(
            "SELECT sharpe,max_dd,calmar,win_rate FROM ar_run "
            "WHERE pipeline_id=:pid AND is_oos=0 ORDER BY created_at DESC LIMIT 1"
        ), {"pid": pipeline_id}).mappings().fetchone()
        brow = con.execute(text(
            "SELECT params_json FROM ar_best_params WHERE pipeline_id=:pid"
        ), {"pid": pipeline_id}).mappings().fetchone()
    metrics = dict(row) if row else {}
    best_params = json.loads(brow["params_json"]) if brow else {}
    spec = json.loads(p.get("spec_json") or "{}")
    return render_template_string(_DETAIL_HTML, p=p,
        spec=json.dumps(spec, ensure_ascii=False, indent=2),
        best_params=json.dumps(best_params, ensure_ascii=False, indent=2),
        metrics=metrics)


@research_bp.route("/approval/<pipeline_id>/approve", methods=["POST"])
def approve(pipeline_id):
    p = catalog.get_pipeline(pipeline_id)
    if not p:
        return jsonify({"error": "not found"}), 404
    catalog.add_approval(pipeline_id, "approve", {}, request.form.get("comment", ""))
    catalog.update_status(pipeline_id, "approved")
    return jsonify({"status": "approved"})


@research_bp.route("/approval/<pipeline_id>/reject", methods=["POST"])
def reject(pipeline_id):
    p = catalog.get_pipeline(pipeline_id)
    if not p:
        return jsonify({"error": "not found"}), 404
    catalog.add_approval(pipeline_id, "reject", {}, request.form.get("comment", ""))
    catalog.update_status(pipeline_id, "rejected")
    return jsonify({"status": "rejected"})


@research_bp.route("/api/pipelines")
def api_list():
    return jsonify(catalog.list_pipelines(request.args.get("status")))


@research_bp.route("/api/dead-queue")
def api_dead():
    import redis
    r = cfg()["redis"]
    rdb = redis.Redis(host=r["host"], port=r["port"], db=r["db"], decode_responses=True)
    return jsonify([json.loads(i) for i in rdb.lrange("ar:backtest_dead", 0, 19)])
