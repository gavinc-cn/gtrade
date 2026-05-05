#!/usr/bin/env python3
# coding=utf-8
"""
XML to SQL Builder Generator
根据 db_tables/*.xml 生成模板化的 FieldTraits 特化

Usage:
    python xml_to_sql_builder_generator.py

Author: Claude Code
Date: 2025-12-20
"""

import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Dict, List, Optional, Tuple


class XMLToSqlBuilderGenerator:
    """XML表定义到SQL Builder FieldTraits的生成器"""

    def __init__(self):
        # C++类型到SQL字段类型的映射
        self.type_mapping = {
            'int': 'SqlFieldType::kInt',
            'int64_t': 'SqlFieldType::kInt64',
            'uint16_t': 'SqlFieldType::kUInt16',
            'uint32_t': 'SqlFieldType::kUInt32',
            'uint64_t': 'SqlFieldType::kUInt64',
            'double': 'SqlFieldType::kDouble',
            'float': 'SqlFieldType::kFloat',
            'char': 'SqlFieldType::kChar',
            'CharCs': 'SqlFieldType::kChar',
            'MarketCs': 'SqlFieldType::kCharArray',
            'AccountIdCs': 'SqlFieldType::kCharArray',
            'InstrumentCs': 'SqlFieldType::kCharArray',
            'PortfolioCs': 'SqlFieldType::kCharArray',
            'PolicyNoCs': 'SqlFieldType::kCharArray',
            'PrivateNoCs': 'SqlFieldType::kCharArray',
            'DateTimeCs': 'SqlFieldType::kCharArray',
            'ErrMsgCs': 'SqlFieldType::kCharArray',
        }

        # 需要转义的类型（字符串类型，可能包含单引号等特殊字符）
        self.escape_types = [
            'MarketCs', 'AccountIdCs', 'InstrumentCs', 'PortfolioCs',
            'PolicyNoCs', 'PrivateNoCs', 'DateTimeCs', 'ErrMsgCs'
        ]

        # 需要加引号的字符类型（标记为需要转义标志，但实际不转义）
        self.char_types = ['CharCs', 'char']

    def _get_sql_field_type(self, cpp_type: str) -> str:
        """获取SQL字段类型"""
        return self.type_mapping.get(cpp_type, 'SqlFieldType::kString')

    def _needs_escape(self, cpp_type: str) -> bool:
        """判断是否需要转义标志（字符串类型和字符类型都标记为 true）"""
        return cpp_type in self.escape_types or cpp_type in self.char_types

    def _is_string_type(self, cpp_type: str) -> bool:
        """判断是否为字符串类型（需要加引号）"""
        return cpp_type in self.escape_types or cpp_type == 'CharCs'

    def _parse_table_xml(self, xml_path: str) -> Dict:
        """解析单个XML文件"""
        try:
            tree = ET.parse(xml_path)
            root = tree.getroot()
        except ET.ParseError as e:
            raise ValueError(f"XML文件解析错误: {e}")

        if root.tag != 'table':
            return None

        table_info = {
            'name': root.get('st_name'),  # 结构体名称
            'table_name': '',  # SQL表名（从indexes获取）
            'comment': root.get('comment', ''),
            'fields': [],
            'primary_keys': []
        }

        # 解析字段
        fields_element = root.find('fields')
        if fields_element is not None:
            for field in fields_element.findall('field'):
                cpp_type = field.get('type')
                field_info = {
                    'name': field.get('name'),
                    'cpp_type': cpp_type,
                    'sql_type': self._get_sql_field_type(cpp_type),
                    'needs_escape': self._needs_escape(cpp_type),
                    'is_string': self._is_string_type(cpp_type),
                    'comment': field.get('comment', '')
                }
                table_info['fields'].append(field_info)

        # 解析索引和表名
        indexes_element = root.find('indexes')
        if indexes_element is not None:
            table_info['table_name'] = indexes_element.get('table_name', table_info['name'].lower())

            # 解析主键
            primary_key_element = indexes_element.find('primary_key')
            if primary_key_element is not None:
                for field in primary_key_element.findall('field'):
                    table_info['primary_keys'].append(field.text)

        return table_info

    def _generate_field_traits(self, table_info: Dict) -> str:
        """生成 FieldTraits 特化"""
        struct_name = table_info['name']
        table_name = table_info['table_name']
        fields = table_info['fields']
        primary_keys = table_info['primary_keys']

        lines = []

        # 注释
        lines.append(f"// FieldTraits 特化 - {table_info['comment']}")
        lines.append(f"template<>")
        lines.append(f"struct FieldTraits<{struct_name}> {{")

        # 表名
        lines.append(f"    static constexpr std::string_view kTableName = \"{table_name}\";")
        lines.append("")

        # 字段元数据数组
        lines.append(f"    // 字段元数据 ({len(fields)}个字段)")
        lines.append(f"    static constexpr std::array<FieldMetadata, {len(fields)}> kFields = {{{{")

        for field in fields:
            escape_flag = "true" if field['needs_escape'] else "false"
            lines.append(f"        {{\"{field['name']}\", {field['sql_type']}, {escape_flag}}},  // {field['comment']}")

        lines.append(f"    }}}};")
        lines.append("")

        # 主键数组
        if primary_keys:
            lines.append(f"    // 主键 ({len(primary_keys)}个)")
            pk_list = ', '.join([f'"{pk}"' for pk in primary_keys])
            lines.append(f"    static constexpr std::array<std::string_view, {len(primary_keys)}> kPrimaryKeys = {{{pk_list}}};")
        else:
            lines.append(f"    // 无主键")
            lines.append(f"    static constexpr std::array<std::string_view, 0> kPrimaryKeys = {{}};")
        lines.append("")

        # GetFieldValue 函数
        lines.append(f"    // 获取字段值")
        lines.append(f"    static std::string GetFieldValue(const {struct_name}& data, size_t field_index) {{")
        lines.append(f"        switch (field_index) {{")

        for i, field in enumerate(fields):
            field_name = field['name']
            cpp_type = field['cpp_type']

            lines.append(f"            case {i}:  // {field_name} ({cpp_type})")

            if cpp_type in ['int', 'int64_t', 'uint16_t', 'uint32_t', 'uint64_t']:
                lines.append(f"                return std::to_string(data.{field_name});")
            elif cpp_type in ['double', 'float']:
                # double 类型使用 DoubleToSqlString 处理 NaN 和 Inf
                lines.append(f"                return DoubleToSqlString(data.{field_name});")
            elif cpp_type == 'CharCs' or cpp_type == 'char':
                # 字符类型不转义，直接加引号
                lines.append(f"                return fmt::format(\"'{{}}'\", data.{field_name});")
            elif cpp_type in self.escape_types:
                # 需要转义的字符串类型
                lines.append(f"                return fmt::format(\"'{{}}'\", SimpleEscapeString(data.{field_name}));")
            else:
                # 普通字符串类型
                lines.append(f"                return fmt::format(\"'{{}}'\", data.{field_name});")

        lines.append(f"            default:")
        lines.append(f"                return \"NULL\";")
        lines.append(f"        }}")
        lines.append(f"    }}")

        lines.append(f"}};")
        lines.append("")

        return '\n'.join(lines)

    def _generate_header_file(self, table_infos: List[Dict]) -> str:
        """生成头文件内容"""
        lines = []

        # 文件头
        lines.append("//")
        lines.append("// SQL Builder Field Traits - 自动生成")
        lines.append("// 警告: 此文件由脚本自动生成，请勿手动修改！")
        lines.append("//")
        lines.append("")
        lines.append("#pragma once")
        lines.append("")
        lines.append("#include \"sql_builder.h\"")
        lines.append("#include \"db_structures.h\"")
        lines.append("#include <string>")
        lines.append("#include <cmath>")
        lines.append("#include <spdlog/fmt/fmt.h>")
        lines.append("")
        lines.append("namespace gtrade {")
        lines.append("")

        # 简单字符串转义函数（内联实现）
        lines.append("// 简单字符串转义函数（用于 FieldTraits）")
        lines.append("inline std::string SimpleEscapeString(const std::string& str) {")
        lines.append("    std::string escaped = str;")
        lines.append("    size_t pos = 0;")
        lines.append("    while ((pos = escaped.find('\\'', pos)) != std::string::npos) {")
        lines.append("        escaped.replace(pos, 1, \"\\\\'\");")
        lines.append("        pos += 2;")
        lines.append("    }")
        lines.append("    return escaped;")
        lines.append("}")
        lines.append("")

        # 安全的 double 转 SQL 字符串函数（处理 NaN 和 Inf）
        lines.append("// 安全的 double 转 SQL 字符串（处理 NaN 和 Inf）")
        lines.append("inline std::string DoubleToSqlString(double value) {")
        lines.append("    if (std::isnan(value) || std::isinf(value)) {")
        lines.append("        return \"NULL\";  // NaN 和 Inf 转换为 SQL 的 NULL")
        lines.append("    }")
        lines.append("    return std::to_string(value);")
        lines.append("}")
        lines.append("")

        # 生成每个表的 FieldTraits
        for table_info in table_infos:
            if table_info:
                lines.append(self._generate_field_traits(table_info))

        lines.append("} // namespace gtrade")
        lines.append("")

        return '\n'.join(lines)

    def generate_from_xml_directory(self, xml_dir: str, output_file: str):
        """从XML目录生成FieldTraits头文件"""
        print(f"正在扫描XML目录: {xml_dir}")

        # 获取所有XML文件
        xml_files = sorted([f for f in os.listdir(xml_dir)
                           if f.endswith('.xml') and not f.startswith('db_tables_old')])

        table_infos = []

        # 解析每个XML文件
        for xml_file in xml_files:
            xml_path = os.path.join(xml_dir, xml_file)
            try:
                table_info = self._parse_table_xml(xml_path)
                if table_info:
                    table_infos.append(table_info)
                    print(f"  ✓ 解析表: {table_info['name']} (表名: {table_info['table_name']}, 字段: {len(table_info['fields'])})")
            except Exception as e:
                print(f"  ✗ 解析失败: {xml_file} - {e}")

        if not table_infos:
            print("错误: 没有找到有效的表定义")
            return

        # 生成头文件
        header_content = self._generate_header_file(table_infos)

        # 写入文件
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(header_content)

        print(f"\n✓ 成功生成文件: {output_file}")
        print(f"  - 表数量: {len(table_infos)}")
        print(f"  - 总字段数: {sum(len(t['fields']) for t in table_infos)}")


if __name__ == '__main__':
    # 配置路径
    script_dir = os.path.dirname(os.path.abspath(__file__))
    xml_dir = os.path.join(script_dir, '../../../common/db_tables')
    output_file = os.path.join(script_dir, '../../../db_server/sql_builder_traits.h')

    # 转换为绝对路径
    xml_dir = os.path.abspath(xml_dir)
    output_file = os.path.abspath(output_file)

    print("=" * 60)
    print("SQL Builder FieldTraits 生成器")
    print("=" * 60)
    print(f"XML目录: {xml_dir}")
    print(f"输出文件: {output_file}")
    print("")

    # 生成代码
    generator = XMLToSqlBuilderGenerator()
    generator.generate_from_xml_directory(xml_dir, output_file)

    print("")
    print("=" * 60)
    print("生成完成！")
    print("=" * 60)
