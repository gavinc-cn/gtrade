<template>
  <div class="login-container">
    <div class="login-box">
      <div class="login-header">
        <h1>GTrade</h1>
        <p>量化交易系统</p>
      </div>
      <el-form
        ref="loginFormRef"
        :model="loginForm"
        :rules="loginRules"
        class="login-form"
        label-position="top"
      >
        <el-form-item label="用户名" prop="username">
          <el-input
            v-model="loginForm.username"
            placeholder="请输入用户名"
            size="large"
            clearable
            autocomplete="username"
            tabindex="1"
          >
            <template #prefix>
              <el-icon><User /></el-icon>
            </template>
          </el-input>
        </el-form-item>
        <el-form-item label="密码" prop="password">
          <el-input
            v-model="loginForm.password"
            type="password"
            placeholder="请输入密码"
            size="large"
            show-password
            autocomplete="current-password"
            tabindex="2"
            @keyup.enter="handleLogin"
          >
            <template #prefix>
              <el-icon><Lock /></el-icon>
            </template>
          </el-input>
        </el-form-item>
        <el-form-item>
          <el-button
            type="primary"
            size="large"
            :loading="loading"
            class="login-button"
            tabindex="3"
            @click="handleLogin"
          >
            {{ loading ? '登录中...' : '登录' }}
          </el-button>
        </el-form-item>
      </el-form>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { User, Lock } from '@element-plus/icons-vue'
import request from '@/utils/request'

const router = useRouter()
const loginFormRef = ref(null)
const loading = ref(false)

const loginForm = reactive({
  username: '',
  password: ''
})

const loginRules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' }
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' }
  ]
}

const handleLogin = async () => {
  if (!loginFormRef.value) return

  try {
    await loginFormRef.value.validate()
    loading.value = true

    const response = await request.post('/api/login', {
      username: loginForm.username,
      password: loginForm.password
    })

    if (response.data.success) {
      // 保存 token 到 localStorage
      localStorage.setItem('token', response.data.token)

      ElMessage.success(response.data.message || '登录成功')

      // 跳转到主页
      router.push('/')
    } else {
      ElMessage.error(response.data.message || '登录失败')
    }
  } catch (error) {
    console.error('登录失败:', error)
    if (error.response?.data?.message) {
      ElMessage.error(error.response.data.message)
    } else {
      ElMessage.error('登录失败，请检查网络连接')
    }
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-container {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  display: flex;
  justify-content: center;
  align-items: center;
  background: linear-gradient(135deg, #1e1e1e 0%, #2c2c2c 100%);
  z-index: 1;
}

.login-box {
  width: 420px;
  padding: 40px;
  background-color: #353535;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.6);
  position: relative;
  z-index: 10;
}

.login-header {
  text-align: center;
  margin-bottom: 35px;
}

.login-header h1 {
  margin: 0;
  font-size: 38px;
  font-weight: bold;
  color: #409eff;
  letter-spacing: 2px;
}

.login-header p {
  margin: 12px 0 0;
  font-size: 15px;
  color: #909399;
  letter-spacing: 1px;
}

.login-form {
  margin-top: 25px;
}

.login-button {
  width: 100%;
  margin-top: 15px;
  height: 45px;
  font-size: 16px;
  font-weight: 600;
}

/* 表单标签样式 */
:deep(.el-form-item__label) {
  color: #b0b0b0;
  font-weight: 500;
  margin-bottom: 8px;
}

/* Element Plus 输入框深色主题 */
:deep(.el-input) {
  position: relative;
  z-index: 100;
}

:deep(.el-input__wrapper) {
  background-color: #2a2a2a;
  box-shadow: 0 0 0 1px #4a4a4a inset;
  cursor: text !important;
  pointer-events: auto !important;
  transition: all 0.3s;
  padding: 8px 15px;
  position: relative;
  z-index: 100;
}

:deep(.el-input__wrapper:hover) {
  box-shadow: 0 0 0 1px #606060 inset;
  background-color: #2e2e2e;
}

:deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 2px #409eff inset !important;
  background-color: #2e2e2e;
}

:deep(.el-input__inner) {
  color: #e0e0e0;
  cursor: text !important;
  pointer-events: auto !important;
  background: transparent;
  position: relative;
  z-index: 101;
}

:deep(.el-input__inner::placeholder) {
  color: #808080;
}

:deep(.el-input__prefix) {
  color: #909399;
}

:deep(.el-input__suffix) {
  color: #909399;
}

/* 确保表单项完全可交互 */
:deep(.el-form-item) {
  pointer-events: auto !important;
  position: relative;
  z-index: 100;
  margin-bottom: 22px;
}

:deep(.el-form-item__content) {
  position: relative;
  z-index: 100;
}

/* 清除按钮样式 */
:deep(.el-input__clear) {
  color: #909399;
}

:deep(.el-input__clear:hover) {
  color: #409eff;
}

/* 密码显示按钮 */
:deep(.el-input__password) {
  color: #909399;
}

:deep(.el-input__password:hover) {
  color: #409eff;
}
</style>
