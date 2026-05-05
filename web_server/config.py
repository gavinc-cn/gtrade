import os
import yaml
from datetime import timedelta
from pathlib import Path
from dotenv import load_dotenv

# 加载 .env 文件
env_path = Path(__file__).parent.parent / '.env'
if env_path.exists():
    load_dotenv(env_path)

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
                'user': config.get('user', ''),
                'password': config.get('password', '')
            }
    return {'user': '', 'password': ''}

# 读取secret.db.yml配置
_db_config = load_db_config()
# 读取 Web 用户配置
_web_config = load_web_config()

class Config:
    """基础配置类"""
    SECRET_KEY = os.environ.get('SECRET_KEY') or ''

    # 数据库配置 - 从 secret.db.yml 读取
    DB_HOST = os.environ.get('DB_HOST') or _db_config.get('host', 'localhost')
    DB_PORT = int(os.environ.get('DB_PORT') or _db_config.get('port', 3307))
    DB_USER = os.environ.get('DB_USER') or _db_config.get('user', 'root')
    DB_PASSWORD = os.environ.get('DB_PASSWORD') or _db_config.get('pwd', '')
    DB_NAME = os.environ.get('DB_NAME') or 'gtrade'
    DB_CHARSET = 'utf8mb4'

    # Web 用户配置 - 从 config/config.yml 读取
    WEB_USER = os.environ.get('WEB_USER') or _web_config.get('user', '')
    WEB_PASSWORD = os.environ.get('WEB_PASSWORD') or _web_config.get('password', '')

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