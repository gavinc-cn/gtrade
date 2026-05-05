"""使用 Claude API 根据 strategy_spec 生成完整 Python 策略代码。"""

import anthropic
from autoresearch.config import get as cfg
from autoresearch.codegen.template import STRATEGY_TEMPLATE

_CODEGEN_PROMPT = """你是一位精通量化交易的 Python 开发者。
请根据以下策略规格，在提供的模板基础上，生成完整的 Python 策略代码。

## 策略规格
标题：{title}
信号逻辑：{signal_logic}
进场条件：{entry_conditions}
出场条件：{exit_conditions}
参数：{params}
风险说明：{risk_notes}

## 代码架构要求（Alpha 分层）
请将策略逻辑明确分为两层，有助于后续 IC 评估和参数优化：

**第一层：Alpha 信号层**
实现一个独立的方法 `compute_alpha(self, kline) -> float`：
- 仅做信号计算，返回一个浮点数（正值=看多、负值=看空、0=无信号）
- 不包含任何下单逻辑
- 将计算结果追加到 `self._alpha_history` 列表（用于 IC 计算）
- 示例：SMA策略返回 (close - sma) / sma；动量策略返回 n期收益率

**第二层：执行层**
在 `on_kline_close` 中调用 `compute_alpha()`，根据返回值决定开平仓：
- alpha > threshold 且未持仓 → 开多
- alpha < -threshold 且持仓 → 平多
- 将信号值记录到日志（供 IC 计算回测器读取）

## 接口规范
1. 继承 gtrade_py.StrategyBase
2. 类名必须是 {class_name}
3. on_init 中从 config 读取参数（使用 config.get("param_name", default)）
4. on_init 中初始化 `self._alpha_history = []`（用于 IC 回算）
5. 使用 self.place_order(market, account_id, inst_id, bs_side, pos_side, oc_side,
   price_type, trade_mode, amount, price=0.0, private_no='') 下单
   完整签名与 StrategyBase.place_order 一致，price 市价单传 0.0，
   private_no 留空由框架自动生成；on_place_order_confirm 通过 private_no 对账
6. bs_side: 'b'=买 's'=卖；pos_side: 'l'=多 's'=空；oc_side: 'o'=开 'c'=平；price_type: 'm'=市价
7. kline_coeff（整数）和 kline_scale（'H'/'D'/'m'）是 subscribe_kline_close 的两个参数，
   从 config 读取：config.get("kline_coeff", 1)，config.get("kline_scale", "H")
8. 有仓位时通过 self.in_pos 标志控制，避免重复开仓
9. 加注释解释关键逻辑
10. 仅输出 Python 代码，不要包含任何 markdown 标记或说明文字

## 代码模板
{template}

请生成完整的策略代码："""


def generate_strategy_code(spec: dict) -> str:
    """根据 strategy_spec 生成 Python 策略代码。

    Returns:
        Python 源代码字符串
    """
    class_name = spec_to_class_name(spec)
    param_assignments = "\n        ".join(
        f'self.{k} = config.get("{k}", {repr(v)})'
        for k, v in spec.get("params", {}).items()
    )
    template = STRATEGY_TEMPLATE.format(
        title=spec["title"],
        source=spec.get("source", ""),
        signal_logic=spec["signal_logic"],
        class_name=class_name,
        param_assignments=param_assignments or "pass",
    )

    client = anthropic.Anthropic(api_key=cfg()["anthropic_api_key"])
    message = client.messages.create(
        model=cfg()["claude_model"],
        max_tokens=3000,
        messages=[{
            "role": "user",
            "content": _CODEGEN_PROMPT.format(
                title=spec["title"],
                signal_logic=spec["signal_logic"],
                entry_conditions=spec.get("entry_conditions", []),
                exit_conditions=spec.get("exit_conditions", []),
                params=spec.get("params", {}),
                risk_notes=spec.get("risk_notes", ""),
                class_name=class_name,
                template=template,
            )
        }]
    )
    return message.content[0].text.strip()


def spec_to_class_name(spec: dict) -> str:
    """从策略规格推导 Python 类名（去空格和连字符）。"""
    return spec["title"].replace(" ", "").replace("-", "")
