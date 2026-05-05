"""策略目录 — 使用 SQLAlchemy 连接项目现有 MySQL（gtrade 库，ar_* 表）。"""

import json
import uuid
from datetime import datetime, timezone
from contextlib import contextmanager
from sqlalchemy import create_engine, text
from autoresearch.config import get as cfg

_engine = None


def _get_engine():
    global _engine
    if _engine is None:
        m = cfg()["mysql"]
        url = (f"mysql+pymysql://{m['user']}:{m['password']}"
               f"@{m['host']}:{m['port']}/{m['db']}")
        _engine = create_engine(url, pool_pre_ping=True, pool_size=5)
    return _engine


@contextmanager
def _conn():
    with _get_engine().connect() as con:
        with con.begin():
            yield con


def init_db() -> None:
    """建 ar_* 表（幂等），追加到现有 gtrade 库。"""
    stmts = [
        """CREATE TABLE IF NOT EXISTS ar_pipeline (
            pipeline_id VARCHAR(36) PRIMARY KEY,
            name VARCHAR(256), source TEXT, spec_json JSON, strategy_file TEXT,
            status ENUM('draft','validated','data_ready','backtesting','optimizing',
                        'pending_approval','approved','live','rejected','failed') DEFAULT 'draft',
            optimization_count INT DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            INDEX idx_status (status)
        )""",
        """CREATE TABLE IF NOT EXISTS ar_run (
            run_id VARCHAR(36) PRIMARY KEY,
            pipeline_id VARCHAR(36), params_json JSON,
            start_date VARCHAR(20), end_date VARCHAR(20),
            sharpe DECIMAL(8,4), max_dd DECIMAL(8,4), calmar DECIMAL(8,4),
            win_rate DECIMAL(6,4), profit_factor DECIMAL(8,4),
            is_oos TINYINT DEFAULT 0,
            status ENUM('running','success','failed') DEFAULT 'running',
            error_msg TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (pipeline_id) REFERENCES ar_pipeline(pipeline_id)
        )""",
        """CREATE TABLE IF NOT EXISTS ar_best_params (
            pipeline_id VARCHAR(36) PRIMARY KEY,
            params_json JSON, oos_sharpe DECIMAL(8,4), study_name VARCHAR(128),
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            FOREIGN KEY (pipeline_id) REFERENCES ar_pipeline(pipeline_id)
        )""",
        """CREATE TABLE IF NOT EXISTS ar_approval (
            approval_id BIGINT AUTO_INCREMENT PRIMARY KEY,
            pipeline_id VARCHAR(36),
            action ENUM('approve','reject','rework') NOT NULL,
            metrics_json JSON, comment TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (pipeline_id) REFERENCES ar_pipeline(pipeline_id)
        )""",
        """CREATE TABLE IF NOT EXISTS ar_deployment (
            deployment_id BIGINT AUTO_INCREMENT PRIMARY KEY,
            pipeline_id VARCHAR(36), strat_id VARCHAR(128), config_path TEXT,
            status ENUM('running','stopped','failed') DEFAULT 'running',
            deployed_at DATETIME DEFAULT CURRENT_TIMESTAMP, stopped_at DATETIME,
            FOREIGN KEY (pipeline_id) REFERENCES ar_pipeline(pipeline_id)
        )""",
    ]
    with _get_engine().connect() as con:
        for s in stmts:
            con.execute(text(s))
        con.commit()


def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


def add_pipeline(name: str, source: str, spec: dict, strategy_file: str) -> str:
    """新增一条 pipeline 记录，返回 pipeline_id。"""
    pid = str(uuid.uuid4())
    with _conn() as con:
        con.execute(text(
            "INSERT INTO ar_pipeline (pipeline_id,name,source,spec_json,strategy_file,created_at) "
            "VALUES (:pid,:name,:src,:spec,:sf,:now)"
        ), {"pid": pid, "name": name, "src": source,
            "spec": json.dumps(spec, ensure_ascii=False),
            "sf": strategy_file, "now": _now()})
    return pid


def update_status(pipeline_id: str, status: str) -> None:
    """更新 pipeline 状态。"""
    with _conn() as con:
        con.execute(text(
            "UPDATE ar_pipeline SET status=:s, updated_at=:now WHERE pipeline_id=:pid"
        ), {"s": status, "now": _now(), "pid": pipeline_id})


def add_run(pipeline_id: str, params: dict, start_date: str, end_date: str,
            metrics: dict, is_oos: bool = False,
            status: str = "success", error_msg: str = "") -> str:
    """记录一次回测运行结果，返回 run_id。"""
    rid = str(uuid.uuid4())
    with _conn() as con:
        con.execute(text(
            "INSERT INTO ar_run (run_id,pipeline_id,params_json,start_date,end_date,"
            "sharpe,max_dd,calmar,win_rate,profit_factor,is_oos,status,error_msg,created_at) "
            "VALUES (:rid,:pid,:p,:sd,:ed,:sh,:md,:ca,:wr,:pf,:oos,:st,:em,:now)"
        ), {"rid": rid, "pid": pipeline_id, "p": json.dumps(params),
            "sd": start_date, "ed": end_date,
            "sh": metrics.get("sharpe"), "md": metrics.get("max_dd"),
            "ca": metrics.get("calmar"), "wr": metrics.get("win_rate"),
            "pf": metrics.get("profit_factor"),
            "oos": int(is_oos), "st": status, "em": error_msg, "now": _now()})
    return rid


def save_best_params(pipeline_id: str, params: dict, oos_sharpe: float,
                     study_name: str) -> None:
    """保存最优参数（upsert）。"""
    with _conn() as con:
        con.execute(text(
            "INSERT INTO ar_best_params (pipeline_id,params_json,oos_sharpe,study_name) "
            "VALUES (:pid,:p,:s,:sn) "
            "ON DUPLICATE KEY UPDATE params_json=:p, oos_sharpe=:s, study_name=:sn"
        ), {"pid": pipeline_id, "p": json.dumps(params),
            "s": oos_sharpe, "sn": study_name})


def get_pipeline(pipeline_id: str) -> dict | None:
    """查询单条 pipeline 记录。"""
    with _get_engine().connect() as con:
        row = con.execute(
            text("SELECT * FROM ar_pipeline WHERE pipeline_id=:pid"),
            {"pid": pipeline_id}
        ).mappings().fetchone()
    return dict(row) if row else None


def list_pipelines(status: str | None = None) -> list[dict]:
    """列出所有 pipeline，可按状态过滤。"""
    with _get_engine().connect() as con:
        if status:
            rows = con.execute(
                text("SELECT * FROM ar_pipeline WHERE status=:s ORDER BY created_at DESC"),
                {"s": status}
            ).mappings().fetchall()
        else:
            rows = con.execute(
                text("SELECT * FROM ar_pipeline ORDER BY created_at DESC")
            ).mappings().fetchall()
    return [dict(r) for r in rows]


def add_approval(pipeline_id: str, action: str, metrics: dict, comment: str = "") -> None:
    """记录一次审批操作（approve/reject/rework）。"""
    with _conn() as con:
        con.execute(text(
            "INSERT INTO ar_approval (pipeline_id,action,metrics_json,comment,created_at) "
            "VALUES (:pid,:a,:m,:c,:now)"
        ), {"pid": pipeline_id, "a": action,
            "m": json.dumps(metrics), "c": comment, "now": _now()})


def add_deployment(pipeline_id: str, strat_id: str, config_path: str) -> None:
    """记录策略实盘部署。"""
    with _conn() as con:
        con.execute(text(
            "INSERT INTO ar_deployment (pipeline_id,strat_id,config_path,deployed_at) "
            "VALUES (:pid,:sid,:cp,:now)"
        ), {"pid": pipeline_id, "sid": strat_id, "cp": config_path, "now": _now()})
