"""
SMA 均线突破策略示例

演示 gtrade_py 框架的基本用法：
  - on_init: 读取配置、订阅 K 线收盘事件和成交推送
  - on_kline_close: 计算 SMA-20，价格突破时下市价多单
  - on_trade_push: 打印成交信息

YAML 配置示例（strategy_config/sma_py.yml）：
  strat_id: sma_py_001
  account_id: main_account
  market: okx
  instrument: BTC-USDT-SWAP
  fee: 0.0005
  python_file: /root/strategies/sma_strategy.py
  python_class: SMAStrategy
  python_interpreter: python3
  auto_restart: true
  max_restart_count: 3
"""

from gtrade_py import StrategyBase


class SMAStrategy(StrategyBase):
    """SMA-20 均线突破策略。

    价格突破 20 周期均线时开多，策略仅演示框架调用，不含完整风控。
    """

    def on_init(self, config: dict) -> bool:
        self.market  = config['market']
        self.inst    = config['instrument']
        self.acct    = config['account_id']
        self.closes  = []
        self.in_pos  = False   # 简单仓位标志

        # 订阅小时 K 线收盘事件
        self.subscribe_kline_close(self.market, self.inst, 1, 'H')
        # 订阅成交/持仓/资金推送
        self.subscribe_trade(self.market, self.acct, self.inst)

        print(f'[SMAStrategy] on_init: market={self.market} inst={self.inst}',
              flush=True)
        return True

    def on_kline_close(self, kline) -> None:
        self.closes.append(kline.close)

        # 保留最近 30 根 K 线，节省内存
        if len(self.closes) > 30:
            self.closes = self.closes[-30:]

        if len(self.closes) < 20:
            return

        sma20 = sum(self.closes[-20:]) / 20
        price = kline.close

        print(f'[SMAStrategy] close={price:.2f} sma20={sma20:.2f}', flush=True)

        # 突破均线且未持仓时开多
        if price > sma20 and not self.in_pos:
            self.in_pos = True
            private_no = self.place_order(
                market=self.market,
                account_id=self.acct,
                inst_id=self.inst,
                bs_side='b',       # 买
                pos_side='l',      # 多方向
                oc_side='o',       # 开仓
                price_type='m',    # 市价
                trade_mode='c',    # 全仓
                amount=0.01,
            )
            print(f'[SMAStrategy] place_order: private_no={private_no}', flush=True)

    def on_trade_push(self, trade) -> None:
        inst  = trade.instrument.decode('utf-8', errors='replace').rstrip('\x00')
        price = trade.td_px
        qty   = trade.td_qty
        print(f'[SMAStrategy] trade: inst={inst} price={price} qty={qty}', flush=True)

    def on_place_order_confirm(self, private_no: str, entno: int,
                               status: str) -> None:
        print(f'[SMAStrategy] confirm: private_no={private_no} '
              f'entno={entno} status={status}', flush=True)
        # 废单时重置仓位标志
        if status == '9':
            self.in_pos = False

    def on_stop(self) -> bool:
        print('[SMAStrategy] on_stop called', flush=True)
        return True
