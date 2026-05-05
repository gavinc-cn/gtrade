from flask import Flask, request, jsonify
from flask_cors import CORS
import logging
import os
import yaml
import atexit
import setproctitle
from config import config
from database import get_db_manager
from strategy_service import get_strategy_service
from reconciliation_service import get_reconciliation_service
from auth import token_required, authenticate, generate_token

setproctitle.setproctitle('gtrade_websrv')

# 配置日志
def setup_logging(app_config):
    # 创建日志目录
    os.makedirs(app_config.LOG_DIR, exist_ok=True)

    # 获取根日志记录器
    root_logger = logging.getLogger()
    root_logger.setLevel(getattr(logging, app_config.LOG_LEVEL))

    # 创建格式化器
    formatter = logging.Formatter(app_config.LOG_FORMAT)

    # 控制台处理器
    console_handler = logging.StreamHandler()
    console_handler.setLevel(getattr(logging, app_config.LOG_LEVEL))
    console_handler.setFormatter(formatter)

    # 文件处理器
    log_file_path = os.path.join(app_config.LOG_DIR, app_config.LOG_FILE)
    file_handler = logging.FileHandler(log_file_path, encoding='utf-8')
    file_handler.setLevel(getattr(logging, app_config.LOG_LEVEL))
    file_handler.setFormatter(formatter)

    # 添加处理器到根日志记录器
    root_logger.addHandler(console_handler)
    root_logger.addHandler(file_handler)

def create_app(config_name=None):
    """应用工厂函数"""
    if config_name is None:
        config_name = os.environ.get('FLASK_ENV', 'development')
    
    app = Flask(__name__)
    app_config = config[config_name]()
    app.config.from_object(app_config)
    
    # 设置日志
    setup_logging(app_config)
    
    # 配置CORS
    CORS(app, origins=app_config.CORS_ORIGINS)
    
    return app

app = create_app()
logger = logging.getLogger(__name__)

# 获取服务实例
db_manager = get_db_manager()
strategy_service = get_strategy_service()
reconciliation_service = get_reconciliation_service()

# 注册 autoresearch 审批 Blueprint
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    from autoresearch.web.routes import research_bp
    app.register_blueprint(research_bp)
    logger.info("autoresearch blueprint registered at /research")
except Exception as _e:
    logger.warning(f"autoresearch blueprint not loaded: {_e}")


def _get_strategy_param_dir():
    """获取策略参数配置目录路径。"""
    candidate_dirs = [
        os.environ.get('STRATEGY_PARAM_DIR'),
        os.path.join(os.path.dirname(os.path.dirname(__file__)), 'src/strategy_param'),
        'strategy_param',
    ]

    for directory in candidate_dirs:
        if directory and os.path.isdir(directory):
            return directory

    return None


def _load_template_param_config(template_name):
    """加载指定模板的 param/indicator 配置。"""
    param_dir = _get_strategy_param_dir()
    if not param_dir:
        return None

    candidates = [
        os.path.join(param_dir, f'{template_name}.yml'),
        os.path.join(param_dir, f'{template_name}.yaml'),
    ]

    for file_path in candidates:
        if not os.path.exists(file_path):
            continue
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f) or {}
            if isinstance(data, dict):
                return data
        except Exception as e:
            logger.error(f"读取策略参数配置失败 {file_path}: {e}")
            return None

    return None


def _normalize_field_items(items):
    """将字段配置标准化为前端需要的格式，并保持配置顺序。"""
    result = []
    if not isinstance(items, list):
        return result

    for item in items:
        if not isinstance(item, dict):
            continue
        field_id = item.get('id')
        if not field_id:
            continue
        result.append({
            'id': field_id,
            'name': item.get('name') or field_id
        })
    return result

# 启动持仓核算定时任务（每分钟执行一次）
reconciliation_service.start(interval_seconds=60)
logger.info("Reconciliation service started with 60s interval")

# 注册应用关闭时停止核算任务
def shutdown_reconciliation():
    logger.info("Shutting down reconciliation service")
    reconciliation_service.stop()

atexit.register(shutdown_reconciliation)

@app.route('/api/login', methods=['POST'])
def login():
    """用户登录"""
    try:
        data = request.get_json()

        if not data or not data.get('username') or not data.get('password'):
            return jsonify({'success': False, 'message': '用户名和密码不能为空'}), 400

        username = data.get('username')
        password = data.get('password')

        if authenticate(username, password):
            token = generate_token(username)
            return jsonify({'success': True, 'token': token, 'message': '登录成功'})
        else:
            return jsonify({'success': False, 'message': '用户名或密码错误'}), 401

    except Exception as e:
        logger.error(f"登录失败: {e}")
        return jsonify({'success': False, 'message': '登录失败'}), 500

@app.route('/api/strategies', methods=['GET'])
@token_required
def get_all_strategies():
    """获取所有策略"""
    try:
        strategies = strategy_service.get_all_strategies()
        return jsonify({'success': True, 'data': strategies})
    except Exception as e:
        logger.error(f"获取策略列表失败: {e}")
        return jsonify({'success': False, 'message': '获取策略列表失败'}), 500

@app.route('/api/strategies/<template>', methods=['GET'])
@token_required
def get_strategies_by_template(template):
    """根据模板获取策略"""
    try:
        strategies = strategy_service.get_strategies_by_template(template)
        return jsonify(strategies)
    except Exception as e:
        logger.error(f"获取策略列表失败: {e}")
        return jsonify({'error': '获取策略列表失败'}), 500

@app.route('/api/strategies', methods=['POST'])
@token_required
def create_strategy():
    """创建新策略"""
    try:
        data = request.get_json()
        
        if not data or not data.get('strat_name') or not data.get('strat_template'):
            return jsonify({'success': False, 'message': '策略名称和模板不能为空'}), 400
        
        strategy_id = strategy_service.create_strategy(data)
        return jsonify({'success': True, 'message': '策略创建成功', 'data': {'id': strategy_id}}), 201
    
    except ValueError as e:
        return jsonify({'success': False, 'message': str(e)}), 400
    except Exception as e:
        logger.error(f"创建策略失败: {e}")
        return jsonify({'success': False, 'message': '创建策略失败'}), 500

@app.route('/api/strategies/<int:strategy_id>', methods=['PUT'])
@token_required
def update_strategy(strategy_id):
    """更新策略"""
    try:
        data = request.get_json()
        
        if not data:
            return jsonify({'error': '请求数据不能为空'}), 400
        
        strategy_service.update_strategy(strategy_id, data)
        return jsonify({'message': '策略更新成功'})
    
    except ValueError as e:
        return jsonify({'error': str(e)}), 400
    except Exception as e:
        logger.error(f"更新策略失败: {e}")
        return jsonify({'error': '更新策略失败'}), 500

@app.route('/api/strategies/<int:strategy_id>', methods=['DELETE'])
@token_required
def delete_strategy(strategy_id):
    """删除策略"""
    try:
        strategy_service.delete_strategy(strategy_id)
        return jsonify({'message': '策略删除成功'})
    
    except ValueError as e:
        return jsonify({'error': str(e)}), 404
    except Exception as e:
        logger.error(f"删除策略失败: {e}")
        return jsonify({'error': '删除策略失败'}), 500

@app.route('/api/strategies/<int:strategy_id>', methods=['GET'])
@token_required
def get_strategy(strategy_id):
    """获取单个策略详情"""
    try:
        strategy = strategy_service.get_strategy_by_id(strategy_id)
        return jsonify(strategy)
    
    except ValueError as e:
        return jsonify({'error': str(e)}), 404
    except Exception as e:
        logger.error(f"获取策略详情失败: {e}")
        return jsonify({'error': '获取策略详情失败'}), 500

@app.route('/api/template/list', methods=['GET'])
@token_required
def get_template_list():
    """获取模板列表"""
    try:
        templates = strategy_service.get_strategy_templates()
        return jsonify({'success': True, 'templates': templates})
    except Exception as e:
        logger.error(f"获取模板列表失败: {e}")
        return jsonify({'success': False, 'message': '获取模板列表失败'}), 500

@app.route('/api/template/config/<template_name>', methods=['GET'])
@token_required
def get_template_config(template_name):
    """获取模板配置（indicator和param定义）"""
    try:
        config_data = _load_template_param_config(template_name)
        if config_data:
            indicator_config = _normalize_field_items(config_data.get('indicator', []))
            param_config = _normalize_field_items(config_data.get('param', []))
            return jsonify({'indicator': indicator_config, 'param': param_config})

        # 回退：从实际策略数据中动态提取
        strategies = strategy_service.get_strategies_by_template(template_name)

        if not strategies:
            return jsonify({'indicator': [], 'param': []})

        # 收集所有的indicator和param字段
        indicator_fields = set()
        param_fields = set()

        for strat in strategies:
            if strat.get('indicator'):
                indicator_fields.update(strat['indicator'].keys())
            if strat.get('param'):
                param_fields.update(strat['param'].keys())

        # 转换为前端需要的格式
        indicator_config = [{'id': field, 'name': field} for field in sorted(indicator_fields)]
        param_config = [{'id': field, 'name': field} for field in sorted(param_fields)]

        return jsonify({'indicator': indicator_config, 'param': param_config})
    except Exception as e:
        logger.error(f"获取模板配置失败: {e}")
        return jsonify({'indicator': [], 'param': []})

@app.route('/api/strategy/list/<template>', methods=['GET'])
@token_required
def get_strategy_list_by_template(template):
    """根据模板获取策略列表"""
    try:
        strategies = strategy_service.get_strategies_by_template(template)
        return jsonify({'success': True, 'strategies': strategies})
    except Exception as e:
        logger.error(f"获取策略列表失败: {e}")
        return jsonify({'success': False, 'message': '获取策略列表失败'}), 500

@app.route('/api/strategy/start/<strategy_id>', methods=['POST'])
@token_required
def start_strategy(strategy_id):
    """启动策略"""
    try:
        # TODO: 调用C++后端的HTTP接口来启动策略
        # 暂时只更新数据库状态
        strategy_service.update_strategy(int(strategy_id), {'status': 1})
        return jsonify({'success': True, 'message': '策略启动成功'})
    except Exception as e:
        logger.error(f"启动策略失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategy/stop/<strategy_id>', methods=['POST'])
@token_required
def stop_strategy(strategy_id):
    """停止策略"""
    try:
        # TODO: 调用C++后端的HTTP接口来停止策略
        # 暂时只更新数据库状态
        strategy_service.update_strategy(int(strategy_id), {'status': 0})
        return jsonify({'success': True, 'message': '策略停止成功'})
    except Exception as e:
        logger.error(f"停止策略失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategy/restart/<strategy_id>', methods=['POST'])
@token_required
def restart_strategy(strategy_id):
    """重启策略"""
    try:
        # TODO: 调用C++后端的HTTP接口来重启策略
        # 暂时先停止再启动
        strategy_service.update_strategy(int(strategy_id), {'status': 0})
        strategy_service.update_strategy(int(strategy_id), {'status': 1})
        return jsonify({'success': True, 'message': '策略重启成功'})
    except Exception as e:
        logger.error(f"重启策略失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategy/batch', methods=['POST'])
@token_required
def batch_strategy_operation():
    """批量操作策略"""
    try:
        data = request.get_json()
        operation = data.get('operation')
        strategy_ids = data.get('strategy_ids', [])

        if not operation or not strategy_ids:
            return jsonify({'success': False, 'error': '参数不完整'}), 400

        success_count = 0
        fail_count = 0

        status_map = {'start': 1, 'stop': 0, 'restart': 1}
        status = status_map.get(operation)

        for strategy_id in strategy_ids:
            try:
                strategy_service.update_strategy(int(strategy_id), {'status': status})
                success_count += 1
            except Exception as e:
                logger.error(f"批量操作策略失败 ID:{strategy_id}, {e}")
                fail_count += 1

        return jsonify({
            'success': True,
            'success_count': success_count,
            'fail_count': fail_count
        })
    except Exception as e:
        logger.error(f"批量操作失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/orders', methods=['GET'])
@token_required
def get_all_orders():
    """获取所有委托列表"""
    try:
        # 获取查询参数
        page = request.args.get('page', 1, type=int)
        page_size = request.args.get('page_size', 20, type=int)
        policy_no = request.args.get('policy_no', None, type=str)
        inst_id = request.args.get('inst_id', None, type=str)
        status = request.args.get('status', None, type=str)

        # 计算偏移量
        offset = (page - 1) * page_size

        # 获取委托列表和总数
        orders = strategy_service.get_all_orders(
            limit=page_size,
            offset=offset,
            policy_no=policy_no,
            inst_id=inst_id,
            status=status
        )
        total = strategy_service.get_orders_count(
            policy_no=policy_no,
            inst_id=inst_id,
            status=status
        )

        return jsonify({
            'success': True,
            'data': {
                'orders': orders,
                'total': total,
                'page': page,
                'page_size': page_size
            }
        })
    except Exception as e:
        logger.error(f"获取所有委托列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategy/<strategy_id>/orders', methods=['GET'])
@token_required
def get_strategy_orders(strategy_id):
    """获取策略的委托列表"""
    try:
        orders = strategy_service.get_strategy_orders(strategy_id)
        return jsonify({'success': True, 'orders': orders})
    except Exception as e:
        logger.error(f"获取委托列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/trades', methods=['GET'])
@token_required
def get_all_trades():
    """获取所有成交列表"""
    try:
        # 获取查询参数
        page = request.args.get('page', 1, type=int)
        page_size = request.args.get('page_size', 20, type=int)
        policy_no = request.args.get('policy_no', None, type=str)
        inst_id = request.args.get('inst_id', None, type=str)
        bs_side = request.args.get('bs_side', None, type=str)

        # 计算偏移量
        offset = (page - 1) * page_size

        # 获取成交列表和总数
        trades = strategy_service.get_all_trades(
            limit=page_size,
            offset=offset,
            policy_no=policy_no,
            inst_id=inst_id,
            bs_side=bs_side
        )
        total = strategy_service.get_trades_count(
            policy_no=policy_no,
            inst_id=inst_id,
            bs_side=bs_side
        )

        return jsonify({
            'success': True,
            'data': {
                'trades': trades,
                'total': total,
                'page': page,
                'page_size': page_size
            }
        })
    except Exception as e:
        logger.error(f"获取所有成交列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategy/<strategy_id>/trades', methods=['GET'])
@token_required
def get_strategy_trades(strategy_id):
    """获取策略的成交列表"""
    try:
        trades = strategy_service.get_strategy_trades(strategy_id)
        return jsonify({'success': True, 'trades': trades})
    except Exception as e:
        logger.error(f"获取成交列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategy/<strategy_id>/positions', methods=['GET'])
@token_required
def get_strategy_positions(strategy_id):
    """获取策略的持仓列表"""
    try:
        positions = strategy_service.get_strategy_positions(strategy_id)
        return jsonify({'success': True, 'positions': positions})
    except Exception as e:
        logger.error(f"获取持仓列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategy/<strategy_id>/logs', methods=['GET'])
@token_required
def get_strategy_logs(strategy_id):
    """获取策略的运行日志"""
    try:
        limit = request.args.get('limit', 200, type=int)
        logs = strategy_service.get_strategy_logs(strategy_id, limit)
        return jsonify({'success': True, 'logs': logs})
    except Exception as e:
        logger.error(f"获取策略日志失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/positions', methods=['GET'])
@token_required
def get_all_positions():
    """获取所有持仓列表"""
    try:
        # 获取查询参数
        page = request.args.get('page', 1, type=int)
        page_size = request.args.get('page_size', 20, type=int)
        account_id = request.args.get('account_id', None, type=str)
        instrument = request.args.get('instrument', None, type=str)

        # 计算偏移量
        offset = (page - 1) * page_size

        # 获取持仓列表和总数
        positions = strategy_service.get_all_positions(
            limit=page_size,
            offset=offset,
            account_id=account_id,
            instrument=instrument
        )
        total = strategy_service.get_positions_count(
            account_id=account_id,
            instrument=instrument
        )

        return jsonify({
            'success': True,
            'data': {
                'positions': positions,
                'total': total,
                'page': page,
                'page_size': page_size
            }
        })
    except Exception as e:
        logger.error(f"获取所有持仓列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/portfolio_positions', methods=['GET'])
@token_required
def get_all_portfolio_positions():
    """获取所有组合持仓列表"""
    try:
        # 获取查询参数
        page = request.args.get('page', 1, type=int)
        page_size = request.args.get('page_size', 20, type=int)
        account_id = request.args.get('account_id', None, type=str)
        portfolio = request.args.get('portfolio', None, type=str)
        instrument = request.args.get('instrument', None, type=str)

        # 计算偏移量
        offset = (page - 1) * page_size

        # 获取组合持仓列表和总数
        positions = strategy_service.get_all_portfolio_positions(
            limit=page_size,
            offset=offset,
            account_id=account_id,
            portfolio=portfolio,
            instrument=instrument
        )
        total = strategy_service.get_portfolio_positions_count(
            account_id=account_id,
            portfolio=portfolio,
            instrument=instrument
        )

        return jsonify({
            'success': True,
            'data': {
                'positions': positions,
                'total': total,
                'page': page,
                'page_size': page_size
            }
        })
    except Exception as e:
        logger.error(f"获取所有组合持仓列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/balances', methods=['GET'])
@token_required
def get_all_balances():
    """获取所有资金列表"""
    try:
        # 获取查询参数
        page = request.args.get('page', 1, type=int)
        page_size = request.args.get('page_size', 20, type=int)
        account_id = request.args.get('account_id', None, type=str)
        currency = request.args.get('currency', None, type=str)

        # 计算偏移量
        offset = (page - 1) * page_size

        # 获取资金列表和总数
        balances = strategy_service.get_all_balances(
            limit=page_size,
            offset=offset,
            account_id=account_id,
            currency=currency
        )
        total = strategy_service.get_balances_count(
            account_id=account_id,
            currency=currency
        )

        return jsonify({
            'success': True,
            'data': {
                'balances': balances,
                'total': total,
                'page': page,
                'page_size': page_size
            }
        })
    except Exception as e:
        logger.error(f"获取所有资金列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/dict', methods=['GET'])
@token_required
def get_dict():
    """获取数据字典配置"""
    try:
        # dict.yml文件路径，支持多种部署方式
        # 1. 优先使用环境变量指定的路径
        dict_file = os.environ.get('DICT_FILE_PATH')

        # 2. 尝试从web_server目录向上查找
        if not dict_file or not os.path.exists(dict_file):
            dict_file = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'src/common/dict.yml')

        # 3. 尝试从当前工作目录查找（Docker环境）
        if not os.path.exists(dict_file):
            dict_file = 'src/common/dict.yml'

        # 4. 尝试绝对路径（Docker环境）
        if not os.path.exists(dict_file):
            dict_file = '/opt/gtrade/src/common/dict.yml'

        if not os.path.exists(dict_file):
            raise FileNotFoundError(f"无法找到数据字典文件，尝试的路径包括相对路径和 /opt/gtrade/src/common/dict.yml")

        with open(dict_file, 'r', encoding='utf-8') as f:
            dict_data = yaml.safe_load(f)

        return jsonify({'success': True, 'data': dict_data})
    except FileNotFoundError as e:
        logger.error(f"数据字典文件不存在: {e}")
        return jsonify({'success': False, 'message': '数据字典文件不存在'}), 404
    except Exception as e:
        logger.error(f"读取数据字典失败: {e}")
        return jsonify({'success': False, 'message': '读取数据字典失败'}), 500

@app.route('/api/reconciliation/status', methods=['GET'])
@token_required
def get_reconciliation_status():
    """获取持仓核算状态"""
    try:
        status = reconciliation_service.get_reconciliation_status()
        return jsonify({'success': True, 'data': status})
    except Exception as e:
        logger.error(f"获取核算状态失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/reconciliation/inconsistent', methods=['GET'])
@token_required
def get_inconsistent_positions():
    """获取不一致的持仓列表"""
    try:
        inconsistent = reconciliation_service.get_inconsistent_positions()

        # 转换为前端友好的格式
        result = []
        for key, mismatch_types in inconsistent.items():
            market, account_id, instrument, pos_side = key
            result.append({
                'market': market,
                'account_id': account_id,
                'instrument': instrument,
                'pos_side': pos_side,
                'mismatch_types': list(mismatch_types)
            })

        return jsonify({'success': True, 'data': result})
    except Exception as e:
        logger.error(f"获取不一致持仓列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategy/config_files', methods=['GET'])
@token_required
def get_available_config_files():
    """获取所有可用的策略配置文件（剔除已添加的策略）"""
    try:
        available_configs = strategy_service.get_available_config_files()
        return jsonify({'success': True, 'data': available_configs})
    except Exception as e:
        logger.error(f"获取可用配置文件失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/strategies/batch_from_config', methods=['POST'])
@token_required
def create_strategies_from_configs():
    """从配置文件批量创建策略"""
    try:
        data = request.get_json()
        config_filenames = data.get('config_files', [])

        if not config_filenames:
            return jsonify({'success': False, 'message': '请选择至少一个配置文件'}), 400

        result = strategy_service.create_strategies_from_configs(config_filenames)
        return jsonify({'success': True, 'data': result})
    except Exception as e:
        logger.error(f"批量创建策略失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/health', methods=['GET'])
def health_check():
    """健康检查接口"""
    return jsonify({'status': 'ok', 'message': 'GTRADE API服务运行正常'})

if __name__ == '__main__':
    logger.info(f"GTrade Web Server starting with PID {os.getpid()}, process name: gtrade_websrv")

    # Docker环境需要绑定到0.0.0.0
    host = '0.0.0.0' if os.environ.get('FLASK_ENV') == 'development' else '127.0.0.1'
    app.run(host=host, port=46011, debug=True)
