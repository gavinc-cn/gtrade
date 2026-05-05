import subprocess

def get_git_branch():
    try:
        # 获取当前分支名称
        result = subprocess.run(
            ['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError:
        return None

# 获取当前日期作为版本号
version = get_git_branch()

# 获取镜像ID
image_id = subprocess.check_output(f'docker images -q gtrade:{version}', shell=True).decode().strip()
if not image_id:
    print(f'错误：未找到gtrade:{version}镜像')
    exit(1)

# 标记镜像
print(f'标记镜像 {image_id} 为 registry.cn-hongkong.aliyuncs.com/orca123456/gtrade:{version}')
subprocess.check_call(f'docker tag {image_id} registry.cn-hongkong.aliyuncs.com/orca123456/gtrade:{version}', shell=True)

# 推送镜像到阿里云
print(f'推送镜像到阿里云...')
subprocess.check_call(f'docker push registry.cn-hongkong.aliyuncs.com/orca123456/gtrade:{version}', shell=True)

print(f'镜像已成功推送到阿里云，版本号：{version}')
