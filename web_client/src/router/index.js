import { createRouter, createWebHistory } from 'vue-router'
import StrategyManager from '../components/StrategyManager.vue'
import OrderManager from '../views/OrderManager.vue'
import TradeManager from '../views/TradeManager.vue'
import PositionManager from '../views/PositionManager.vue'
import PortfolioPositionManager from '../views/PortfolioPositionManager.vue'
import BalanceManager from '../views/BalanceManager.vue'
import SettingsManager from '../views/SettingsManager.vue'
import ManualOrderManager from '../views/ManualOrderManager.vue'
import LoginSimple from '../views/LoginSimple.vue'

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: LoginSimple,
    meta: { title: '登录', requiresAuth: false }
  },
  {
    // 默认落地页 = 手动下单（登录后 router.push('/') 即进入该页）
    path: '/',
    redirect: '/manual-orders'
  },
  {
    path: '/strategy',
    name: 'StrategyManager',
    component: StrategyManager,
    meta: { title: '策略管理', requiresAuth: true }
  },
  {
    path: '/orders',
    name: 'OrderManager',
    component: OrderManager,
    meta: { title: '委托管理', requiresAuth: true }
  },
  {
    path: '/manual-orders',
    name: 'ManualOrderManager',
    component: ManualOrderManager,
    meta: { title: '手动下单', requiresAuth: true }
  },
  {
    path: '/trades',
    name: 'TradeManager',
    component: TradeManager,
    meta: { title: '成交管理', requiresAuth: true }
  },
  {
    path: '/positions',
    name: 'PositionManager',
    component: PositionManager,
    meta: { title: '持仓管理', requiresAuth: true }
  },
  {
    path: '/portfolio-positions',
    name: 'PortfolioPositionManager',
    component: PortfolioPositionManager,
    meta: { title: '组合持仓管理', requiresAuth: true }
  },
  {
    path: '/balances',
    name: 'BalanceManager',
    component: BalanceManager,
    meta: { title: '资金管理', requiresAuth: true }
  },
  {
    path: '/settings',
    name: 'Settings',
    component: SettingsManager,
    meta: { title: '设置', requiresAuth: true }
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

// 路由守卫
router.beforeEach((to, from, next) => {
  const token = localStorage.getItem('token')

  // 如果路由需要认证
  if (to.meta.requiresAuth !== false) {
    if (!token) {
      // 未登录，跳转到路由登录页
      next('/login')
      return
    } else {
      // 已登录，允许访问
      next()
    }
  } else {
    // 不需要认证的路由，直接允许访问
    // 如果已登录且访问登录页，跳转到首页
    if (to.path === '/login' && token) {
      next('/')
    } else {
      next()
    }
  }
})

export default router
