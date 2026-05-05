from xml.etree import ElementTree

def generate_class_code(xml_file, output_file):
    tree = ElementTree.parse(xml_file)
    root = tree.getroot()

    struct_elem = root.find('struct')
    struct_name = struct_elem.attrib['name']

    members = struct_elem.findall('member')

    # Define type to format mapping
    type_map = {
        'int': 'i',
        'short': 'h',
        'float': 'f',
        'double': 'd',
        'char': lambda size: f'{size}s'
    }

    # Generate format string
    fmt = ''
    for member in members:
        type_ = member.attrib['type']
        size = member.attrib.get('size', None)
        if type_ in type_map:
            if callable(type_map[type_]):
                fmt += type_map[type_](size)
            else:
                fmt += type_map[type_]
        else:
            raise ValueError(f'Unknown type: {type_}')

    # Collect member names
    member_names = [member.attrib['name'] for member in members]

    # Generate class code
    class_code = f"import struct\n\n"
    class_code += f"class {struct_name}:\n"
    class_code += f"    _fmt = '{fmt}'\n"
    class_code += f"    _size = struct.calcsize(_fmt)\n\n"

    # __init__ method
    init_params = ', '.join(member_names)
    class_code += f"    def __init__(self, {init_params}):\n"
    for member in member_names:
        class_code += f"        self.{member} = {member}\n"
    class_code += "\n"

    # from_bytes class method
    class_code += f"    @classmethod\n"
    class_code += f"    def from_bytes(cls, data):\n"
    class_code += f"        if len(data) != cls._size:\n"
    class_code += f"            raise ValueError('Data length does not match struct size.')\n"
    unpack_line = f"        {', '.join(member_names)} = struct.unpack(cls._fmt, data)\n"
    class_code += unpack_line
    # Process desc field
    for member in members:
        if member.attrib['type'] == 'char' and 'size' in member.attrib:
            name = member.attrib['name']
            class_code += f"        {name} = {name}.decode('ascii').rstrip('\\0')\n"
    class_code += f"        return cls({', '.join(member_names)})\n\n"

    # to_bytes method
    class_code += f"    def to_bytes(self):\n"
    pack_args = []
    for member in members:
        name = member.attrib['name']
        type_ = member.attrib['type']
        if type_ == 'char' and 'size' in member.attrib:
            size = member.attrib['size']
            pack_args.append(f"self.{name}.encode('ascii').ljust({size}, b'\\0')[:{size}]")
        else:
            pack_args.append(f"self.{name}")
    pack_line = f"        return struct.pack(self._fmt, {', '.join(pack_args)})\n"
    class_code += pack_line

    # Write to output file
    with open(output_file, 'w') as f:
        f.write(class_code)

def generate_ctypes_code(xml_file, output_file):
    """从 XML 结构体描述生成 ctypes.Structure 子类。

    与 generate_class_code() 相比，此函数生成的代码使用 ctypes，
    与 C++ 编译器遵循相同的 x86_64 ABI 对齐规则，自动插入 padding，
    无需手工计算对齐偏移。

    XML 额外支持的属性：
      type="int64"  → ctypes.c_int64
      type="uint64" → ctypes.c_uint64
      type="uint32" → ctypes.c_uint32
      type="uint16" → ctypes.c_uint16
      type="uint8"  → ctypes.c_uint8
      type="bool"   → ctypes.c_bool
      count="N"     → 数组类型，如 double[10] → ctypes.c_double * 10
      type="char" size="1" (或无 size) → ctypes.c_char（单字节）
      type="char" size="N" (N>1)       → ctypes.c_char * N
    """
    tree = ElementTree.parse(xml_file)
    root = tree.getroot()

    struct_elem = root.find('struct')
    struct_name = struct_elem.attrib['name']
    members = struct_elem.findall('member')

    def _ctypes_type(type_, size=None, count=None):
        """把 XML 类型属性转换为 ctypes 类型字符串。"""
        base_map = {
            'int':    'ctypes.c_int',
            'int64':  'ctypes.c_int64',
            'uint64': 'ctypes.c_uint64',
            'uint32': 'ctypes.c_uint32',
            'uint16': 'ctypes.c_uint16',
            'uint8':  'ctypes.c_uint8',
            'double': 'ctypes.c_double',
            'float':  'ctypes.c_float',
            'bool':   'ctypes.c_bool',
        }
        if type_ == 'char':
            sz = int(size) if size is not None else 1
            base = 'ctypes.c_char' if sz == 1 else f'ctypes.c_char * {sz}'
        elif type_ in base_map:
            base = base_map[type_]
        else:
            raise ValueError(f'generate_ctypes_code: unknown type "{type_}"')

        if count:
            # count 用于数值数组，如 double[10]
            base = f'{base} * {count}'
        return base

    # 生成 _fields_ 列表
    fields_lines = []
    for m in members:
        name  = m.attrib['name']
        type_ = m.attrib['type']
        size  = m.attrib.get('size')
        count = m.attrib.get('count')
        ctype = _ctypes_type(type_, size, count)
        desc  = m.attrib.get('desc', '')
        comment = f'  # {desc}' if desc else ''
        fields_lines.append(f"        ('{name}', {ctype}),{comment}")

    fields_str = '\n'.join(fields_lines)

    # 生成简洁的 __repr__，只显示前几个非数组字段
    repr_parts = []
    for m in members:
        if m.attrib.get('count'):
            continue  # 跳过数组字段
        name  = m.attrib['name']
        type_ = m.attrib['type']
        size  = m.attrib.get('size')
        sz    = int(size) if size else 1
        if type_ == 'char' and sz > 1:
            repr_parts.append(
                f"{name}={{self.{name}.decode('utf-8', errors='replace').rstrip(chr(0))}}")
        else:
            repr_parts.append(f"{name}={{self.{name}}}")
        if len(repr_parts) >= 4:
            break
    repr_fmt = ', '.join(repr_parts)

    rel_path = xml_file.replace('\\', '/')
    code = (
        f"# 自动生成，勿手改\n"
        f"# 源: {rel_path}\n"
        f"import ctypes\n"
        f"\n"
        f"\n"
        f"class {struct_name}(ctypes.Structure):\n"
        f"    _fields_ = [\n"
        f"{fields_str}\n"
        f"    ]\n"
        f"\n"
        f"    @classmethod\n"
        f"    def from_bytes(cls, data: bytes):\n"
        f"        assert len(data) == ctypes.sizeof(cls), (\n"
        f"            f'{struct_name}: expected {{ctypes.sizeof(cls)}} bytes, got {{len(data)}}'\n"
        f"        )\n"
        f"        return cls.from_buffer_copy(data)\n"
        f"\n"
        f"    def __repr__(self):\n"
        f"        return f'{struct_name}({repr_fmt})'\n"
    )

    with open(output_file, 'w') as f:
        f.write(code)


if __name__ == '__main__':
    import os, pathlib

    # 原有示例（保持不变）
    generate_class_code('../../interface/demo/device_control.xml', '../../interface/demo/device_control.py')

    # 生成 Python 策略框架所需的 ctypes 结构体
    xml_dir = pathlib.Path('../../interface/python_structs')
    out_dir = pathlib.Path('../../../python/gtrade_py/structs')
    out_dir.mkdir(parents=True, exist_ok=True)

    for xml_file in sorted(xml_dir.glob('*.xml')):
        out_file = out_dir / (xml_file.stem.lower() + '.py')
        print(f'generating {xml_file} → {out_file}')
        generate_ctypes_code(str(xml_file), str(out_file))