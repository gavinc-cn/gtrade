<template>
  <div class="order-manager">
    <el-card class="filter-card">
      <template #header>
        <div class="card-header">
          <span>委托查询</span>
          <el-button type="primary" size="small" @click="fetchOrders">
            <el-icon><Refresh /></el-icon>
            刷新
          </el-button>
        </div>
      </template>

      <el-form :inline="true" :model="filterForm" class="filter-form">
        <el-form-item label="策略名称">
          <el-input v-model="filterForm.strategy" placeholder="请输入策略名称" clearable />
        </el-form-item>
        <el-form-item label="交易对">
          <el-input v-model="filterForm.symbol" placeholder="请输入交易对" clearable />
        </el-form-item>
        <el-form-item label="委托状态">
          <el-select v-model="filterForm.status" placeholder="请选择状态" clearable>
            <el-option label="全部" value="" />
            <el-option label="已提交" value="submitted" />
            <el-option label="部分成交" value="partially_filled" />
            <el-option label="已成交" value="filled" />
            <el-option label="已取消" value="canceled" />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="applyFilter">查询</el-button>
          <el-button @click="resetFilter">重置</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <el-card class="table-card">
      <div class="table-count-bar" v-if="ordersFilteredCount !== null">
        当前页筛选后 {{ ordersFilteredCount }} / {{ orders.length }} 条
      </div>
      <el-table
        ref="ordersTableRef"
        :data="orders"
        style="width: 100%"
        stripe
        border
        v-loading="loading"
        @filter-change="handleOrdersFilterChange"
      >
        <el-table-column
          prop="orderId"
          label="订单ID"
          min-width="180"
          show-overflow-tooltip
          sortable
          :filters="getUniqueFilters('orderId')"
          :filter-method="(value, row) => row.orderId === value"
        />
        <el-table-column
          prop="strategy"
          label="策略名称"
          min-width="150"
          show-overflow-tooltip
          sortable
          :filters="getUniqueFilters('strategy')"
          :filter-method="(value, row) => row.strategy === value"
        />
        <el-table-column
          prop="symbol"
          label="交易对"
          min-width="140"
          show-overflow-tooltip
          sortable
          :filters="getUniqueFilters('symbol')"
          :filter-method="(value, row) => row.symbol === value"
        />
        <el-table-column
          prop="market"
          label="市场"
          min-width="100"
          show-overflow-tooltip
          sortable
          :filters="getUniqueFilters('market')"
          :filter-method="(value, row) => row.market === value"
        />
        <el-table-column
          prop="exEntno"
          label="交易所委托号"
          min-width="180"
          show-overflow-tooltip
        />
        <el-table-column
          prop="side"
          label="方向"
          min-width="100"
          align="center"
          sortable
          :filters="getSideFilters()"
          :filter-method="(value, row) => row.side === value"
        >
          <template #default="scope">
            <el-tag :type="getSideType(scope.row.side)">
              {{ getSideText(scope.row.side) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column
          prop="price"
          label="委托价格"
          min-width="120"
          align="right"
          sortable
          :sort-method="(a, b) => sortByNumber(a, b, 'price')"
        />
        <el-table-column
          prop="quantity"
          label="委托数量"
          min-width="120"
          align="right"
          sortable
          :sort-method="(a, b) => sortByNumber(a, b, 'quantity')"
        />
        <el-table-column
          prop="filledQty"
          label="成交数量"
          min-width="120"
          align="right"
          sortable
          :sort-method="(a, b) => sortByNumber(a, b, 'filledQty')"
        />
        <el-table-column
          prop="status"
          label="状态"
          min-width="110"
          align="center"
          sortable
          :filters="getStatusFilters()"
          :filter-method="(value, row) => String(row.status) === value"
        >
          <template #default="scope">
            <span class="status-tag" :style="getStatusStyle(scope.row.status)">
              {{ getStatusText(scope.row.status) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column
          prop="errMsg"
          label="废单原因"
          min-width="150"
          show-overflow-tooltip
        >
          <template #default="scope">
            <span v-if="scope.row.errMsg" style="color: #f56c6c;">{{ scope.row.errMsg }}</span>
            <span v-else>-</span>
          </template>
        </el-table-column>
        <el-table-column
          prop="createTime"
          label="创建时间"
          min-width="180"
          show-overflow-tooltip
          sortable
        />
        <el-table-column
          label="操作"
          fixed="right"
          min-width="120"
          align="center"
        >
          <template #default="scope">
            <el-button
              v-if="scope.row.status === 'submitted' || scope.row.status === 'partially_filled'"
              type="danger"
              size="small"
              @click="cancelOrder(scope.row)"
            >
              撤单
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :page-sizes="[10, 20, 50, 100]"
        :total="totalOrders"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
        style="margin-top: 20px; justify-content: center;"
      />
    </el-card>
  </div>
</template>

<script>
import { ref, computed, onMounted, watch, nextTick } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { dictService } from '@/services/dictService'
import request from '@/utils/request'

export default {
  name: 'OrderManager',
  setup() {
    const loading = ref(false)
    const orders = ref([])
    const currentPage = ref(1)
    const pageSize = ref(100)
    const totalOrders = ref(0)
    const dictLoaded = ref(false)

    const ordersTableRef = ref(null)
    const ordersFilteredCount = ref(null)

    const handleOrdersFilterChange = (filters) => {
      const hasActive = Object.values(filters).some(v => Array.isArray(v) && v.length > 0)
      if (!hasActive) { ordersFilteredCount.value = null; return }
      nextTick(() => {
        const el = ordersTableRef.value?.$el
        if (!el) return
        ordersFilteredCount.value = el.querySelectorAll('.el-table__body-wrapper tbody tr.el-table__row').length
      })
    }

    watch(orders, () => { ordersFilteredCount.value = null })

    const filterForm = ref({
      strategy: '',
      symbol: '',
      status: ''
    })

    // API基础URL - 使用相对路径通过Vite代理访问
    const API_BASE_URL = ''

    // 将纳秒时间戳转换为可读时间
    const formatTimestamp = (timestamp) => {
      if (!timestamp || timestamp === '0') return '-'
      // 如果是字符串，转换为数字
      const ts = typeof timestamp === 'string' ? parseInt(timestamp) : timestamp
      // 纳秒转毫秒
      const milliseconds = Math.floor(ts / 1000000)
      const date = new Date(milliseconds)
      return date.toLocaleString('zh-CN', {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hour12: false
      })
    }

    // 将后端数据转换为前端格式
    const transformOrder = (order) => {
      return {
        orderId: order.entno,
        strategy: order.policy_no,
        symbol: order.inst_id,
        market: order.market || '',
        exEntno: order.ex_entno || '',
        side: order.bs_side, // 保持原始值，由数据字典映射
        price: order.price?.toFixed(2) || '0.00',
        quantity: order.amount?.toFixed(4) || '0.0000',
        filledQty: order.filled?.toFixed(4) || '0.0000',
        status: order.status,
        errMsg: order.err_msg || '',
        createTime: formatTimestamp(order.ent_time),
        // 保留原始数据用于撤单等操作
        _raw: order
      }
    }

    const fetchOrders = async () => {
      loading.value = true
      try {
        // 构建查询参数
        const params = {
          page: currentPage.value,
          page_size: pageSize.value
        }

        // 添加过滤条件
        if (filterForm.value.strategy) {
          params.policy_no = filterForm.value.strategy
        }
        if (filterForm.value.symbol) {
          params.inst_id = filterForm.value.symbol
        }
        if (filterForm.value.status) {
          params.status = filterForm.value.status
        }

        const response = await request.get(`${API_BASE_URL}/api/orders`, { params })

        if (response.data.success) {
          // 转换数据格式
          orders.value = response.data.data.orders.map(transformOrder)
          totalOrders.value = response.data.data.total
          ElMessage.success('委托数据已刷新')
        } else {
          throw new Error(response.data.message || '获取委托数据失败')
        }
      } catch (error) {
        console.error('获取委托数据失败:', error)
        ElMessage.error('获取委托数据失败: ' + (error.response?.data?.error || error.message))
        // 失败时清空数据
        orders.value = []
        totalOrders.value = 0
      } finally {
        loading.value = false
      }
    }

    const applyFilter = () => {
      // 重置到第一页并重新获取数据
      currentPage.value = 1
      fetchOrders()
    }

    const resetFilter = () => {
      filterForm.value = {
        strategy: '',
        symbol: '',
        status: ''
      }
      currentPage.value = 1
      fetchOrders()
    }

    const cancelOrder = async (order) => {
      try {
        await ElMessageBox.confirm(
          `确定要撤销订单 ${order.orderId} 吗?`,
          '撤单确认',
          {
            confirmButtonText: '确定',
            cancelButtonText: '取消',
            type: 'warning',
          }
        )

        // TODO: 实现撤单API
        // await axios.post(`${API_BASE_URL}/api/orders/${order.orderId}/cancel`)

        ElMessage.warning('撤单功能暂未实现')
        // fetchOrders()
      } catch (error) {
        if (error !== 'cancel') {
          ElMessage.error('撤单失败: ' + (error.response?.data?.error || error.message))
        }
      }
    }

    const getStatusType = (status) => {
      // 使用数据字典服务获取状态类型
      if (dictLoaded.value) {
        return dictService.getType('EntrustStatus', status)
      }
      // 数据字典未加载时的降级处理
      return 'info'
    }

    const getStatusText = (status) => {
      // 使用数据字典服务获取状态显示文本
      if (dictLoaded.value) {
        return dictService.getName('EntrustStatus', status)
      }
      // 数据字典未加载时直接返回原值
      return String(status)
    }

    const getStatusStyle = (status) => {
      const color = dictService.getEntrustStatusColor(status)
      console.log('[status]', JSON.stringify(status), typeof status, '->', color)
      return `background-color: ${color} !important; border-color: ${color} !important; color: #ffffff !important;`
    }

    const getSideType = (side) => {
      // 使用数据字典服务获取买卖方向类型
      if (dictLoaded.value) {
        return dictService.getType('BuySellSide', side)
      }
      // 降级处理
      const upperSide = String(side).toUpperCase()
      return upperSide === 'B' ? 'success' : 'danger'
    }

    const getSideText = (side) => {
      // 使用数据字典服务获取买卖方向显示文本
      if (dictLoaded.value) {
        return dictService.getName('BuySellSide', side)
      }
      // 降级处理 - 与 dict.yml 中定义的 name 值保持一致
      const upperSide = String(side).toUpperCase()
      return upperSide === 'B' ? '买' : '卖'
    }

    const getSideFilters = () => {
      // 从数据字典获取买卖方向筛选选项
      if (dictLoaded.value) {
        const options = dictService.getOptions('BuySellSide')
        // 转换为大写，因为数据库存储的是大写
        return options.map(o => ({
          text: o.label,
          value: o.value.toUpperCase()
        }))
      }
      // 降级处理 - 与 dict.yml 中定义的 name 值保持一致
      return [
        { text: '买', value: 'B' },
        { text: '卖', value: 'S' }
      ]
    }

    // 加载数据字典
    const loadDict = async () => {
      try {
        await dictService.load()
        dictLoaded.value = true
        console.log('数据字典加载成功')
      } catch (error) {
        console.error('加载数据字典失败:', error)
        // 即使加载失败也继续，因为已经有降级方案
        dictLoaded.value = true
        console.warn('使用本地字典数据作为降级方案')
      }
    }

    const handleSizeChange = (val) => {
      pageSize.value = val
      currentPage.value = 1
      fetchOrders()
    }

    const handleCurrentChange = (val) => {
      currentPage.value = val
      fetchOrders()
    }

    // 获取唯一值的筛选选项（通用方法）
    const getUniqueFilters = (prop) => {
      const values = new Set()
      orders.value.forEach(order => {
        const val = order[prop]
        if (val !== undefined && val !== null && val !== '') {
          values.add(val)
        }
      })
      return Array.from(values).map(val => ({ text: String(val), value: val }))
    }

    // 按数字字段排序
    const sortByNumber = (a, b, prop) => {
      const aVal = a[prop]
      const bVal = b[prop]

      // 尝试转换为数字进行比较
      const aNum = typeof aVal === 'string' ? parseFloat(aVal) : aVal
      const bNum = typeof bVal === 'string' ? parseFloat(bVal) : bVal

      if (isNaN(aNum) && isNaN(bNum)) return 0
      if (isNaN(aNum)) return 1
      if (isNaN(bNum)) return -1

      return aNum - bNum
    }

    // 获取状态筛选选项
    const getStatusFilters = () => {
      const values = new Set()
      orders.value.forEach(order => {
        const val = order.status
        if (val !== undefined && val !== null && val !== '') {
          values.add(String(val))
        }
      })
      const opts = Array.from(values).map(v => ({
        text: getStatusText(v),
        value: String(v)
      }))

      // 如果没有数据，使用数据字典中的默认选项
      if (opts.length === 0 && dictLoaded.value) {
        const dictOptions = dictService.getOptions('EntrustStatus')
        return dictOptions.map(o => ({ text: o.label, value: String(o.value) }))
      }

      return opts
    }

    onMounted(async () => {
      // 先加载数据字典，再加载订单数据
      await loadDict()
      fetchOrders()
    })

    return {
      loading,
      orders,
      filterForm,
      currentPage,
      pageSize,
      totalOrders,
      dictLoaded,
      fetchOrders,
      applyFilter,
      resetFilter,
      cancelOrder,
      getStatusType,
      getStatusText,
      getStatusStyle,
      getSideType,
      getSideText,
      getSideFilters,
      handleSizeChange,
      handleCurrentChange,
      getUniqueFilters,
      sortByNumber,
      getStatusFilters,
      ordersTableRef,
      ordersFilteredCount,
      handleOrdersFilterChange
    }
  }
}
</script>

<style scoped>
.order-manager {
  padding: 20px;
}

.table-count-bar {
  font-size: 13px;
  color: #909399;
  margin-bottom: 8px;
}

.status-tag {
  display: inline-block;
  padding: 0 8px;
  height: 24px;
  line-height: 24px;
  border-radius: 4px;
  font-size: 12px;
  color: #ffffff;
}

.filter-card {
  margin-bottom: 20px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.filter-form {
  margin-top: 10px;
}

.table-card {
  background-color: #2c2c2c;
}

.el-card {
  background-color: #2c2c2c;
  border-color: #404040;
}

.el-card :deep(.el-card__header) {
  background-color: #2a2a2a;
  border-color: #404040;
  color: #e0e0e0;
}

.el-pagination {
  display: flex;
  justify-content: center;
}
</style>
