"""验证生成的策略代码。"""

import ast
import os


class ValidationError(Exception):
    pass


def validate_strategy_code(code: str, class_name: str) -> None:
    """验证 Python 策略代码。

    Checks:
    1. 语法正确（ast.parse）
    2. 类定义存在且继承 StrategyBase
    3. on_init / on_kline_close 方法存在

    Raises:
        ValidationError: 验证失败时
    """
    try:
        tree = ast.parse(code)
    except SyntaxError as e:
        raise ValidationError(f"语法错误: {e}") from e

    class_defs = {
        node.name: node
        for node in ast.walk(tree)
        if isinstance(node, ast.ClassDef)
    }

    if class_name not in class_defs:
        raise ValidationError(f"未找到类 {class_name}，代码中的类: {list(class_defs)}")

    cls = class_defs[class_name]
    base_names = [
        (b.id if isinstance(b, ast.Name) else
         b.attr if isinstance(b, ast.Attribute) else "")
        for b in cls.bases
    ]
    if "StrategyBase" not in base_names:
        raise ValidationError(f"{class_name} 未继承 StrategyBase，实际基类: {base_names}")

    methods = {
        node.name
        for node in ast.walk(cls)
        if isinstance(node, ast.FunctionDef)
    }
    for required in ("on_init", "on_kline_close"):
        if required not in methods:
            raise ValidationError(f"缺少必要方法: {required}")


def save_strategy(code: str, class_name: str, strategy_dir: str) -> str:
    """保存策略代码到文件，返回绝对路径。"""
    os.makedirs(strategy_dir, exist_ok=True)
    file_name = f"strat_{class_name.lower()}.py"
    file_path = os.path.join(strategy_dir, file_name)
    with open(file_path, "w", encoding="utf-8") as f:
        f.write(code)
    return file_path
