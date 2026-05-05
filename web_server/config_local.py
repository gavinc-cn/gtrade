"""
本地开发环境配置
用于本地开发时连接Docker MySQL容器
"""
import os
from pathlib import Path
from dotenv import load_dotenv
from config import DevelopmentConfig

# 加载 .env 文件
env_path = Path(__file__).parent.parent / '.env'
if env_path.exists():
    load_dotenv(env_path)

class LocalDevelopmentConfig(DevelopmentConfig):
    """本地开发环境配置 - 连接Docker MySQL"""

    # 数据库配置 - 连接到Docker暴露的端口
    DB_HOST = os.environ.get('DB_HOST') or 'localhost'
    DB_PORT = int(os.environ.get('DB_PORT') or 3307)  # Docker映射到3307端口
    DB_USER = os.environ.get('DB_USER') or 'gtrade'
    DB_PASSWORD = os.environ.get('DB_PASSWORD') or ''
    DB_NAME = os.environ.get('DB_NAME') or 'gtrade'

    # CORS配置 - 允许本地前端访问
    CORS_ORIGINS = [
        'http://localhost:46010',
        'http://127.0.0.1:46010',
        'http://localhost:5173',  # Vite默认端口
        'http://127.0.0.1:5173'
    ]

# 导出配置
config_local = LocalDevelopmentConfig
