# coding=utf-8
import os
import CppHeaderParser
from pprint import pprint


class MockCreator:
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
                'fields': [],
                'methods': [],
            }
            for i in class_info['properties']['public']:
                field = {}
                field['name'] = i['name']
                field['type'] = i['type']
                field['size'] = i['array_size'] if i['array'] else 1
                field['namespace'] = i['namespace']
                ret_map[class_nm]['fields'].append(field)

            for i in class_info['methods']['public']:
                method = {
                    'name': i['name'],
                    'rtnType': i['rtnType'],
                    'parameters': [],
                    'suffix': [],
                }
                if i['const']:
                    method['suffix'].append('const')
                if i['override'] or i['virtual']:
                    method['suffix'].append('override')
                if i['final']:
                    method['suffix'].append('final')

                for arg in i['parameters']:
                    arg_info = {
                        'name': arg['name'],
                        'type': arg['type'],
                        'size': arg['array_size'] if arg['array'] else 1,
                        'namespace': arg.get('namespace', ''),
                    }
                    method['parameters'].append(arg_info)

                ret_map[class_nm]['methods'].append(method)
        return ret_map

    def create_mock(self, dir_name, base_name, dst_dir="", ns=""):
        file_path = os.path.join(dir_name, base_name)
        core_name = os.path.splitext(os.path.basename(base_name))[0]
        struct_map = self.parse_h_v2(file_path, ns=ns)
        cpp_code = f"""\n
#pragma once
#include <gmock/gmock.h>
#include <iostream>
#include "{base_name}"
// note：本文件由脚本自动生成
"""

        for name, class_info in struct_map.items():
            field_lst = class_info['fields']
            method_lst = class_info['methods']
            namespace = class_info['namespace']
            inherits = class_info['inherits']
            if namespace:
                namespace += '::'
            # print(name, field_lst)
            # print('inherits:', inherits)
            cpp_code += f"""
class Mock{name} : public {name} {{
public:"""
            # for child_info in inherits:
            #     cpp_code += f"{child_info['class']}"
            #     if child_info != inherits[-1]:
            #         cpp_code += ','
            # cpp_code += '), ('

            # for i, d in enumerate(field_lst):
            #     cpp_code += f"{d['name']}"
            #     if i != len(field_lst) - 1:
            #         cpp_code += ','
            # cpp_code += '));'

            # MOCK_METHOD(void, Forward, (int distance), (override));
            for method_idx, method_info in enumerate(method_lst):
                arg_lst = method_info['parameters']
                cpp_code += f"""
    MOCK_METHOD({method_info['rtnType']}, {method_info['name']}, ("""
                for arg_idx, arg_info in enumerate(arg_lst):
                    ns_prefix = f"{arg_info['namespace']}::" if arg_info['namespace'] else ''
                    cpp_code += f"{arg_info['type']} {ns_prefix}{arg_info['name']}"
                    if arg_idx != len(arg_lst) - 1:
                        cpp_code += ', '
                cpp_code += f"), ({','.join(method_info['suffix'])}));"
            cpp_code += """
};
"""
        # print(cpp_code)

        if not dst_dir:
            dst_dir = dir_name
        dst_file_path = os.path.join(dst_dir, f'{core_name}_mock.h')
        with open(dst_file_path, 'w', encoding='utf8', newline='\n') as fout:
            fout.write(cpp_code)


if __name__ == '__main__':
    pwd = os.getcwd()
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    mock_creator = MockCreator()
    mock_creator.create_mock("turtle.h")
    os.chdir(pwd)
    print('done')
