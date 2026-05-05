import subprocess
import os
import time
from git import Repo, InvalidGitRepositoryError

def get_git_branch():
    try:
        repo = Repo(path='.', search_parent_directories=True)  # 自动向上查找.git目录
        return repo.active_branch.name
    except (InvalidGitRepositoryError, TypeError):
        # 当前目录不是git仓库或本地化
        return None
    except Exception as e:  # 其他异常（如没有分支）
        print(f"获取分支出错: {str(e)}")
        return None

def call_from(cwd, cmd):
    start = time.time()
    print(f'\n>>> cwd={cwd}\n>>> cmd={cmd}')
    subprocess.check_call(cmd, cwd=cwd, shell=True)
    print(f'>>> done in {time.time() - start:.3f}s')


if __name__ == '__main__':
    file_path = os.path.abspath(__file__)
    script_dir = os.path.dirname(file_path)
    src_dir = os.path.join(script_dir, os.path.pardir)
    auto_code_dir = os.path.join(script_dir, 'auto_code')
    mysql_gateway_dir = os.path.join(auto_code_dir, 'mysql_gateway')

    call_from(auto_code_dir, 'python xml_to_sql_generator.py')
    call_from(auto_code_dir, 'python xml_to_header_generator.py')

    # call_from(mysql_gateway_dir, 'python xml_to_mysql_gateway_generator.py')
    call_from(mysql_gateway_dir, 'python xml_to_sql_builder_generator.py')

    call_from(auto_code_dir, 'python create_print_func.py')
    call_from(auto_code_dir, 'python dict_generator.py')

    call_from(auto_code_dir, 'generate_proto.bat')

    call_from(auto_code_dir, 'python convert_encoding.py')



