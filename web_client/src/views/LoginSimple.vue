<template>
  <div class="login-page">
    <div class="login-card">
      <h1 class="title">GTrade</h1>
      <p class="subtitle">量化交易系统</p>

      <form @submit.prevent="handleLogin" class="form">
        <div class="form-group">
          <label for="username">用户名</label>
          <input
            id="username"
            v-model="username"
            type="text"
            placeholder="请输入用户名"
            autocomplete="username"
            class="input"
          />
        </div>

        <div class="form-group">
          <label for="password">密码</label>
          <input
            id="password"
            v-model="password"
            type="password"
            placeholder="请输入密码"
            autocomplete="current-password"
            class="input"
            @keyup.enter="handleLogin"
          />
        </div>

        <button
          type="submit"
          class="btn"
          :disabled="loading"
        >
          {{ loading ? '登录中...' : '登录' }}
        </button>

        <div v-if="error" class="error">{{ error }}</div>
        <div v-if="success" class="success">{{ success }}</div>
      </form>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import request from '@/utils/request'

const router = useRouter()
const username = ref('')
const password = ref('')
const loading = ref(false)
const error = ref('')
const success = ref('')

const handleLogin = async () => {
  error.value = ''
  success.value = ''

  if (!username.value || !password.value) {
    error.value = '请输入用户名和密码'
    return
  }

  loading.value = true

  try {
    const response = await request.post('/api/login', {
      username: username.value,
      password: password.value
    })

    if (response.data.success) {
      localStorage.setItem('token', response.data.token)
      success.value = '登录成功！跳转中...'
      setTimeout(() => {
        router.push('/')
      }, 500)
    } else {
      error.value = response.data.message || '登录失败'
    }
  } catch (err) {
    console.error('登录错误:', err)
    if (err.response?.data?.message) {
      error.value = err.response.data.message
    } else if (err.message) {
      error.value = `网络错误: ${err.message}`
    } else {
      error.value = '登录失败，请检查网络连接'
    }
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
* {
  box-sizing: border-box;
}

.login-page {
  position: fixed;
  top: 0;
  left: 0;
  width: 100vw;
  height: 100vh;
  display: flex;
  justify-content: center;
  align-items: center;
  background: linear-gradient(135deg, #1e1e1e 0%, #2c2c2c 100%);
  margin: 0;
  padding: 20px;
}

.login-card {
  width: 100%;
  max-width: 420px;
  padding: 40px;
  background: #353535;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.6);
}

.title {
  margin: 0 0 10px 0;
  font-size: 38px;
  font-weight: bold;
  color: #409eff;
  text-align: center;
  letter-spacing: 2px;
}

.subtitle {
  margin: 0 0 30px 0;
  font-size: 15px;
  color: #909399;
  text-align: center;
  letter-spacing: 1px;
}

.form {
  width: 100%;
}

.form-group {
  margin-bottom: 20px;
}

.form-group label {
  display: block;
  margin-bottom: 8px;
  color: #b0b0b0;
  font-size: 14px;
  font-weight: 500;
}

.input {
  width: 100%;
  padding: 12px 16px;
  font-size: 16px;
  color: #e0e0e0;
  background: #2a2a2a;
  border: 1px solid #4a4a4a;
  border-radius: 6px;
  outline: none;
  transition: all 0.3s;
  font-family: inherit;
}

.input::placeholder {
  color: #808080;
}

.input:hover {
  border-color: #606060;
  background: #2e2e2e;
}

.input:focus {
  border-color: #409eff;
  border-width: 2px;
  background: #2e2e2e;
  padding: 11px 15px; /* 补偿边框增加 */
}

.btn {
  width: 100%;
  padding: 14px;
  margin-top: 10px;
  font-size: 16px;
  font-weight: 600;
  color: #fff;
  background: #409eff;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.3s;
  font-family: inherit;
}

.btn:hover:not(:disabled) {
  background: #66b1ff;
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(64, 158, 255, 0.4);
}

.btn:active:not(:disabled) {
  transform: translateY(0);
}

.btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.error {
  margin-top: 15px;
  padding: 12px;
  background: rgba(245, 108, 108, 0.1);
  border: 1px solid #f56c6c;
  border-radius: 6px;
  color: #f56c6c;
  font-size: 14px;
  text-align: center;
}

.success {
  margin-top: 15px;
  padding: 12px;
  background: rgba(103, 194, 58, 0.1);
  border: 1px solid #67c23a;
  border-radius: 6px;
  color: #67c23a;
  font-size: 14px;
  text-align: center;
}
</style>
