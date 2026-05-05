import pymysql
import logging
from contextlib import contextmanager
from config import config
import os

logger = logging.getLogger(__name__)

class DatabaseManager:
    """数据库管理器"""
    
    def __init__(self, config_name=None):
        if config_name is None:
            config_name = os.environ.get('FLASK_ENV', 'development')
        
        self.config = config[config_name]()
        self.db_config = self.config.database_config
    
    def get_connection(self):
        """获取数据库连接"""
        try:
            connection = pymysql.connect(**self.db_config)
            return connection
        except Exception as e:
            logger.error(f"数据库连接失败: {e}")
            raise
    
    @contextmanager
    def get_cursor(self, dictionary=True):
        """获取数据库游标的上下文管理器"""
        connection = None
        cursor = None
        try:
            connection = self.get_connection()
            cursor_class = pymysql.cursors.DictCursor if dictionary else pymysql.cursors.Cursor
            cursor = connection.cursor(cursor_class)
            yield cursor
            connection.commit()
        except Exception as e:
            if connection:
                connection.rollback()
            logger.error(f"数据库操作失败: {e}")
            raise
        finally:
            if cursor:
                cursor.close()
            if connection:
                connection.close()
    
    def execute_query(self, query, params=None, fetch_one=False, fetch_all=True):
        """执行查询语句"""
        with self.get_cursor() as cursor:
            cursor.execute(query, params)
            
            if fetch_one:
                return cursor.fetchone()
            elif fetch_all:
                return cursor.fetchall()
            else:
                return cursor.lastrowid
    
    def execute_many(self, query, params_list):
        """批量执行语句"""
        with self.get_cursor() as cursor:
            cursor.executemany(query, params_list)
            return cursor.rowcount
    
    def init_database(self):
        """初始化数据库表结构"""
        try:
            # 读取SQL文件并执行
            sql_file_path = os.path.join(os.path.dirname(__file__), '..', 'deploy', 'sql', 'strat_info.sql')
            
            if os.path.exists(sql_file_path):
                with open(sql_file_path, 'r', encoding='utf-8') as f:
                    sql_content = f.read()
                
                # 分割SQL语句（以分号分割）
                sql_statements = [stmt.strip() for stmt in sql_content.split(';') if stmt.strip()]
                
                with self.get_cursor() as cursor:
                    for statement in sql_statements:
                        if statement:
                            cursor.execute(statement)
                
                logger.info("数据库表结构初始化成功")
            else:
                logger.warning(f"SQL文件不存在: {sql_file_path}")
                
        except Exception as e:
            logger.error(f"数据库初始化失败: {e}")
            raise
    
    def test_connection(self):
        """测试数据库连接"""
        try:
            with self.get_cursor() as cursor:
                cursor.execute("SELECT 1")
                result = cursor.fetchone()
                return result is not None
        except Exception as e:
            logger.error(f"数据库连接测试失败: {e}")
            return False

# 全局数据库管理器实例
db_manager = DatabaseManager()

def get_db_manager():
    """获取数据库管理器实例"""
    return db_manager