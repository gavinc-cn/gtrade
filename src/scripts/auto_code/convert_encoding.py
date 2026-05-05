# coding=utf-8
"""
将指定的文件或文件夹转换为UTF-8编码，换行符改为\n (Unix风格)

用法:
    直接修改脚本底部的配置区域，然后运行: python convert_encoding.py

配置项:
    paths       要转换的文件或目录路径列表
    ext         要处理的文件扩展名列表，为空则使用默认扩展名
    dry_run     True: 只预览不修改, False: 实际修改
"""
import os
import sys

# chardet是可选依赖，没有时使用简单的编码检测
try:
    import chardet
    HAS_CHARDET = True
except ImportError:
    HAS_CHARDET = False

# 默认处理的文件扩展名
DEFAULT_EXTENSIONS = {'.cpp', '.h', '.hpp', '.c', '.cc', '.cxx',
                      '.py', '.yml', '.yaml', '.json', '.md', '.txt',
                      '.cmake', '.sh', '.bat', '.xml', '.html', '.css', '.js', '.ts'}


def detect_encoding(file_path):
    """
    检测文件编码
    返回检测到的编码名称
    """
    if HAS_CHARDET:
        with open(file_path, 'rb') as f:
            raw_data = f.read()
        result = chardet.detect(raw_data)
        return result['encoding'] if result['encoding'] else 'utf-8'
    else:
        # 没有chardet时返回None，由调用者尝试常见编码
        return None


def has_crlf(content):
    """
    检查内容是否包含CRLF换行符
    """
    return '\r\n' in content or '\r' in content


def read_file_with_encoding(file_path, encoding):
    """
    以二进制模式读取文件并解码，保留原始换行符
    """
    with open(file_path, 'rb') as f:
        raw_data = f.read()
    return raw_data.decode(encoding)


def convert_file(file_path, dry_run=False):
    """
    转换单个文件的编码为UTF-8，换行符为LF

    参数:
        file_path: 文件路径
        dry_run: 如果为True，只检查不修改

    返回:
        (changed, message) - changed表示是否需要修改，message为描述信息
    """
    try:
        # 检测原始编码
        original_encoding = detect_encoding(file_path)

        # 读取文件内容 (以二进制模式读取并解码，保留原始换行符)
        try:
            content = read_file_with_encoding(file_path, 'utf-8')
            current_encoding = 'utf-8'
        except UnicodeDecodeError:
            # 如果UTF-8读取失败，使用检测到的编码
            if original_encoding:
                try:
                    content = read_file_with_encoding(file_path, original_encoding)
                    current_encoding = original_encoding
                except (UnicodeDecodeError, TypeError):
                    original_encoding = None

            if not original_encoding:
                # 尝试常见编码
                for enc in ['gbk', 'gb2312', 'gb18030', 'latin-1']:
                    try:
                        content = read_file_with_encoding(file_path, enc)
                        current_encoding = enc
                        break
                    except UnicodeDecodeError:
                        continue
                else:
                    return False, f"无法解码文件: {file_path}"

        # 检查是否需要修改
        need_encoding_change = current_encoding.lower() not in ['utf-8', 'ascii']
        need_newline_change = has_crlf(content)

        if not need_encoding_change and not need_newline_change:
            return False, None

        # 转换换行符
        new_content = content.replace('\r\n', '\n').replace('\r', '\n')

        changes = []
        if need_encoding_change:
            changes.append(f"编码: {current_encoding} -> utf-8")
        if need_newline_change:
            changes.append("换行符: CRLF -> LF")

        message = f"{file_path}: {', '.join(changes)}"

        if not dry_run:
            # 写入文件
            with open(file_path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(new_content)

        return True, message

    except Exception as e:
        return False, f"处理文件失败 {file_path}: {str(e)}"


def get_files_to_process(path, extensions):
    """
    获取需要处理的文件列表

    参数:
        path: 文件或目录路径
        extensions: 要处理的文件扩展名集合

    返回:
        文件路径列表
    """
    files = []

    if os.path.isfile(path):
        ext = os.path.splitext(path)[1].lower()
        if ext in extensions or not extensions:
            files.append(path)
    elif os.path.isdir(path):
        for root, _, filenames in os.walk(path):
            # 跳过隐藏目录和常见的忽略目录
            if any(part.startswith('.') for part in root.split(os.sep)):
                continue
            if any(ignore in root for ignore in ['node_modules', '__pycache__', 'build', 'cmake-build']):
                continue

            for filename in filenames:
                ext = os.path.splitext(filename)[1].lower()
                if ext in extensions:
                    files.append(os.path.join(root, filename))

    return files


def run(paths, ext=None, dry_run=False):
    """
    执行编码转换

    参数:
        paths: 要处理的文件或目录路径列表
        ext: 要处理的扩展名列表，为空则使用默认扩展名
        dry_run: 如果为True，只检查不修改
    """
    # 解析扩展名
    if ext:
        extensions = set(e.strip() if e.startswith('.') else f'.{e.strip()}' for e in ext)
    else:
        extensions = DEFAULT_EXTENSIONS

    # 收集所有要处理的文件
    all_files = []
    for path in paths:
        target_path = os.path.abspath(path)
        if not os.path.exists(target_path):
            print(f"错误: 路径不存在: {target_path}")
            continue
        all_files.extend(get_files_to_process(target_path, extensions))

    if not all_files:
        print(f"没有找到需要处理的文件 (扩展名: {', '.join(sorted(extensions))})")
        return

    print(f"{'[DRY RUN] ' if dry_run else ''}扫描 {len(all_files)} 个文件...")

    # 处理文件
    changed_count = 0
    for file_path in all_files:
        changed, message = convert_file(file_path, dry_run)
        if changed and message:
            print(message)
            changed_count += 1

    # 输出统计
    action = "将要修改" if dry_run else "已修改"
    print(f"\n完成: {action} {changed_count}/{len(all_files)} 个文件")


if __name__ == '__main__':
    # ============ 配置区域 ============
    # 要处理的文件或目录路径列表
    paths = [
        "../../../deploy",
    ]

    # 要处理的扩展名列表，为空则使用默认扩展名
    ext = [
        ".cpp",
        ".sh",
    ]

    # True: 只预览不修改, False: 实际修改
    dry_run = False
    # =================================

    run(paths, ext, dry_run)
