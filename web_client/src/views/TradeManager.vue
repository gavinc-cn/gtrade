<template>
  <div class="trade-manager">
    <el-card class="filter-card">
      <template #header>
        <div class="card-header">
          <span>成交查询</span>
          <el-button type="primary" size="small" @click="fetchTrades">
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
        <el-form-item label="方向">
          <el-select v-model="filterForm.side" placeholder="请选择方向" clearable>
            <el-option label="全部" value="" />
            <el-option label="买入" value="B" />
            <el-option label="卖出" value="S" />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="applyFilter">查询</el-button>
          <el-button @click="resetFilter">重置</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <el-card class="table-card">
      <div class="table-count-bar" v-if="tradesFilteredCount !== null">
        当前页筛选后 {{ tradesFilteredCount }} / {{ trades.length }} 条
      </div>
      <el-table
        ref="tradesTableRef"
        :data="trades"
        style="width: 100%"
        stripe
        border
        v-loading="loading"
        @filter-change="handleTradesFilterChange"
      >
        <el-table-column
          prop="tdno"
          label="成交ID"
          min-width="180"
          show-overflow-tooltip
          sortable
          :filters="getUniqueFilters('tdno')"
          :filter-method="(value, row) => row.tdno === value"
        />
        <el-table-column
          prop="ordno"
          label="委托ID"
          min-width="180"
          show-overflow-tooltip
          sortable
          :filters="getUniqueFilters('ordno')"
          :filter-method="(value, row) => row.ordno === value"
        />
        <el-table-column
          prop="strat_id"
          label="策略名称"
          min-width="200"
          show-overflow-tooltip
          sortable
          :filters="getUniqueFilters('strat_id')"
          :filter-method="(value, row) => row.strat_id === value"
        />
        <el-table-column
          prop="instrument"
          label="交易对"
          min-width="140"
          show-overflow-tooltip
          sortable
          :filters="getUniqueFilters('instrument')"
          :filter-method="(value, row) => row.instrument === value"
        />
        <el-table-column
          prop="td_side"
          label="方向"
          min-width="100"
          align="center"
          sortable
          :filters="getSideFilters()"
          :filter-method="(value, row) => row.td_side === value"
        >
          <template #default="scope">
            <el-tag :type="getSideType(scope.row.td_side)">
              {{ getSideText(scope.row.td_side) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column
          prop="td_px"
          label="成交价格"
          min-width="120"
          align="right"
          sortable
          :sort-method="(a, b) => sortByNumber(a, b, 'td_px')"
        >
          <template #default="scope">
            {{ formatNumber(scope.row.td_px, 2) }}
          </template>
        </el-table-column>
        <el-table-column
          prop="td_qty"
          label="成交数量"
          min-width="120"
          align="right"
          sortable
          :sort-method="(a, b) => sortByNumber(a, b, 'td_qty')"
        >
          <template #default="scope">
            {{ formatNumber(scope.row.td_qty, 4) }}
          </template>
        </el-table-column>
        <el-table-column
          label="成交金额"
          min-width="140"
          align="right"
          sortable
          :sort-method="(a, b) => sortByNumber(a, b, 'amount')"
        >
          <template #default="scope">
            {{ formatNumber(scope.row.td_px * scope.row.td_qty, 2) }}
          </template>
        </el-table-column>
        <el-table-column
          prop="pos_side"
          label="持仓方向"
          min-width="100"
          align="center"
          sortable
          :filters="getPosSideFilters()"
          :filter-method="(value, row) => row.pos_side === value"
        >
          <template #default="scope">
            <el-tag :type="getPosSideType(scope.row.pos_side)">
              {{ getPosSideName(scope.row.pos_side) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column
          prop="filled_time"
          label="成交时间"
          min-width="180"
          show-overflow-tooltip
          sortable
        >
          <template #default="scope">
            {{ formatTimestamp(scope.row.filled_time) }}
          </template>
        </el-table-column>
      </el-table>

      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :page-sizes="[10, 20, 50, 100]"
        :total="totalTrades"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
        style="margin-top: 20px; justify-content: center;"
      />
    </el-card>
  </div>
</template>

<script>
import { ref, onMounted, nextTick, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { Refresh } from '@element-plus/icons-vue'
import request from '@/utils/request'
import { dictService } from '@/services/dictService'

export default {
  name: 'TradeManager',
  setup() {
    const loading = ref(false)
    const trades = ref([])
    const currentPage = ref(1)
    const pageSize = ref(20)
    const totalTrades = ref(0)
    const dictLoaded = ref(false)

    const tradesTableRef = ref(null)
    const tradesFilteredCount = ref(null)

    const handleTradesFilterChange = (filters) => {
      const hasActive = Object.values(filters).some(v => Array.isArray(v) && v.length > 0)
      if (!hasActive) { tradesFilteredCount.value = null; return }
      nextTick(() => {
        const el = tradesTableRef.value?.$el
        if (!el) return
        tradesFilteredCount.value = el.querySelectorAll('.el-table__body-wrapper tbody tr.el-table__row').length
      })
    }

    watch(trades, () => { tradesFilteredCount.value = null })

    const filterForm = ref({
      strategy: '',
      symbol: '',
      side: ''
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

    // 格式化数字
    const formatNumber = (value, decimals = 2) => {
      if (value === null || value === undefined) return '-'
      const num = typeof value === 'string' ? parseFloat(value) : value
      if (isNaN(num)) return '-'
      return num.toFixed(decimals)
    }

    const fetchTrades = async () => {
      loading.value = true
      try {
        // 构建查询参数
        const params = {
          page: currentPage.value,
          page_size: pageSize.value
        }

        // 添加过滤条件
        if (filterForm.value.strategy) {
          params.strat_id = filterForm.value.strategy
        }
        if (filterForm.value.symbol) {
          params.instrument = filterForm.value.symbol
        }
        if (filterForm.value.side) {
          params.td_side = filterForm.value.side
        }

        const response = await request.get(`${API_BASE_URL}/api/trades`, { params })

        if (response.data.success) {
          trades.value = response.data.data.trades
          totalTrades.value = response.data.data.total
          ElMessage.success('成交数据已刷新')
        } else {
          throw new Error(response.data.message || '获取成交数据失败')
        }
      } catch (error) {
        console.error('获取成交数据失败:', error)
        ElMessage.error('获取成交数据失败: ' + (error.response?.data?.error || error.message))
        // 失败时清空数据
        trades.value = []
        totalTrades.value = 0
      } finally {
        loading.value = false
      }
    }

    const applyFilter = () => {
      // 重置到第一页并重新获取数据
      currentPage.value = 1
      fetchTrades()
    }

    const resetFilter = () => {
      filterForm.value = {
        strategy: '',
        symbol: '',
        side: ''
      }
      currentPage.value = 1
      fetchTrades()
    }

    const handleSizeChange = (val) => {
      pageSize.value = val
      currentPage.value = 1
      fetchTrades()
    }

    const handleCurrentChange = (val) => {
      currentPage.value = val
      fetchTrades()
    }

    // 获取唯一值的筛选选项（通用方法）
    const getUniqueFilters = (prop) => {
      const values = new Set()
      trades.value.forEach(trade => {
        const val = trade[prop]
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

    const getPosSideName = (value) => {
      // 使用数据字典服务获取持仓方向显示文本
      if (dictLoaded.value) {
        return dictService.getName('PosSide', value)
      }
      // 降级处理
      const lowerValue = String(value).toLowerCase()
      if (lowerValue === 'l') return '多'
      if (lowerValue === 's') return '空'
      if (lowerValue === 'n') return '净'
      return String(value)
    }

    const getPosSideType = (value) => {
      // 使用数据字典服务获取持仓方向类型
      const lowerValue = String(value).toLowerCase()
      return lowerValue === 'l' ? 'success' : (lowerValue === 's' ? 'danger' : 'info')
    }

    const getPosSideFilters = () => {
      // 从数据字典获取持仓方向筛选选项
      if (dictLoaded.value) {
        const options = dictService.getOptions('PosSide')
        return options.map(o => ({
          text: o.label,
          value: o.value
        }))
      }
      // 降级处理
      return [
        { text: '多', value: 'l' },
        { text: '空', value: 's' },
        { text: '净', value: 'n' }
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

    onMounted(async () => {
      // 先加载数据字典，再加载成交数据
      await loadDict()
      fetchTrades()
    })

    return {
      loading,
      trades,
      filterForm,
      currentPage,
      pageSize,
      totalTrades,
      dictLoaded,
      fetchTrades,
      applyFilter,
      resetFilter,
      handleSizeChange,
      handleCurrentChange,
      formatTimestamp,
      formatNumber,
      getUniqueFilters,
      sortByNumber,
      getSideType,
      getSideText,
      getSideFilters,
      getPosSideName,
      getPosSideType,
      getPosSideFilters,
      Refresh,
      tradesTableRef,
      tradesFilteredCount,
      handleTradesFilterChange
    }
  }
}
</script>

<style scoped>
.trade-manager {
  padding: 20px;
}

.table-count-bar {
  font-size: 13px;
  color: #909399;
  margin-bottom: 8px;
}

.filter-card {
  margin-bottom: 20px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.summary-info {
  font-size: 14px;
  color: #909399;
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
