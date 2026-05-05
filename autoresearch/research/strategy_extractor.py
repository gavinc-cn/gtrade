"""使用 Claude API 从论文摘要或代码中提取可执行策略规格。

输出 strategy_spec.json 格式（供 codegen 使用）。
"""

import json
import anthropic
from autoresearch.config import get as cfg

_EXTRACTION_PROMPT = """你是一位量化策略分析师。请从以下内容中提取可实现的交易策略规格。

输入内容：
---
{content}
---

请严格按 JSON 格式输出，不要包含任何其他文字：
{{
  "title": "策略名称（英文，用于Python类名，如 MomentumBreakout）",
  "signal_logic": "详细的信号生成逻辑描述（中文）",
  "entry_conditions": ["进场条件1", "进场条件2"],
  "exit_conditions": ["出场条件1", "出场条件2"],
  "params": {{"参数名": 默认值}},
  "param_ranges": {{"参数名": [最小值, 最大值]}},
  "data_requirements": {{
    "channel": "depth1",
    "instrument": "BTC-USDT-SWAP",
    "market": "okx",
    "kline_interval": "1H"
  }},
  "risk_notes": "风险说明（中文）",
  "implementable": true
}}

如果内容不包含可实现的交易策略，将 implementable 设为 false，其他字段可为空。"""


def extract_strategy_spec(content: str, source: str = "") -> dict | None:
    """从文本内容提取策略规格。

    Args:
        content: 论文摘要或代码片段
        source: 来源标识（如 arxiv:2401.00001）

    Returns:
        strategy_spec dict，若不可提取则返回 None
    """
    client = anthropic.Anthropic(api_key=cfg()["anthropic_api_key"])
    message = client.messages.create(
        model=cfg()["claude_model"],
        max_tokens=1024,
        messages=[{
            "role": "user",
            "content": _EXTRACTION_PROMPT.format(content=content[:4000])
        }]
    )
    text = message.content[0].text.strip()

    # 提取 JSON（Claude 有时会在 JSON 前后加说明文字）
    start = text.find("{")
    end = text.rfind("}") + 1
    if start == -1 or end == 0:
        return None

    spec = json.loads(text[start:end])
    spec["source"] = source
    if not spec.get("implementable", True):
        return None
    return spec
