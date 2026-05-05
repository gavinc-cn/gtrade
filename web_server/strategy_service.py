import json
from datetime import datetime
from database import get_db_manager
import logging
import os
import yaml
import requests
from config import config

logger = logging.getLogger(__name__)

class StrategyService:
    """策略服务类"""
    
    def __init__(self):
        self.db = get_db_manager()
    
    def get_all_strategies(self):
        """获取所有策略"""
        try:
            query = "SELECT * FROM strat_info ORDER BY create_time DESC"
            strategies = self.db.execute_query(query)
            
            # 解析JSON字段
            for strategy in strategies:
                strategy['param'] = self._parse_json_field(strategy.get('param'))
                strategy['indicator'] = self._parse_json_field(strategy.get('indicator'))
                
                # 格式化时间字段
                if strategy.get('create_time'):
                    strategy['create_time'] = strategy['create_time'].strftime('%Y-%m-%d %H:%M:%S')
                if strategy.get('update_time'):
                    strategy['update_time'] = strategy['update_time'].strftime('%Y-%m-%d %H:%M:%S')
            
            return strategies
        except Exception as e:
            logger.error(f"获取所有策略失败: {e}")
            raise
    
    def get_strategies_by_template(self, template):
        """根据模板获取策略"""
        try:
            query = "SELECT * FROM strat_info WHERE strat_template = %s ORDER BY create_time DESC"
            strategies = self.db.execute_query(query, (template,))
            
            # 解析JSON字段
            for strategy in strategies:
                strategy['param'] = self._parse_json_field(strategy.get('param'))
                strategy['indicator'] = self._parse_json_field(strategy.get('indicator'))
                
                # 格式化时间字段
                if strategy.get('create_time'):
                    strategy['create_time'] = strategy['create_time'].strftime('%Y-%m-%d %H:%M:%S')
                if strategy.get('update_time'):
                    strategy['update_time'] = strategy['update_time'].strftime('%Y-%m-%d %H:%M:%S')
            
            return strategies
        except Exception as e:
            logger.error(f"根据模板获取策略失败: {e}")
            raise
    
    def get_strategy_by_id(self, strategy_id):
        """根据ID获取策略"""
        try:
            query = "SELECT * FROM strat_info WHERE id = %s"
            strategy = self.db.execute_query(query, (strategy_id,), fetch_one=True, fetch_all=False)
            
            if not strategy:
                return None
            
            # 解析JSON字段
            strategy['param'] = self._parse_json_field(strategy.get('param'))
            strategy['indicator'] = self._parse_json_field(strategy.get('indicator'))
            
            # 格式化时间字段
            if strategy.get('create_time'):
                strategy['create_time'] = strategy['create_time'].strftime('%Y-%m-%d %H:%M:%S')
            if strategy.get('update_time'):
                strategy['update_time'] = strategy['update_time'].strftime('%Y-%m-%d %H:%M:%S')
            
            return strategy
        except Exception as e:
            logger.error(f"根据ID获取策略失败: {e}")
            raise
    
    def create_strategy(self, strategy_data):
        """创建新策略"""
        try:
            # 验证必填字段
            if not strategy_data.get('strat_name'):
                raise ValueError('策略名称不能为空')
            if not strategy_data.get('strat_template'):
                raise ValueError('策略模板不能为空')
            
            # 检查策略名称是否已存在
            if self._strategy_name_exists(strategy_data['strat_name']):
                raise ValueError('策略名称已存在')
            
            # 准备插入数据
            now = datetime.now()
            insert_query = """
                INSERT INTO strat_info (strat_name, strat_template, param, indicator, status, create_time, update_time)
                VALUES (%s, %s, %s, %s, %s, %s, %s)
            """
            
            param_json = json.dumps(strategy_data.get('param', {}), ensure_ascii=False)
            indicator_json = json.dumps(strategy_data.get('indicator', {}), ensure_ascii=False)
            
            strategy_id = self.db.execute_query(
                insert_query,
                (
                    strategy_data['strat_name'],
                    strategy_data['strat_template'],
                    param_json,
                    indicator_json,
                    strategy_data.get('status', 0),
                    now,
                    now
                ),
                fetch_one=False,
                fetch_all=False
            )
            
            logger.info(f"策略创建成功，ID: {strategy_id}")
            return strategy_id
        except Exception as e:
            logger.error(f"创建策略失败: {e}")
            raise
    
    def update_strategy(self, strategy_id, strategy_data):
        """更新策略"""
        try:
            # 检查策略是否存在
            existing_strategy = self.get_strategy_by_id(strategy_id)
            if not existing_strategy:
                raise ValueError('策略不存在')
            
            # 构建更新字段
            update_fields = []
            params = []
            
            if 'strat_name' in strategy_data:
                # 检查新名称是否与其他策略冲突
                if (strategy_data['strat_name'] != existing_strategy['strat_name'] and 
                    self._strategy_name_exists(strategy_data['strat_name'], exclude_id=strategy_id)):
                    raise ValueError('策略名称已存在')
                update_fields.append("strat_name = %s")
                params.append(strategy_data['strat_name'])
            
            if 'strat_template' in strategy_data:
                update_fields.append("strat_template = %s")
                params.append(strategy_data['strat_template'])
            
            if 'param' in strategy_data:
                update_fields.append("param = %s")
                params.append(json.dumps(strategy_data['param'], ensure_ascii=False))
            
            if 'indicator' in strategy_data:
                update_fields.append("indicator = %s")
                params.append(json.dumps(strategy_data['indicator'], ensure_ascii=False))
            
            if 'status' in strategy_data:
                update_fields.append("status = %s")
                params.append(strategy_data['status'])
            
            if not update_fields:
                raise ValueError('没有需要更新的字段')
            
            # 添加更新时间
            update_fields.append("update_time = %s")
            params.append(datetime.now())
            params.append(strategy_id)
            
            update_query = f"UPDATE strat_info SET {', '.join(update_fields)} WHERE id = %s"
            
            self.db.execute_query(update_query, params, fetch_one=False, fetch_all=False)
            
            logger.info(f"策略更新成功，ID: {strategy_id}")
            return True
        except Exception as e:
            logger.error(f"更新策略失败: {e}")
            raise
    
    def delete_strategy(self, strategy_id):
        """删除策略"""
        try:
            # 检查策略是否存在
            if not self.get_strategy_by_id(strategy_id):
                raise ValueError('策略不存在')
            
            delete_query = "DELETE FROM strat_info WHERE id = %s"
            self.db.execute_query(delete_query, (strategy_id,), fetch_one=False, fetch_all=False)
            
            logger.info(f"策略删除成功，ID: {strategy_id}")
            return True
        except Exception as e:
            logger.error(f"删除策略失败: {e}")
            raise
    
    def get_strategy_templates(self):
        """获取所有策略模板"""
        try:
            query = "SELECT DISTINCT strat_template FROM strat_info ORDER BY strat_template"
            templates = self.db.execute_query(query)
            return [template['strat_template'] for template in templates]
        except Exception as e:
            logger.error(f"获取策略模板失败: {e}")
            raise
    
    def get_strategy_statistics(self):
        """获取策略统计信息"""
        try:
            query = """
                SELECT 
                    strat_template,
                    status,
                    COUNT(*) as count
                FROM strat_info 
                GROUP BY strat_template, status
                ORDER BY strat_template, status
            """
            stats = self.db.execute_query(query)
            
            # 整理统计数据
            result = {}
            for stat in stats:
                template = stat['strat_template']
                if template not in result:
                    result[template] = {'total': 0, 'running': 0, 'paused': 0, 'stopped': 0}
                
                count = stat['count']
                result[template]['total'] += count
                
                if stat['status'] == 1:
                    result[template]['running'] = count
                elif stat['status'] == 2:
                    result[template]['paused'] = count
                else:
                    result[template]['stopped'] = count
            
            return result
        except Exception as e:
            logger.error(f"获取策略统计信息失败: {e}")
            raise
    
    def _parse_json_field(self, json_str):
        """解析JSON字段"""
        if not json_str:
            return {}
        try:
            return json.loads(json_str)
        except json.JSONDecodeError:
            return {}
    
    def _strategy_name_exists(self, name, exclude_id=None):
        """检查策略名称是否已存在"""
        if exclude_id:
            query = "SELECT id FROM strat_info WHERE strat_name = %s AND id != %s"
            params = (name, exclude_id)
        else:
            query = "SELECT id FROM strat_info WHERE strat_name = %s"
            params = (name,)

        result = self.db.execute_query(query, params, fetch_one=True, fetch_all=False)
        return result is not None

    def get_all_orders(self, limit=100, offset=0, policy_no=None, inst_id=None, status=None):
        """获取所有委托列表，支持分页和过滤"""
        try:
            # 构建查询条件
            conditions = []
            params = []

            if policy_no:
                conditions.append("policy_no = %s")
                params.append(policy_no)

            if inst_id:
                conditions.append("inst_id LIKE %s")
                params.append(f"%{inst_id}%")

            if status:
                conditions.append("status = %s")
                params.append(status)

            # 构建WHERE子句
            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            # 查询委托表
            query = f"""
                SELECT * FROM `order`
                {where_clause}
                ORDER BY ent_time DESC
                LIMIT %s OFFSET %s
            """
            params.extend([limit, offset])

            orders = self.db.execute_query(query, tuple(params))

            # 将大整数字段转换为字符串，避免JavaScript精度丢失
            for order in orders:
                if order.get('entno') is not None:
                    order['entno'] = str(order['entno'])
                if order.get('ex_entno') is not None:
                    order['ex_entno'] = str(order['ex_entno'])
                if order.get('ent_time') is not None:
                    order['ent_time'] = str(order['ent_time'])
                if order.get('expire_time') is not None:
                    order['expire_time'] = str(order['expire_time'])
                if order.get('confirm_time') is not None:
                    order['confirm_time'] = str(order['confirm_time'])
                if order.get('filled_time') is not None:
                    order['filled_time'] = str(order['filled_time'])
                if order.get('update_time') is not None:
                    order['update_time'] = str(order['update_time'])
                if order.get('withdraw_time') is not None:
                    order['withdraw_time'] = str(order['withdraw_time'])
                if order.get('drawno') is not None:
                    order['drawno'] = str(order['drawno'])
                if order.get('fmt_time') is not None:
                    order['fmt_time'] = str(order['fmt_time'])
                if order.get('status_gid') is not None:
                    order['status_gid'] = str(order['status_gid'])

            return orders
        except Exception as e:
            logger.error(f"获取所有委托列表失败: {e}")
            return []

    def get_orders_count(self, policy_no=None, inst_id=None, status=None):
        """获取委托总数，用于分页"""
        try:
            # 构建查询条件
            conditions = []
            params = []

            if policy_no:
                conditions.append("policy_no = %s")
                params.append(policy_no)

            if inst_id:
                conditions.append("inst_id LIKE %s")
                params.append(f"%{inst_id}%")

            if status:
                conditions.append("status = %s")
                params.append(status)

            # 构建WHERE子句
            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            # 查询总数
            query = f"""
                SELECT COUNT(*) as total FROM `order`
                {where_clause}
            """

            result = self.db.execute_query(query, tuple(params), fetch_one=True, fetch_all=False)
            return result.get('total', 0) if result else 0
        except Exception as e:
            logger.error(f"获取委托总数失败: {e}")
            return 0

    def get_strategy_orders(self, strategy_id):
        """获取策略的委托列表"""
        try:
            # 首先获取策略名称
            strategy = self.get_strategy_by_id(strategy_id)
            if not strategy:
                # 如果按ID找不到，尝试按名称查找
                policy_no = strategy_id
            else:
                policy_no = strategy.get('strat_name', strategy_id)

            # 查询委托表，按policy_no过滤
            query = """
                SELECT * FROM `order`
                WHERE policy_no = %s
                ORDER BY ent_time DESC
                LIMIT 100
            """
            orders = self.db.execute_query(query, (policy_no,))

            # 将大整数字段转换为字符串，避免JavaScript精度丢失
            for order in orders:
                if order.get('entno') is not None:
                    order['entno'] = str(order['entno'])
                if order.get('ex_entno') is not None:
                    order['ex_entno'] = str(order['ex_entno'])
                if order.get('ent_time') is not None:
                    order['ent_time'] = str(order['ent_time'])
                if order.get('expire_time') is not None:
                    order['expire_time'] = str(order['expire_time'])
                if order.get('confirm_time') is not None:
                    order['confirm_time'] = str(order['confirm_time'])
                if order.get('filled_time') is not None:
                    order['filled_time'] = str(order['filled_time'])
                if order.get('update_time') is not None:
                    order['update_time'] = str(order['update_time'])
                if order.get('withdraw_time') is not None:
                    order['withdraw_time'] = str(order['withdraw_time'])
                if order.get('drawno') is not None:
                    order['drawno'] = str(order['drawno'])
                if order.get('fmt_time') is not None:
                    order['fmt_time'] = str(order['fmt_time'])
                if order.get('status_gid') is not None:
                    order['status_gid'] = str(order['status_gid'])

            return orders
        except Exception as e:
            logger.error(f"获取策略委托列表失败: {e}")
            return []

    def get_all_trades(self, limit=100, offset=0, policy_no=None, inst_id=None, bs_side=None):
        """获取所有成交列表，支持分页和过滤"""
        try:
            # 构建查询条件
            conditions = []
            params = []

            if policy_no:
                conditions.append("strat_id = %s")
                params.append(policy_no)

            if inst_id:
                conditions.append("instrument LIKE %s")
                params.append(f"%{inst_id}%")

            if bs_side:
                conditions.append("td_side = %s")
                params.append(bs_side)

            # 构建WHERE子句
            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            # 查询成交表
            query = f"""
                SELECT * FROM trade
                {where_clause}
                ORDER BY filled_time DESC
                LIMIT %s OFFSET %s
            """
            params.extend([limit, offset])

            trades = self.db.execute_query(query, tuple(params))

            # 将大整数字段转换为字符串，避免JavaScript精度丢失
            for trade in trades:
                if trade.get('tdno') is not None:
                    trade['tdno'] = str(trade['tdno'])
                if trade.get('ordno') is not None:
                    trade['ordno'] = str(trade['ordno'])
                if trade.get('filled_time') is not None:
                    trade['filled_time'] = str(trade['filled_time'])

            return trades
        except Exception as e:
            logger.error(f"获取所有成交列表失败: {e}")
            return []

    def get_trades_count(self, policy_no=None, inst_id=None, bs_side=None):
        """获取成交总数，用于分页"""
        try:
            # 构建查询条件
            conditions = []
            params = []

            if policy_no:
                conditions.append("strat_id = %s")
                params.append(policy_no)

            if inst_id:
                conditions.append("instrument LIKE %s")
                params.append(f"%{inst_id}%")

            if bs_side:
                conditions.append("td_side = %s")
                params.append(bs_side)

            # 构建WHERE子句
            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            # 查询总数
            query = f"""
                SELECT COUNT(*) as total FROM trade
                {where_clause}
            """

            result = self.db.execute_query(query, tuple(params), fetch_one=True, fetch_all=False)
            return result.get('total', 0) if result else 0
        except Exception as e:
            logger.error(f"获取成交总数失败: {e}")
            return 0

    def get_strategy_trades(self, strategy_id):
        """获取策略的成交列表"""
        try:
            # 首先获取策略名称
            strategy = self.get_strategy_by_id(strategy_id)
            if not strategy:
                # 如果按ID找不到，尝试按名称查找
                policy_no = strategy_id
            else:
                policy_no = strategy.get('strat_name', strategy_id)

            # 查询成交表，按strat_id过滤
            query = """
                SELECT * FROM trade
                WHERE strat_id = %s
                ORDER BY filled_time DESC
                LIMIT 100
            """
            trades = self.db.execute_query(query, (policy_no,))

            # 将大整数字段转换为字符串，避免JavaScript精度丢失
            for trade in trades:
                if trade.get('tdno') is not None:
                    trade['tdno'] = str(trade['tdno'])
                if trade.get('ordno') is not None:
                    trade['ordno'] = str(trade['ordno'])
                if trade.get('filled_time') is not None:
                    trade['filled_time'] = str(trade['filled_time'])

            return trades
        except Exception as e:
            logger.error(f"获取策略成交列表失败: {e}")
            return []

    def get_strategy_positions(self, strategy_id):
        """获取策略的持仓列表"""
        try:
            # 首先获取策略名称
            strategy = self.get_strategy_by_id(strategy_id)
            if not strategy:
                # 如果按ID找不到，尝试按名称查找
                policy_no = strategy_id
            else:
                policy_no = strategy.get('strat_name', strategy_id)

            # 注意: 当前数据库中没有持仓表，这里返回空列表
            # 如果后续添加了持仓表，可以在这里查询
            # 暂时从成交表中聚合计算持仓(简化版本)
            query = """
                SELECT
                    instrument,
                    pos_side,
                    SUM(CASE WHEN td_side = 'B' THEN td_qty ELSE -td_qty END) as position,
                    AVG(td_px) as avg_price
                FROM trade
                WHERE strat_id = %s
                GROUP BY instrument, pos_side
                HAVING position != 0
            """
            positions = self.db.execute_query(query, (policy_no,))

            # 添加可用和未实现盈亏字段(实际应该从持仓表获取)
            for pos in positions:
                pos['available'] = pos.get('position', 0)  # 简化处理
                pos['unrealized_pnl'] = 0  # 需要实时价格才能计算

            return positions
        except Exception as e:
            logger.error(f"获取策略持仓列表失败: {e}")
            return []

    def get_strategy_logs(self, strategy_id, limit=200):
        """获取策略运行日志，按 log_time DESC，默认最近200条"""
        try:
            strategy = self.get_strategy_by_id(strategy_id)
            if not strategy:
                strat_id = strategy_id
            else:
                strat_id = strategy.get('strat_name', strategy_id)

            query = """
                SELECT strat_id, log_level, log_time, seq, content
                FROM strategy_log
                WHERE strat_id = %s
                ORDER BY log_time DESC, seq DESC
                LIMIT %s
            """
            logs = self.db.execute_query(query, (strat_id, int(limit)))

            for log in logs:
                if log.get('log_time') is not None:
                    log['log_time'] = str(log['log_time'])

            return logs
        except Exception as e:
            logger.error(f"获取策略日志失败: {e}")
            return []

    def get_all_positions(self, limit=100, offset=0, account_id=None, instrument=None):
        """获取所有持仓列表，支持分页和过滤"""
        try:
            # 构建查询条件
            conditions = []
            params = []

            if account_id:
                conditions.append("account_id = %s")
                params.append(account_id)

            if instrument:
                conditions.append("instrument LIKE %s")
                params.append(f"%{instrument}%")

            # 构建WHERE子句
            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            # 查询持仓表
            query = f"""
                SELECT * FROM position
                {where_clause}
                ORDER BY local_time DESC
                LIMIT %s OFFSET %s
            """
            params.extend([limit, offset])

            positions = self.db.execute_query(query, tuple(params))

            # 将大整数字段转换为字符串，避免JavaScript精度丢失
            for pos in positions:
                if pos.get('ex_time') is not None:
                    pos['ex_time'] = str(pos['ex_time'])
                if pos.get('local_time') is not None:
                    pos['local_time'] = str(pos['local_time'])

            return positions
        except Exception as e:
            logger.error(f"获取所有持仓列表失败: {e}")
            return []

    def get_positions_count(self, account_id=None, instrument=None):
        """获取持仓总数，用于分页"""
        try:
            # 构建查询条件
            conditions = []
            params = []

            if account_id:
                conditions.append("account_id = %s")
                params.append(account_id)

            if instrument:
                conditions.append("instrument LIKE %s")
                params.append(f"%{instrument}%")

            # 构建WHERE子句
            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            # 查询总数
            query = f"""
                SELECT COUNT(*) as total FROM position
                {where_clause}
            """

            result = self.db.execute_query(query, tuple(params), fetch_one=True, fetch_all=False)
            return result.get('total', 0) if result else 0
        except Exception as e:
            logger.error(f"获取持仓总数失败: {e}")
            return 0

    def get_all_portfolio_positions(self, limit=100, offset=0, account_id=None, portfolio=None, instrument=None):
        """获取所有组合持仓列表，支持分页和过滤"""
        try:
            # 构建查询条件
            conditions = []
            params = []

            if account_id:
                conditions.append("account_id = %s")
                params.append(account_id)

            if portfolio:
                conditions.append("portfolio = %s")
                params.append(portfolio)

            if instrument:
                conditions.append("instrument LIKE %s")
                params.append(f"%{instrument}%")

            # 构建WHERE子句
            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            # 查询组合持仓表
            query = f"""
                SELECT * FROM portfolio_position
                {where_clause}
                ORDER BY local_time DESC
                LIMIT %s OFFSET %s
            """
            params.extend([limit, offset])

            positions = self.db.execute_query(query, tuple(params))

            # 将大整数字段转换为字符串，避免JavaScript精度丢失
            for pos in positions:
                if pos.get('ex_time') is not None:
                    pos['ex_time'] = str(pos['ex_time'])
                if pos.get('local_time') is not None:
                    pos['local_time'] = str(pos['local_time'])

            return positions
        except Exception as e:
            logger.error(f"获取所有组合持仓列表失败: {e}")
            return []

    def get_portfolio_positions_count(self, account_id=None, portfolio=None, instrument=None):
        """获取组合持仓总数，用于分页"""
        try:
            # 构建查询条件
            conditions = []
            params = []

            if account_id:
                conditions.append("account_id = %s")
                params.append(account_id)

            if portfolio:
                conditions.append("portfolio = %s")
                params.append(portfolio)

            if instrument:
                conditions.append("instrument LIKE %s")
                params.append(f"%{instrument}%")

            # 构建WHERE子句
            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            # 查询总数
            query = f"""
                SELECT COUNT(*) as total FROM portfolio_position
                {where_clause}
            """

            result = self.db.execute_query(query, tuple(params), fetch_one=True, fetch_all=False)
            return result.get('total', 0) if result else 0
        except Exception as e:
            logger.error(f"获取组合持仓总数失败: {e}")
            return 0

    def get_all_balances(self, limit=100, offset=0, account_id=None, currency=None):
        """获取所有资金列表，支持分页和过滤"""
        try:
            conditions = []
            params = []

            if account_id:
                conditions.append("account_id = %s")
                params.append(account_id)

            if currency:
                conditions.append("currency LIKE %s")
                params.append(f"%{currency}%")

            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            query = f"""
                SELECT * FROM balance
                {where_clause}
                ORDER BY account_id ASC, currency ASC
                LIMIT %s OFFSET %s
            """
            params.extend([limit, offset])

            balances = self.db.execute_query(query, tuple(params))

            # 将大整数字段转换为字符串，避免JavaScript精度丢失
            for bal in balances:
                if bal.get('ex_time') is not None:
                    bal['ex_time'] = str(bal['ex_time'])
                if bal.get('local_time') is not None:
                    bal['local_time'] = str(bal['local_time'])

            return balances
        except Exception as e:
            logger.error(f"获取所有资金列表失败: {e}")
            return []

    def get_balances_count(self, account_id=None, currency=None):
        """获取资金总数，用于分页"""
        try:
            conditions = []
            params = []

            if account_id:
                conditions.append("account_id = %s")
                params.append(account_id)

            if currency:
                conditions.append("currency LIKE %s")
                params.append(f"%{currency}%")

            where_clause = ""
            if conditions:
                where_clause = "WHERE " + " AND ".join(conditions)

            query = f"""
                SELECT COUNT(*) as total FROM balance
                {where_clause}
            """

            result = self.db.execute_query(query, tuple(params), fetch_one=True, fetch_all=False)
            return result.get('total', 0) if result else 0
        except Exception as e:
            logger.error(f"获取资金总数失败: {e}")
            return 0

    def get_available_config_files(self):
        """获取所有可用的策略配置文件（剔除已添加的策略）"""
        try:
            # 策略配置文件路径
            config_dir = os.environ.get('STRATEGY_CONFIG_DIR', '/opt/win/gtrade/strategy_config')

            # 如果路径不存在，尝试其他可能的路径
            if not os.path.exists(config_dir):
                config_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'strategy_config')

            if not os.path.exists(config_dir):
                logger.error(f"策略配置目录不存在: {config_dir}")
                return []

            # 获取所有 yml 配置文件
            all_config_files = []
            for file in os.listdir(config_dir):
                if file.endswith('.yml') or file.endswith('.yaml'):
                    file_path = os.path.join(config_dir, file)
                    all_config_files.append({
                        'filename': file,
                        'filepath': file_path
                    })

            # 获取已添加的策略名称（从配置文件名推断）
            existing_strategies = self.get_all_strategies()
            existing_names = set()
            for strat in existing_strategies:
                # 策略名称通常来自配置文件名（去掉 .yml 后缀）
                strat_name = strat.get('strat_name', '')
                existing_names.add(strat_name)

            # 筛选出未添加的配置文件
            available_configs = []
            for config in all_config_files:
                # 从文件名推断策略名称（去掉后缀）
                base_name = os.path.splitext(config['filename'])[0]

                # 读取配置文件获取详细信息
                try:
                    with open(config['filepath'], 'r', encoding='utf-8') as f:
                        config_data = yaml.safe_load(f)

                    # 获取策略模板ID
                    template_id = config_data.get('strat_template_id', 'Unknown')

                    # 如果策略名称不在已添加列表中，则添加到可用列表
                    if base_name not in existing_names:
                        available_configs.append({
                            'filename': config['filename'],
                            'name': base_name,
                            'template': template_id,
                            'filepath': config['filepath']
                        })
                except Exception as e:
                    logger.error(f"读取配置文件失败 {config['filename']}: {e}")
                    continue

            return available_configs
        except Exception as e:
            logger.error(f"获取可用配置文件失败: {e}")
            return []

    def create_strategies_from_configs(self, config_filenames):
        """从配置文件批量创建策略 - 通过 gtrade HTTP Gateway"""
        try:
            config_dir = os.environ.get('STRATEGY_CONFIG_DIR', '/opt/win/gtrade/strategy_config')

            if not os.path.exists(config_dir):
                config_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'strategy_config')

            if not os.path.exists(config_dir):
                raise ValueError(f"策略配置目录不存在: {config_dir}")

            # 获取 gtrade HTTP Gateway 地址
            env = os.environ.get('FLASK_ENV', 'development')
            gtrade_gateway = config[env]().GTRADE_HTTP_GATEWAY

            created_strategies = []
            failed_strategies = []

            for filename in config_filenames:
                try:
                    file_path = os.path.join(config_dir, filename)

                    if not os.path.exists(file_path):
                        failed_strategies.append({
                            'filename': filename,
                            'error': '配置文件不存在'
                        })
                        continue

                    # 从文件名推断策略名称
                    strat_name = os.path.splitext(filename)[0]

                    # 调用 gtrade HTTP Gateway 的 /api/strategy/add 接口
                    # 传入配置文件的绝对路径
                    try:
                        response = requests.post(
                            f"{gtrade_gateway}/api/strategy/add",
                            json={"config_path": file_path},
                            timeout=10
                        )

                        if response.status_code == 200:
                            response_data = response.json()
                            if response_data.get('success'):
                                created_strategies.append({
                                    'filename': filename,
                                    'name': strat_name
                                })
                                logger.info(f"从配置文件创建策略成功: {filename} -> {strat_name}")
                            else:
                                error_msg = response_data.get('error', '未知错误')
                                failed_strategies.append({
                                    'filename': filename,
                                    'error': error_msg
                                })
                                logger.warning(f"创建策略失败 {filename}: {error_msg}")
                        else:
                            error_msg = f"HTTP {response.status_code}"
                            try:
                                error_data = response.json()
                                error_msg = error_data.get('error', error_msg)
                            except:
                                pass
                            failed_strategies.append({
                                'filename': filename,
                                'error': error_msg
                            })
                            logger.warning(f"创建策略失败 {filename}: {error_msg}")

                    except requests.exceptions.Timeout:
                        failed_strategies.append({
                            'filename': filename,
                            'error': 'gtrade 服务响应超时'
                        })
                        logger.error(f"创建策略超时 {filename}")
                    except requests.exceptions.ConnectionError:
                        failed_strategies.append({
                            'filename': filename,
                            'error': '无法连接到 gtrade 服务，请确认 gtrade 正在运行'
                        })
                        logger.error(f"无法连接到 gtrade 服务: {filename}")
                    except Exception as e:
                        failed_strategies.append({
                            'filename': filename,
                            'error': str(e)
                        })
                        logger.error(f"调用 gtrade API 失败 {filename}: {e}")

                except Exception as e:
                    failed_strategies.append({
                        'filename': filename,
                        'error': str(e)
                    })
                    logger.error(f"处理配置文件失败 {filename}: {e}")

            return {
                'success_count': len(created_strategies),
                'fail_count': len(failed_strategies),
                'created': created_strategies,
                'failed': failed_strategies
            }

        except Exception as e:
            logger.error(f"批量创建策略失败: {e}")
            raise

# 全局策略服务实例
strategy_service = StrategyService()

def get_strategy_service():
    """获取策略服务实例"""
    return strategy_service