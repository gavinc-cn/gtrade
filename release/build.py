import os
import shutil
import subprocess
import psutil
from datetime import datetime

DEV = 'dev'
PROD = 'prod'

RELEASE = 'Release'
DEBUG = 'Debug'


def rm_path(path):
    if os.path.isdir(path):
        shutil.rmtree(path)
    elif os.path.isfile(path):
        os.remove(path)


class Builder:

    def __init__(self):
        self.time_str = datetime.now().strftime('%Y%m%d_%H%M%S')
        cur_file_path = os.path.abspath(__file__)
        self.cur_dir = os.path.dirname(cur_file_path)
        self.project_dir = os.path.abspath(os.path.join(self.cur_dir, os.pardir))
        self.src_dir = os.path.join(self.project_dir, 'src')
        self.scripts_dir = os.path.join(self.src_dir, 'scripts')
        self.build_dir = os.path.join(self.project_dir, 'build')
        self.output_dir = os.path.join(self.build_dir, 'output')

    def exec_cmd(self, cmd):
        print(f'[exec] "{cmd}" from {os.getcwd()}')
        # env = os.environ.copy()
        # env['PATH'] = ''
        subprocess.check_call(cmd, shell=True)

    def build_common(self, mode):
        if not args.dirty:
            shutil.rmtree(self.build_dir, ignore_errors=True)
            print(f'delete {self.build_dir}')
        os.makedirs(self.build_dir, exist_ok=True)

        os.chdir(self.build_dir)
        if mode == PROD:
            self.exec_cmd(f'cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release')
        elif mode == DEV:
            self.exec_cmd(f'cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release')
        else:
            raise RuntimeError(f'invalid mode={mode}')
        rm_path(self.output_dir)
        # os.makedirs(self.output_dir)
        self.exec_cmd(f'cmake --build . -t install -j{args.cpu}')

    def copy_config(self, release_dir, mode):
        src_dir = os.path.join(self.project_dir, 'deploy', f'{mode}_config')
        for i in os.listdir(src_dir):
            path = os.path.join(src_dir, i)
            if os.path.isdir(path):
                shutil.copytree(path, os.path.join(release_dir, i))
            elif os.path.isfile(path):
                shutil.copy2(path, os.path.join(release_dir, i))

    def copy_exe(self, src, dst):
        shutil.copy2(src, dst)
        os.chmod(dst, 0o755)

    def copy_run_sh(self, release_dir):
        self.copy_exe(os.path.join(self.cur_dir, 'start.sh'), os.path.join(release_dir, 'start.sh'))
        self.copy_exe(os.path.join(self.cur_dir, 'stop.sh'), os.path.join(release_dir, 'stop.sh'))
        self.copy_exe(os.path.join(self.cur_dir, 'status.sh'), os.path.join(release_dir, 'status.sh'))
        self.copy_exe(os.path.join(self.cur_dir, 'restart.sh'), os.path.join(release_dir, 'restart.sh'))

    def copy_scripts(self, release_dir):
        for i in [
            'clients',
            'common',
            'data_recorder',
            'utils',
            'csv2clickhouse.py',
            'data_config.yml',
            'data_recorder.py',
        ]:
            src_path = os.path.join(self.scripts_dir, i)
            dst_path = os.path.join(release_dir, i)
            if os.path.isdir(src_path):
                shutil.copytree(src_path, dst_path)
            elif os.path.isfile(src_path):
                shutil.copy2(src_path, dst_path)

    def build_release(self, mode):
        print(f'== BUILD {mode.upper()} ==')

        self.copy_config(self.output_dir, mode)
        self.copy_run_sh(self.output_dir)
        self.copy_scripts(self.output_dir)

        release_dir_name = args.rel_name if args.rel_name else f'GTRADE_{mode.upper()}_{self.time_str}'
        release_dir = os.path.join(self.build_dir, release_dir_name)
        rm_path(release_dir)
        shutil.copytree(self.output_dir, release_dir, symlinks=True)

        release_zip_name = f'{release_dir_name}.zip'
        rm_path(os.path.join(self.build_dir, release_zip_name))
        if not args.no_zip:
            self.exec_cmd(f'cd {self.build_dir} && zip -ry {release_zip_name} {release_dir_name}')

    def run(self, mode):
        self.build_common(mode)
        self.build_release(mode)



if __name__ == '__main__':
    pwd = os.getcwd()
    if psutil.WINDOWS:
        raise SystemError("windows not supported")

    import argparse
    class CommandLineArgs:
        def __init__(self, cmd_args):
            self.mode = cmd_args.mode
            self.cpu = cmd_args.cpu
            self.build_type = cmd_args.build_type
            self.no_zip = cmd_args.no_zip
            self.dirty = cmd_args.dirty
            self.rel_name = cmd_args.rel_name
    parser = argparse.ArgumentParser(description='gtrade builder', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--mode', choices=[DEV, PROD], default=DEV)
    parser.add_argument('--cpu', type=int, default=max(os.cpu_count() - 1, 1))
    parser.add_argument('--build-type', choices=[RELEASE, DEBUG], default=RELEASE)
    parser.add_argument('--no-zip', action='store_const', const=True, default=False)
    parser.add_argument('--dirty', action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument('--rel-name', help='release folder name')
    args = CommandLineArgs(parser.parse_args())

    Builder().run(args.mode)

    os.chdir(pwd)
    print('done')