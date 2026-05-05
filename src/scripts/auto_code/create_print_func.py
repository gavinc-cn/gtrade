# coding=utf-8
import os
import CppHeaderParser
from pprint import pprint
from utils.file_utils import lazy_write_file

pwd = os.getcwd()
os.chdir(os.path.dirname(os.path.abspath(__file__)))


class GenDumpFunc:

    def __init__(self):
        self.processed_file_name = set()

    def parse_h_v2(self, file_path, ns=None):
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
        # pprint(cppHeader.enums)
        # pprint(cppHeader.global_enums)
        # pprint(cppHeader.pragmas)
        # pprint(cppHeader.includes)
        # pprint(cppHeader.defines)
        # pprint(cppHeader.variables)
        # pprint(cppHeader.functions)
        ret_map = {}
        for class_nm, class_info in cppHeader.classes.items():
            ret_map[class_nm] = {
                'namespace': ns if ns is not None else class_info['namespace'],
                'inherits': class_info['inherits'],
                'fields': []
            }
            for i in class_info['properties']['public']:
                field = {}
                field['name'] = i['name'] or i['type']
                field['type'] = i['type']
                field['size'] = i.get('array_size', 0) if i['array'] else 1
                field['namespace'] = i['namespace']
                ret_map[class_nm]['fields'].append(field)

        # pprint(cppHeader.global_enums)
        enum_info_map = {}
        for em_name, em_info in cppHeader.global_enums.items():
            enum_info_map[em_name] = em_info['values']
        return ret_map, enum_info_map

    def gen_dump_func(self, class_info_map):
        h_code = ''
        cpp_code = ''
        for name, class_info in class_info_map.items():
            field_lst = class_info['fields']
            namespace = class_info['namespace']
            inherits = class_info['inherits']
            if namespace:
                namespace += '::'
            # print(name, field_lst)
            # print('inherits:', inherits)
            h_code += f"""
std::ostream& operator<<(std::ostream& os, const {namespace}{name}& st);"""
            cpp_code += f"""
std::ostream& operator<<(std::ostream& os, const {namespace}{name}& st)
{{
    os << "{{" """
            for child_info in inherits:
                cpp_code += f"""
    << dynamic_cast<const {namespace}{child_info['class']}&>(st) << "," """
            for i, d in enumerate(field_lst):
                if str(d['size']) not in ['0', '1'] and d['type'] != 'char':
                    cpp_code += f"""
    << "{d['name']}:[";
    for (auto i=std::begin(st.{d['name']}); i!=std::end(st.{d['name']}); i++)
    {{
        decltype(*i) empty{{}};
        if (memcmp(i, &empty, sizeof(*i)) == 0) continue;
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
        return h_code, cpp_code

    def gen_enum_func(self, enum_info_map):
        h_code = ''
        cpp_code = ''
        for name, enum_info in enum_info_map.items():
            h_code += f"""
std::string GetEmName_{name}(int k);"""
            cpp_code += f"""
std::string GetEmName_{name}(int k) {{
    static const std::unordered_map<int,std::string> tmp_map = {{"""
            for pair in enum_info:
                cpp_code += f"""
            {{{name}::{pair['name']}, "{pair['name']}"}},"""
            cpp_code += f"""
    }};
    auto iter = tmp_map.find(k);
    if (iter != tmp_map.end()) {{
        return iter->second;
    }} else {{
        return {{}};
    }}
}}
"""
            h_code += f"""
{name} GetEmVal_{name}(const std::string& k);"""
            cpp_code += f"""
{name} GetEmVal_{name}(const std::string& k) {{
    static const std::unordered_map<std::string,{name}> tmp_map = {{"""
            for pair in enum_info:
                cpp_code += f"""
            {{"{pair['name']}", {name}::{pair['name']}}},"""
            cpp_code += f"""
    }};
    auto iter = tmp_map.find(k);
    if (iter != tmp_map.end()) {{
        return iter->second;
    }} else {{
        return {{}};
    }}
}}
"""
            h_code += f"""
std::ostream& operator<<(std::ostream& os, const {name}& st);"""
            cpp_code += f"""
std::ostream& operator<<(std::ostream& os, const {name}& st)
{{
    os << GetEmName_{name}(st);
    return os;
}}
"""
        return h_code, cpp_code

    def write_dump_file(self, h_code, cpp_code, file_path, dst_dir=""):
        dir_name = os.path.dirname(file_path)
        base_name = os.path.basename(file_path)
        core_name = os.path.splitext(base_name)[0]
        if not dst_dir:
            dst_dir = dir_name
        dst_h_file_path = os.path.join(dst_dir, f'{core_name}_dump.h')
        dst_cpp_file_path = os.path.join(dst_dir, f'{core_name}_dump.cpp')
        lazy_write_file(dst_h_file_path, h_code)
        lazy_write_file(dst_cpp_file_path, cpp_code)

    def gen_dump_file(self, file_path, ns=None):
        print(f'{"="*10} {file_path} {"="*10}')
        base_name = os.path.basename(file_path)
        core_name = os.path.splitext(base_name)[0]
        if core_name in self.processed_file_name:
            raise RuntimeError(f"{core_name} is duplicated")
        self.processed_file_name.add(core_name)
        h_code = f"""
#pragma once
#include "{base_name}"
#ifdef __linux__
#include <iostream>
#include <unordered_map>
/*
* note: 本文件由脚本自动生成
*/
"""
        cpp_code = f"""
#ifdef __linux__
#include <cstring>
#include "{core_name}_dump.h"
/*
* note: 本文件由脚本自动生成
*/
"""
        class_info_map, enum_info_map = self.parse_h_v2(file_path, ns=ns)
        # pprint(class_info_map)
        # pprint(enum_info_map)

        tmp = self.gen_dump_func(class_info_map)
        h_code += tmp[0]
        cpp_code += tmp[1]

        tmp = self.gen_enum_func(enum_info_map)
        h_code += tmp[0]
        cpp_code += tmp[1]

        h_code += "\n#endif // __linux__"
        cpp_code += "\n#endif // __linux__"

        # print(h_code)
        # print(cpp_code)
        self.write_dump_file(h_code, cpp_code, file_path)

    def run(self):
        self.gen_dump_file("../../common/msg_id.h")
        self.gen_dump_file("../../interface/i_exchange_data.h")
        self.gen_dump_file("../../interface/i_strategy_engine.h")
        self.gen_dump_file("../../interface/type_define.h")
        self.gen_dump_file("../../interface/i_client.h")
        self.gen_dump_file("../../interface/i_timer_manager.h")
        self.gen_dump_file("../../interface/db_structures.h")


if __name__ == '__main__':
    GenDumpFunc().run()

    os.chdir(pwd)
    print('done')