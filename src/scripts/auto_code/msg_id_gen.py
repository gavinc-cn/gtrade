"""
从 src/common/msg_id.h 解析 MsgId 枚举，生成 python/gtrade_py/msg_id.py。

用法（从 src/scripts/auto_code/ 目录下运行）：
    python msg_id_gen.py

或指定路径：
    python msg_id_gen.py --input ../../common/msg_id.h \
                         --output ../../../python/gtrade_py/msg_id.py
"""

import re
import pathlib
import argparse


def parse_enum(header_text: str) -> list[tuple[str, int]]:
    """解析 C++ enum 定义，返回 [(name, value), ...] 列表。

    支持：
      - 普通递增枚举（无显式值）
      - 显式赋值枚举（如 kFoo = 42）
      - 行尾注释（// ...）
    """
    # 找到 enum MsgId { ... } 块
    m = re.search(r'enum\s+MsgId\s*\{([^}]*)\}', header_text, re.DOTALL)
    if not m:
        raise ValueError('未找到 enum MsgId { ... } 块')

    body = m.group(1)
    results = []
    current_val = 0

    for line in body.splitlines():
        # 去掉行尾注释和前后空白
        line = re.sub(r'//.*$', '', line).strip().rstrip(',').strip()
        if not line:
            continue

        # 匹配 name 或 name = value
        m2 = re.match(r'^([A-Za-z_]\w*)\s*(?:=\s*(\d+))?$', line)
        if not m2:
            continue

        name = m2.group(1)
        explicit = m2.group(2)
        if explicit is not None:
            current_val = int(explicit)

        results.append((name, current_val))
        current_val += 1

    return results


def generate_msg_id_py(items: list[tuple[str, int]]) -> str:
    """生成 msg_id.py 内容字符串。"""
    lines = [
        '# 自动生成，勿手改',
        '# 源: src/common/msg_id.h',
        '',
        '',
        'class MsgId:',
    ]
    for name, val in items:
        lines.append(f'    {name} = {val}')
    lines.append('')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description='从 msg_id.h 生成 msg_id.py')
    parser.add_argument(
        '--input',
        default=str(pathlib.Path(__file__).parent.parent.parent / 'common' / 'msg_id.h'),
        help='msg_id.h 路径',
    )
    parser.add_argument(
        '--output',
        default=str(pathlib.Path(__file__).parent.parent.parent.parent / 'python' / 'gtrade_py' / 'msg_id.py'),
        help='输出的 msg_id.py 路径',
    )
    args = parser.parse_args()

    header_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    print(f'读取: {header_path}')
    header_text = header_path.read_text(encoding='utf-8')

    items = parse_enum(header_text)
    if not items:
        raise RuntimeError('未解析到任何枚举成员，请检查 msg_id.h 格式')

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(generate_msg_id_py(items), encoding='utf-8')
    print(f'生成: {output_path}  ({len(items)} 个枚举值)')

    # 打印前几个和后几个，便于确认
    for name, val in items[:5]:
        print(f'  {name} = {val}')
    print('  ...')
    for name, val in items[-3:]:
        print(f'  {name} = {val}')


if __name__ == '__main__':
    main()
