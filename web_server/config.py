import os
import yaml
from datetime import timedelta
from pathlib import Path

# 加载数据库配置
def load_db_config():
    """从 config.yml 中指定的 db_config 路径加载数据库配置

    与 gtrade C++ 保持一致：
    1. 先读取 config/config.yml 获取 db_config 路径
    2. 再从该路径读取实际的数据库配置
    """
    main_config_file = Path(__file__).parent.parent / 'config/config.yml'
    if not main_config_file.exists():
        return {}

    with open(main_config_file, 'r', encoding='utf-8') as f:
        main_config = yaml.safe_load(f)

    db_config_path = main_config.get('db_config')
    if not db_config_path:
        return {}

    # 从 db_config 路径读取数据库配置
    secret_file = Path(db_config_path)
    if secret_file.exists():
        with open(secret_file, 'r', encoding='utf-8') as f:
            config = yaml.safe_load(f)
            return config.get('mysql', {})
    return {}

# 加载 Web 用户配置
def load_web_config():
    """从 config/config.yml 加载 Web 用户配置"""
    config_file = Path(__file__).parent.parent / 'config/config.yml'
    if config_file.exists():
        with open(config_file, 'r', encoding='utf-8') as f:
            config = yaml.safe_load(f)
            return {
                'user': config.get('user', 'admin'),
                'password': config.get('password', 'admin')
            }
    return {'user': 'admin', 'password': 'admin'}

# SSE 推送通道默认参数（config/config.yml 的 web_push 段按 key 覆盖）
# 契约与实测依据见 doc_ai/plan/202610/20261003_1610_web行情推送改造方案.md §2.3/§2.8
WEB_PUSH_DEFAULTS = {
    'enabled': True,              # 总开关：false 时 /api/stream* 返回 503，前端回落轮询
    'hub_tick_ms': 50,            # 中枢调度精度（限频下限/保底的判定粒度）
    'depth_poll_ms': 250,         # depth 上游采样周期（引擎 HTTP，每次新建连接）
    'depth_min_push_ms': 500,     # depth 推送下限（用户指定）
    'depth_timeout_s': 2,         # 引擎请求超时（采样周期只有 250ms，不能用默认 10s）
    'db_poll_ms': 1000,           # strategies 上游采样周期（MySQL strat_info 查询）
    'strategies_min_push_ms': 1000,  # strategies 推送下限（增量攒批）
    'max_push_ms': 60000,         # 无变化时的保底推送上限（用户指定）
    'ping_interval_ms': 20000,    # mixed 通道的心跳（过渡期沿用）；下面是分通道值
    'ping_interval_quote_ms': 10000,   # quote 通道心跳（也是空闲释放的最坏延迟）
    'ping_interval_trade_ms': 5000,    # trade 通道心跳（背压语义更严，心跳更密）
    'max_topics_per_conn': 8,     # 单连接 topic 上限
    'queue_size': 8,              # 连接级事件队列容量（mixed 通道）
    'queue_size_quote': 2,        # quote 通道队列容量（快照只有最新一帧有用，保持极浅）
    'queue_size_trade': 8,        # trade 通道队列容量
    'retry_max_ms': 5000,         # 上游失败退避上限（基准周期 ×4 递增到此值）
    'ticket_ttl_s': 30,           # 一次性握手票据有效期
    'allow_url_token': True,      # 允许 ?token= 直连（仅本地调试；生产应设 false）
    'watchdog_ms': 90000,         # 前端看门狗：超过此时长无任何事件视为失联
    'degraded_after_ms': 10000,   # 前端失联多久后回落轮询
    'fallback_depth_poll_ms': 1000,      # 降级轮询间隔（盘口，沿用旧值）
    'fallback_strategies_poll_ms': 10000,  # 降级轮询间隔（策略表，沿用旧值）
    # 引擎侧推送入口（rev4 §5）：引擎作 WS 客户端连入，链路见 ws_engine.py
    'ws_enabled': True,           # 是否起 /ws/engine 监听
    'ws_port': 46013,             # 引擎入口端口（仅绑 127.0.0.1）
    'engine_secret': '',          # 引擎 hello 的共享密钥（空=仅本地回环放行，生产必须配置）
    'engine_ping_ms': 10000,      # 服务端→引擎的应用层心跳周期
    'engine_hello_timeout_s': 5,  # 等 hello 首帧的超时
    'engine_query_timeout_s': 5,  # 补查单次往返超时
    'upstream_engine_first': True,  # 实时数据优先用引擎推送，链路未就绪时回落轮询
}


def load_web_push_config():
    """从 config/config.yml 的 web_push 段加载推送参数（缺项用默认值补齐）。"""
    merged = dict(WEB_PUSH_DEFAULTS)
    config_file = Path(__file__).parent.parent / 'config/config.yml'
    if config_file.exists():
        with open(config_file, 'r', encoding='utf-8') as f:
            raw = (yaml.safe_load(f) or {}).get('web_push') or {}
        if isinstance(raw, dict):
            # 只接受已声明的 key，避免配置里写错名字被静默吞掉
            for key, value in raw.items():
                if key in merged and value is not None:
                    merged[key] = value
    # 环境变量覆盖：WEB_PUSH_<KEY 大写>（测试/多环境部署用；与 GTRADE_HTTP_GATEWAY 同风格）
    for key in merged:
        env_value = os.environ.get(f'WEB_PUSH_{key.upper()}')
        if env_value is None:
            continue
        if isinstance(merged[key], bool):
            merged[key] = env_value.strip().lower() in ('1', 'true', 'yes', 'on')
        elif isinstance(merged[key], int):
            try:
                merged[key] = int(env_value)
            except ValueError:
                pass
        else:
            merged[key] = env_value
    return merged


# 读取secret.db.yml配置
_db_config = load_db_config()
# 读取 Web 用户配置
_web_config = load_web_config()
# 读取 SSE 推送配置
_web_push_config = load_web_push_config()

class Config:
    """基础配置类"""
    SECRET_KEY = os.environ.get('SECRET_KEY') or 'gtrade-secret-key-2024'

    # 数据库配置 - 从 secret.db.yml 读取
    DB_HOST = os.environ.get('DB_HOST') or _db_config.get('host', 'localhost')
    DB_PORT = int(os.environ.get('DB_PORT') or _db_config.get('port', 3307))
    DB_USER = os.environ.get('DB_USER') or _db_config.get('user', 'root')
    DB_PASSWORD = os.environ.get('DB_PASSWORD') or _db_config.get('pwd', 'gtrade123')
    DB_NAME = os.environ.get('DB_NAME') or 'gtrade'
    DB_CHARSET = 'utf8mb4'

    # Web 用户配置 - 从 config/config.yml 读取
    WEB_USER = _web_config.get('user', 'admin')
    WEB_PASSWORD = _web_config.get('password', 'admin')

    # JWT 配置
    JWT_SECRET_KEY = SECRET_KEY
    JWT_EXPIRATION_HOURS = 24

    # API配置
    API_TITLE = 'GTRADE API'
    API_VERSION = 'v1.0'

    # CORS配置
    CORS_ORIGINS = [
        'http://localhost:46010',
        'http://127.0.0.1:46010',
        'http://localhost:3001',
        'http://127.0.0.1:3001',
        'http://localhost:8080',
        'http://127.0.0.1:8080'
    ]

    # 日志配置
    LOG_LEVEL = 'INFO'
    LOG_FORMAT = '%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    LOG_DIR = '/tmp/gtrade'
    LOG_FILE = 'web_server.log'

    # GTrade HTTP Gateway 配置
    GTRADE_HTTP_GATEWAY = os.environ.get('GTRADE_HTTP_GATEWAY') or 'http://127.0.0.1:46012'

    # SSE 推送通道配置（web_push 段，见 WEB_PUSH_DEFAULTS）
    WEB_PUSH = _web_push_config

    @property
    def database_config(self):
        """获取数据库配置字典"""
        return {
            'host': self.DB_HOST,
            'port': self.DB_PORT,
            'user': self.DB_USER,
            'password': self.DB_PASSWORD,
            'database': self.DB_NAME,
            'charset': self.DB_CHARSET
        }

class DevelopmentConfig(Config):
    """开发环境配置"""
    DEBUG = True
    LOG_LEVEL = 'DEBUG'

class ProductionConfig(Config):
    """生产环境配置"""
    DEBUG = False
    LOG_LEVEL = 'WARNING'

class TestingConfig(Config):
    """测试环境配置"""
    TESTING = True
    DB_NAME = 'gtrade_test'

# 配置字典
config = {
    'development': DevelopmentConfig,
    'production': ProductionConfig,
    'testing': TestingConfig,
    'default': DevelopmentConfig
}