# coding=utf-8
import os
import CppHeaderParser
from pprint import pprint


class GenDumpFunc:
    def parse_h_v2(self, file_path, ns=''):
        try:
            encoding = 'utf8'
            with open(file_path, 'r', encoding=encoding) as fin:
                content = fin.read()
        except UnicodeDecodeError as e:
            encoding = 'gbk'
            with open(file_path, 'r', encoding=encoding) as fin:
                content = fin.read()
        cppHeader = CppHeaderParser.CppHeader(content, argType='string')
        # pprint(cppHeader.classes)
        ret_map = {}

        for class_nm, class_info in cppHeader.classes.items():
            ret_map[class_nm] = {
                'namespace': ns or class_info['namespace'],
                'inherits': class_info['inherits'],
                'fields': []
            }
            for i in class_info['properties']['public']:
                field = {}
                field['name'] = i['name']
                field['type'] = i['type']
                field['size'] = i['array_size'] if i['array'] else 1
                field['namespace'] = i['namespace']
                ret_map[class_nm]['fields'].append(field)
        return ret_map

    def gen_dump_func(self, file_path, dst_dir="", ns=""):
        dir_name = os.path.dirname(file_path)
        base_name = os.path.basename(file_path)
        core_name = os.path.splitext(base_name)[0]
        struct_map = self.parse_h_v2(file_path, ns=ns)
        cpp_code = f"""\n
#pragma once
#include "{base_name}"
#include <iostream>
// note：本文件由脚本自动生成
"""

        for name, class_info in struct_map.items():
            field_lst = class_info['fields']
            namespace = class_info['namespace']
            inherits = class_info['inherits']
            if namespace:
                namespace += '::'
            # print(name, field_lst)
            # print('inherits:', inherits)
            cpp_code += f"""
static std::ostream& operator<<(std::ostream& os, const {namespace}{name}& st) 
{{
    os << "{{" """
            for child_info in inherits:
                cpp_code += f"""
    << dynamic_cast<const {namespace}{child_info['class']}&>(st) << "," """
            for i, d in enumerate(field_lst):
                if str(d['size']) not in ['0', '1'] and d['type'] != 'char':
                    cpp_code += f"""
    << {d['name']}:[";
    for (auto i=std::begin(st.{d['name']}); i!=std::end(st.{d['name']}); ++i)
    {{
        decltype(*i) empty = *i;
        if (memcmp(&empty, &empty, sizeof(*i)) == 0) continue;
        os << *i;
        if (std::next(i) != std::end(st.{d['name']}))
        {{
            os << ", ";
        }}
    }}
    os << "]" """
                else:
                    cpp_code += f"""
    << "{d['name']}:" << st.{d['name']} """
                if i != len(field_lst) - 1:
                    cpp_code += '<< ","'
            cpp_code += f"""
    << "}}";
    return os;
}}
"""
        cpp_code += f"""
"""
        if not dst_dir:
            dst_dir = dir_name
        dst_file_path = os.path.join(dst_dir, f'{core_name}_dump.h')
        with open(dst_file_path, 'w', encoding='utf8', newline='\n') as fout:
            fout.write(cpp_code)


if __name__ == '__main__':
    pwd = os.getcwd()
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    gdf = GenDumpFunc()
    gdf.gen_dump_func("interface.h")
    os.chdir(pwd)
    print('done')
