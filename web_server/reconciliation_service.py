#!/usr/bin/env python3
"""
持仓核算服务
定期核算持仓一致性，检查:
1. portfolio_position 汇总后与 position 是否一致
2. trade 汇总后与 position 是否一致
"""

import logging
import threading
import time
from datetime import datetime
from typing import Dict, Set, Tuple
from collections import defaultdict

logger = logging.getLogger(__name__)


class ReconciliationService:
    """持仓核算服务"""

    def __init__(self, db_manager):
        self.db_manager = db_manager
        # 存储不一致的持仓记录: key=(market, account_id, instrument, pos_side), value=不一致类型列表
        self.inconsistent_positions: Dict[Tuple[str, str, str, str], Set[str]] = {}
        self.last_reconciliation_time = None
        self.reconciliation_lock = threading.Lock()
        self._running = False
        self._timer = None

    def start(self, interval_seconds=60):
        """
        启动定时核算任务

        Args:
            interval_seconds: 核算间隔（秒），默认60秒（1分钟）
        """
        if self._running:
            logger.warning("Reconciliation service is already running")
            return

        self._running = True
        logger.info(f"Starting reconciliation service with interval {interval_seconds} seconds")
        self._schedule_next_run(interval_seconds)

    def stop(self):
        """停止定时核算任务"""
        self._running = False
        if self._timer:
            self._timer.cancel()
            self._timer = None
        logger.info("Reconciliation service stopped")

    def _schedule_next_run(self, interval_seconds):
        """调度下一次执行"""
        if not self._running:
            return

        def run_and_schedule():
            try:
                self.reconcile()
            except Exception as e:
                logger.error(f"Reconciliation failed: {e}", exc_info=True)
            finally:
                if self._running:
                    self._schedule_next_run(interval_seconds)

        self._timer = threading.Timer(interval_seconds, run_and_schedule)
        self._timer.daemon = True
        self._timer.start()

    def reconcile(self):
        """
        执行持仓核算
        检查 portfolio_position 和 trade 与 position 的一致性
        """
        with self.reconciliation_lock:
            logger.info("Starting position reconciliation")
            start_time = time.time()

            try:
                # 清空之前的不一致记录
                self.inconsistent_positions.clear()

                # 执行两种核算
                self._reconcile_portfolio_positions()
                self._reconcile_trade_positions()

                self.last_reconciliation_time = datetime.now()

                elapsed = time.time() - start_time
                logger.info(f"Reconciliation completed in {elapsed:.2f}s, "
                           f"found {len(self.inconsistent_positions)} inconsistent positions")

            except Exception as e:
                logger.error(f"Error during reconciliation: {e}", exc_info=True)
                raise

    def _reconcile_portfolio_positions(self):
        """
        核算组合持仓与持仓表的一致性
        组合持仓表中不同组合加总后，应与持仓表中对应的持仓一致
        """
        connection = None
        try:
            connection = self.db_manager.get_connection()
            cursor = connection.cursor()

            # 从 portfolio_position 按 (market, account_id, instrument, pos_side) 分组汇总
            query = """
                SELECT
                    market,
                    account_id,
                    instrument,
                    pos_side,
                    SUM(available) as total_available
                FROM portfolio_position
                GROUP BY market, account_id, instrument, pos_side
            """
            cursor.execute(query)
            portfolio_aggregated = cursor.fetchall()

            # 从 position 表获取对应数据
            for row in portfolio_aggregated:
                market, account_id, instrument, pos_side, portfolio_total = row

                # 查询 position 表中对应记录
                position_query = """
                    SELECT available
                    FROM position
                    WHERE market = %s AND account_id = %s
                      AND instrument = %s AND pos_side = %s
                """
                cursor.execute(position_query, (market, account_id, instrument, pos_side))
                position_row = cursor.fetchone()

                if position_row is None:
                    # position 表中不存在，但 portfolio_position 有数据
                    logger.warning(
                        f"Position missing in position table but exists in portfolio_position: "
                        f"{market}/{account_id}/{instrument}/{pos_side}, "
                        f"portfolio total: {portfolio_total}"
                    )
                    key = (market, account_id, instrument, pos_side)
                    if key not in self.inconsistent_positions:
                        self.inconsistent_positions[key] = set()
                    self.inconsistent_positions[key].add('portfolio_mismatch')
                else:
                    position_available = position_row[0]

                    # 比较数值（允许小的浮点误差）
                    diff = abs(portfolio_total - position_available)
                    tolerance = 1e-6  # 允许的误差范围

                    if diff > tolerance:
                        logger.warning(
                            f"Position mismatch (portfolio): "
                            f"{market}/{account_id}/{instrument}/{pos_side}, "
                            f"portfolio total: {portfolio_total}, "
                            f"position: {position_available}, "
                            f"diff: {diff}"
                        )
                        key = (market, account_id, instrument, pos_side)
                        if key not in self.inconsistent_positions:
                            self.inconsistent_positions[key] = set()
                        self.inconsistent_positions[key].add('portfolio_mismatch')

            cursor.close()

        except Exception as e:
            logger.error(f"Error reconciling portfolio positions: {e}", exc_info=True)
            raise
        finally:
            if connection:
                connection.close()

    def _reconcile_trade_positions(self):
        """
        核算成交与持仓表的一致性
        通过成交表把所有成交加总后，应当与持仓表一致

        注意：这里的逻辑是计算净持仓变化
        - 买入开多/卖出平空: 增加多头持仓
        - 卖出开空/买入平多: 增加空头持仓
        """
        connection = None
        try:
            connection = self.db_manager.get_connection()
            cursor = connection.cursor()

            # 从 trade 表按 (market, account_id, instrument, pos_side) 分组汇总
            # td_side: 'B' 买入, 'S' 卖出
            # pos_side: 'l' 多, 's' 空, 'n' 净
            query = """
                SELECT
                    market,
                    account_id,
                    instrument,
                    pos_side,
                    td_side,
                    SUM(td_qty) as total_qty
                FROM trade
                GROUP BY market, account_id, instrument, pos_side, td_side
            """
            cursor.execute(query)
            trade_aggregated = cursor.fetchall()

            # 按 (market, account_id, instrument, pos_side) 计算净持仓
            net_positions = defaultdict(float)
            for row in trade_aggregated:
                market, account_id, instrument, pos_side, td_side, total_qty = row
                key = (market, account_id, instrument, pos_side)

                # 计算净持仓变化
                # 对于多头(l): 买入增加，卖出减少
                # 对于空头(s): 卖出增加，买入减少
                if pos_side == 'l':  # 多头
                    if td_side == 'B':  # 买入
                        net_positions[key] += total_qty
                    else:  # 卖出
                        net_positions[key] -= total_qty
                elif pos_side == 's':  # 空头
                    if td_side == 'S':  # 卖出
                        net_positions[key] += total_qty
                    else:  # 买入
                        net_positions[key] -= total_qty
                else:  # 净持仓
                    # 买入为正，卖出为负
                    if td_side == 'B':
                        net_positions[key] += total_qty
                    else:
                        net_positions[key] -= total_qty

            # 与 position 表对比
            for key, trade_total in net_positions.items():
                market, account_id, instrument, pos_side = key

                # 查询 position 表中对应记录
                position_query = """
                    SELECT available
                    FROM position
                    WHERE market = %s AND account_id = %s
                      AND instrument = %s AND pos_side = %s
                """
                cursor.execute(position_query, (market, account_id, instrument, pos_side))
                position_row = cursor.fetchone()

                if position_row is None:
                    # position 表中不存在，但 trade 有数据
                    logger.warning(
                        f"Position missing in position table but exists in trades: "
                        f"{market}/{account_id}/{instrument}/{pos_side}, "
                        f"trade total: {trade_total}"
                    )
                    if key not in self.inconsistent_positions:
                        self.inconsistent_positions[key] = set()
                    self.inconsistent_positions[key].add('trade_mismatch')
                else:
                    position_available = position_row[0]

                    # 比较数值（允许小的浮点误差）
                    diff = abs(trade_total - position_available)
                    tolerance = 1e-6  # 允许的误差范围

                    if diff > tolerance:
                        logger.warning(
                            f"Position mismatch (trade): "
                            f"{market}/{account_id}/{instrument}/{pos_side}, "
                            f"trade total: {trade_total}, "
                            f"position: {position_available}, "
                            f"diff: {diff}"
                        )
                        if key not in self.inconsistent_positions:
                            self.inconsistent_positions[key] = set()
                        self.inconsistent_positions[key].add('trade_mismatch')

            cursor.close()

        except Exception as e:
            logger.error(f"Error reconciling trade positions: {e}", exc_info=True)
            raise
        finally:
            if connection:
                connection.close()

    def is_position_inconsistent(self, market, account_id, instrument, pos_side):
        """
        检查指定持仓是否不一致

        Args:
            market: 市场
            account_id: 账户ID
            instrument: 标的
            pos_side: 持仓方向

        Returns:
            bool: True 表示不一致，False 表示一致
        """
        with self.reconciliation_lock:
            key = (market, account_id, instrument, pos_side)
            return key in self.inconsistent_positions

    def get_inconsistent_positions(self):
        """
        获取所有不一致的持仓

        Returns:
            dict: 不一致持仓的字典，key为(market, account_id, instrument, pos_side)，value为不一致类型集合
        """
        with self.reconciliation_lock:
            return dict(self.inconsistent_positions)

    def get_reconciliation_status(self):
        """
        获取核算状态

        Returns:
            dict: 核算状态信息
        """
        with self.reconciliation_lock:
            return {
                'last_reconciliation_time': self.last_reconciliation_time.isoformat() if self.last_reconciliation_time else None,
                'inconsistent_count': len(self.inconsistent_positions),
                'running': self._running
            }


# 全局单例
_reconciliation_service = None


def get_reconciliation_service():
    """获取核算服务单例"""
    global _reconciliation_service
    if _reconciliation_service is None:
        from database import get_db_manager
        db_manager = get_db_manager()
        _reconciliation_service = ReconciliationService(db_manager)
    return _reconciliation_service
