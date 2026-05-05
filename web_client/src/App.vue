<template>
  <div id="app">
    <!-- 登录页面不显示侧边栏和导航栏 -->
    <router-view v-if="isLoginPage" />

    <!-- 其他页面显示完整布局 -->
    <el-container v-else>
      <el-aside width="200px" class="sidebar">
        <div class="logo">
          <h2>GTrade</h2>
        </div>
        <el-menu
          :default-active="$route.path"
          class="sidebar-menu"
          router
          background-color="#1e1e1e"
          text-color="#b0b0b0"
          active-text-color="#409eff"
        >
          <el-menu-item index="/strategy">
            <el-icon><Histogram /></el-icon>
            <span>策略管理</span>
          </el-menu-item>
          <el-menu-item index="/orders">
            <el-icon><Document /></el-icon>
            <span>委托管理</span>
          </el-menu-item>
          <el-menu-item index="/trades">
            <el-icon><TrendCharts /></el-icon>
            <span>成交管理</span>
          </el-menu-item>
          <el-menu-item index="/positions">
            <el-icon><Box /></el-icon>
            <span>持仓管理</span>
          </el-menu-item>
          <el-menu-item index="/portfolio-positions">
            <el-icon><Grid /></el-icon>
            <span>组合持仓管理</span>
          </el-menu-item>
          <el-menu-item index="/balances">
            <el-icon><Coin /></el-icon>
            <span>资金管理</span>
          </el-menu-item>
        </el-menu>
      </el-aside>
      <el-container>
        <el-header>
          <div class="header-content">
            <span class="header-title">{{ currentPageTitle }}</span>
            <div class="header-right">
              <span class="header-time">{{ currentTime }}</span>
              <el-button type="danger" size="small" @click="handleLogout" class="logout-btn">
                <el-icon><SwitchButton /></el-icon>
                <span>退出登录</span>
              </el-button>
            </div>
          </div>
        </el-header>
        <el-main>
          <router-view />
        </el-main>
      </el-container>
    </el-container>
  </div>
</template>

<script>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessageBox, ElMessage } from 'element-plus'

export default {
  name: 'App',
  setup() {
    const route = useRoute()
    const router = useRouter()
    const currentTime = ref('')
    let timer = null

    const currentPageTitle = computed(() => {
      return route.meta?.title || 'GTrade'
    })

    const isLoginPage = computed(() => {
      return route.path === '/login'
    })

    const updateTime = () => {
      const now = new Date()
      currentTime.value = now.toLocaleString('zh-CN', {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hour12: false
      })
    }

    const handleLogout = async () => {
      try {
        await ElMessageBox.confirm('确定要退出登录吗？', '提示', {
          confirmButtonText: '确定',
          cancelButtonText: '取消',
          type: 'warning'
        })

        // 清除 token
        localStorage.removeItem('token')

        ElMessage.success('退出登录成功')

        // 跳转到登录页
        setTimeout(() => {
          router.push('/login')
        }, 500)
      } catch (error) {
        // 用户取消操作
      }
    }

    onMounted(() => {
      updateTime()
      timer = setInterval(updateTime, 1000)
    })

    onUnmounted(() => {
      if (timer) {
        clearInterval(timer)
      }
    })

    return {
      currentTime,
      currentPageTitle,
      isLoginPage,
      handleLogout
    }
  }
}
</script>

<style>
/* 全局深色主题 */
body {
  margin: 0;
  padding: 0;
  background-color: #2c2c2c;
}

#app {
  font-family: Avenir, Helvetica, Arial, sans-serif;
  -webkit-font-smoothing: antialiased;
  -moz-osx-font-smoothing: grayscale;
  height: 100vh;
  background-color: #2c2c2c;
  color: #e0e0e0;
}

.el-container {
  background-color: #2c2c2c;
  height: 100vh;
}

.sidebar {
  background-color: #1e1e1e;
  height: 100vh;
  overflow-y: auto;
  box-shadow: 2px 0 8px rgba(0, 0, 0, 0.3);
}

.logo {
  padding: 20px;
  text-align: center;
  border-bottom: 1px solid #404040;
}

.logo h2 {
  margin: 0;
  color: #409eff;
  font-size: 24px;
  font-weight: bold;
}

.sidebar-menu {
  border-right: none;
}

.el-menu-item {
  height: 56px;
  line-height: 56px;
}

.el-menu-item:hover {
  background-color: #2a2a2a !important;
}

.el-menu-item.is-active {
  background-color: #263445 !important;
}

.el-header {
  background-color: #1e1e1e;
  color: #fff;
  line-height: 60px;
  font-size: 16px;
  padding: 0 20px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.3);
  display: flex;
  align-items: center;
}

.header-content {
  display: flex;
  justify-content: space-between;
  align-items: center;
  width: 100%;
}

.header-title {
  font-size: 20px;
  font-weight: bold;
  color: #e0e0e0;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 20px;
}

.header-time {
  font-size: 14px;
  color: #909399;
}

.logout-btn {
  margin-left: 10px;
}

.el-main {
  padding: 0;
  background-color: #2c2c2c;
  overflow-y: auto;
}

/* Element Plus 深色主题覆盖 */
.el-tabs {
  --el-bg-color: #2c2c2c;
  --el-text-color-primary: #e0e0e0;
  --el-border-color: #404040;
}

.el-tabs__header {
  background-color: #2c2c2c;
}

.el-tabs__item {
  color: #b0b0b0;
}

.el-tabs__item.is-active {
  color: #409eff;
}

.el-table {
  --el-table-bg-color: #353535;
  --el-table-tr-bg-color: #353535;
  --el-table-row-hover-bg-color: #404040;
  --el-table-header-bg-color: #2a2a2a;
  --el-table-header-text-color: #e0e0e0;
  --el-table-text-color: #d0d0d0;
  --el-table-border-color: #404040;
}

.el-table th {
  background-color: #2a2a2a !important;
  color: #e0e0e0 !important;
}

.el-table td {
  background-color: #353535 !important;
  border-color: #404040 !important;
}

.el-table--striped .el-table__body tr.el-table__row--striped td {
  background-color: #3a3a3a !important;
}

.el-table__body tr:hover > td {
  background-color: #404040 !important;
}

.el-button {
  --el-button-bg-color: #404040;
  --el-button-border-color: #555555;
  --el-button-text-color: #e0e0e0;
  --el-button-hover-bg-color: #4a4a4a;
  --el-button-hover-border-color: #666666;
  --el-button-active-bg-color: #505050;
}

.el-button--primary {
  --el-button-bg-color: #1e5080;
  --el-button-border-color: #1e5080;
  --el-button-hover-bg-color: #26628f;
  --el-button-active-bg-color: #18406a;
}

.el-button--success {
  --el-button-bg-color: #356b1e;
  --el-button-border-color: #356b1e;
  --el-button-hover-bg-color: #3f7a24;
  --el-button-active-bg-color: #2a5518;
}

.el-button--warning {
  --el-button-bg-color: #8a6322;
  --el-button-border-color: #8a6322;
  --el-button-hover-bg-color: #9d7028;
  --el-button-active-bg-color: #73521c;
}

.el-button--danger {
  --el-button-bg-color: #943333;
  --el-button-border-color: #943333;
  --el-button-hover-bg-color: #a53d3d;
  --el-button-active-bg-color: #7a2929;
}

.el-tag {
  --el-tag-bg-color: #404040;
  --el-tag-border-color: #555555;
  --el-tag-text-color: #e0e0e0;
}

.el-tag--success {
  --el-tag-bg-color: rgba(53, 107, 30, 0.3);
  --el-tag-border-color: #3f7a24;
  --el-tag-text-color: #5a9a35;
}

.el-tag--info {
  --el-tag-bg-color: rgba(64, 64, 64, 0.3);
  --el-tag-border-color: #606060;
  --el-tag-text-color: #909090;
}

.el-dropdown-menu {
  background-color: #353535;
  border-color: #404040;
}

.el-dropdown-menu__item {
  color: #d0d0d0;
}

.el-dropdown-menu__item:hover {
  background-color: #404040;
  color: #409eff;
}

.el-message-box {
  background-color: #353535;
  border-color: #404040;
}

.el-message-box__title {
  color: #e0e0e0;
}

.el-message-box__content {
  color: #d0d0d0;
}
</style>
