import axios from 'axios'
import { ElMessage } from 'element-plus'
import router from '../router'

// 创建 axios 实例
const request = axios.create({
  baseURL: import.meta.env.DEV ? '' : ((import.meta.env.VITE_API_BASE_URL && import.meta.env.VITE_API_BASE_URL.trim()) ? import.meta.env.VITE_API_BASE_URL : ''),
  timeout: 15000
})

// 请求拦截器
request.interceptors.request.use(
  config => {
    // 从 localStorage 获取 token
    const token = localStorage.getItem('token')

    // 如果 token 存在，添加到请求头
    if (token) {
      config.headers.Authorization = `Bearer ${token}`
    }

    return config
  },
  error => {
    console.error('请求错误:', error)
    return Promise.reject(error)
  }
)

// 响应拦截器
request.interceptors.response.use(
  response => {
    return response
  },
  error => {
    // silent 请求由调用方自行处理错误提示（如手动下单页对委托/资金的高频轮询，
    // 避免 500/网络错误的全局 toast 刷屏；401 登录跳转仍保留在下方）
    if (error.config && error.config.silent && error.response?.status !== 401) {
      return Promise.reject(error)
    }
    if (error.response) {
      // 401 未授权，跳转到登录页
      if (error.response.status === 401) {
        localStorage.removeItem('token')

        // 如果当前不在登录页，则跳转到登录页
        if (router.currentRoute.value.path !== '/login') {
          ElMessage.warning('登录已过期，请重新登录')
          router.push('/login')
        }
      } else if (error.response.status === 403) {
        ElMessage.error('没有权限访问')
      } else if (error.response.status === 500) {
        ElMessage.error('服务器错误，请稍后重试')
      }
    } else if (error.request) {
      ElMessage.error('网络错误，请检查网络连接')
    }

    return Promise.reject(error)
  }
)

export default request
