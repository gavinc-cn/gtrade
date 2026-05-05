<template>
  <div class="position-manager">
    <el-card class="filter-card">
      <template #header>
        <div class="card-header">
          <span>持仓查询</span>
          <el-button type="primary" size="small" @click="fetchPositions">
            <el-icon><Refresh /></el-icon>
            刷新
          </el-button>
        </div>
      </template>

      <el-form :inline="true" :model="filterForm" class="filter-form">
        <el-form-item label="账户ID">
          <el-input v-model="filterForm.account_id" placeholder="请输入账户ID" clearable />
        </el-form-item>
        <el-form-item label="标的名称">
          <el-input v-model="filterForm.instrument" placeholder="请输入标的名称" clearable />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="applyFilter">查询</el-button>
          <el-button @click="resetFilter">重置</el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <el-card class="table-card">
      <template #header>
        <div class="card-header">
          <span>持仓列表</span>
          <div class="summary-info">
            <span>总持仓: {{ total }} 个</span>
            <span style="margin-left: 20px;">总浮动盈亏:
              <span :style="{ color: totalUpl >= 0 ? '#67C23A' : '#F56C6C' }">
                {{ totalUpl >= 0 ? '+' : '' }}{{ totalUpl.toFixed(2) }} USDT
              </span>
            </span>
            <span v-if="reconciliationStatus && reconciliationStatus.inconsistent_count > 0"
                  style="margin-left: 20px; color: #F56C6C;">
              ⚠ 不一致持仓: {{ reconciliationStatus.inconsistent_count }} 个
            </span>
            <span v-if="reconciliationStatus && reconciliationStatus.last_reconciliation_time"
                  style="margin-left: 20px; font-size: 12px; color: #909399;">
              最后核算: {{ new Date(reconciliationStatus.last_reconciliation_time).toLocaleTimeString('zh-CN') }}
            </span>
          </div>
        </div>
      </template>

      <el-table
        :data="positions"
        style="width: 100%"
        stripe
        border
        v-loading="loading"
        :row-class-name="(scope) => isPositionInconsistent(scope.row) ? 'inconsistent-row' : ''"
      >
        <el-table-column prop="market" label="市场" width="100" />
        <el-table-column prop="account_id" label="账户ID" width="150" />
        <el-table-column prop="instrument" label="标的名称" width="150" />
        <el-table-column prop="pos_side" label="持仓方向" width="100">
          <template #default="scope">
            <el-tag :type="getPosSideType(scope.row.pos_side)">
              {{ getPosSideName(scope.row.pos_side) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="inst_type" label="标的类型" width="100">
          <template #default="scope">
            {{ getInstTypeName(scope.row.inst_type) }}
          </template>
        </el-table-column>
        <el-table-column prop="available" label="可用数量" width="120" align="right" />
        <el-table-column prop="avg_px" label="开仓均价" width="120" align="right">
          <template #default="scope">
            {{ scope.row.avg_px ? scope.row.avg_px.toFixed(4) : '-' }}
          </template>
        </el-table-column>
        <el-table-column prop="upl" label="未实现盈亏" width="140" align="right">
          <template #default="scope">
            <span :style="{ color: scope.row.upl >= 0 ? '#67C23A' : '#F56C6C' }">
              {{ scope.row.upl >= 0 ? '+' : '' }}{{ scope.row.upl ? scope.row.upl.toFixed(2) : '0.00' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="upl_ratio" label="收益率" width="100" align="right">
          <template #default="scope">
            <span :style="{ color: scope.row.upl_ratio >= 0 ? '#67C23A' : '#F56C6C' }">
              {{ scope.row.upl_ratio >= 0 ? '+' : '' }}{{ scope.row.upl_ratio ? (scope.row.upl_ratio * 100).toFixed(2) : '0.00' }}%
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="notional_usd" label="名义价值(USD)" width="140" align="right">
          <template #default="scope">
            {{ scope.row.notional_usd ? scope.row.notional_usd.toFixed(2) : '-' }}
          </template>
        </el-table-column>
        <el-table-column prop="margin_mode" label="保证金模式" width="120" align="center">
          <template #default="scope">
            {{ getTradeModeName(scope.row.margin_mode) }}
          </template>
        </el-table-column>
        <el-table-column prop="pos_source" label="持仓来源" width="100" align="center">
          <template #default="scope">
            {{ getPosSourceName(scope.row.pos_source) }}
          </template>
        </el-table-column>
        <el-table-column prop="datetime" label="更新时间" width="180" />
      </el-table>

      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :page-sizes="[10, 20, 50, 100]"
        :total="total"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
        style="margin-top: 20px; justify-content: center;"
      />
    </el-card>
  </div>
</template>

<script>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import request from '@/utils/request'
import { dictService } from '@/services/dictService'

// 使用相对路径，通过Vite代理访问后端API
const API_BASE_URL = ''

export default {
  name: 'PositionManager',
  setup() {
    const loading = ref(false)
    const dictLoaded = ref(false)

    // 持仓数据
    const positions = ref([])
    const currentPage = ref(1)
    const pageSize = ref(20)
    const total = ref(0)
    const filterForm = ref({
      account_id: '',
      instrument: ''
    })

    // 核算相关
    const inconsistentPositions = ref(new Set())
    const reconciliationStatus = ref(null)

    // 计算总浮动盈亏
    const totalUpl = computed(() => {
      return positions.value.reduce((sum, pos) => {
        return sum + (pos.upl || 0)
      }, 0)
    })

    // 获取持仓列表
    const fetchPositions = async () => {
      loading.value = true
      try {
        const params = {
          page: currentPage.value,
          page_size: pageSize.value
        }

        if (filterForm.value.account_id) {
          params.account_id = filterForm.value.account_id
        }
        if (filterForm.value.instrument) {
          params.instrument = filterForm.value.instrument
        }

        const response = await request.get('/api/positions', { params })

        if (response.data.success) {
          positions.value = response.data.data.positions
          total.value = response.data.data.total
          ElMessage.success('持仓数据已刷新')
        } else {
          ElMessage.error('获取持仓数据失败')
        }
      } catch (error) {
        console.error('获取持仓数据失败:', error)
        ElMessage.error('获取持仓数据失败: ' + (error.response?.data?.error || error.message))
      } finally {
        loading.value = false
      }
    }

    // 获取不一致的持仓列表
    const fetchInconsistentPositions = async () => {
      try {
        const response = await request.get('/api/reconciliation/inconsistent')

        if (response.data.success) {
          // 构建不一致持仓的 Set，用于快速查找
          const inconsistentSet = new Set()
          response.data.data.forEach(item => {
            const key = `${item.market}|${item.account_id}|${item.instrument}|${item.pos_side}`
            inconsistentSet.add(key)
          })
          inconsistentPositions.value = inconsistentSet
        }
      } catch (error) {
        console.error('获取核算数据失败:', error)
      }
    }

    // 获取核算状态
    const fetchReconciliationStatus = async () => {
      try {
        const response = await request.get('/api/reconciliation/status')

        if (response.data.success) {
          reconciliationStatus.value = response.data.data
        }
      } catch (error) {
        console.error('获取核算状态失败:', error)
      }
    }

    // 检查持仓是否不一致
    const isPositionInconsistent = (position) => {
      const key = `${position.market}|${position.account_id}|${position.instrument}|${position.pos_side}`
      return inconsistentPositions.value.has(key)
    }

    // 过滤
    const applyFilter = () => {
      currentPage.value = 1
      fetchPositions()
    }

    const resetFilter = () => {
      filterForm.value = {
        account_id: '',
        instrument: ''
      }
      currentPage.value = 1
      fetchPositions()
    }

    // 分页
    const handleSizeChange = (val) => {
      pageSize.value = val
      currentPage.value = 1
      fetchPositions()
    }

    const handleCurrentChange = (val) => {
      currentPage.value = val
      fetchPositions()
    }

    // 数据字典翻译函数
    const getPosSideName = (value) => {
      return dictService.getName('PosSide', value) || value || '-'
    }

    const getPosSideType = (value) => {
      const val = String(value).toLowerCase()
      return (val === 'l' || val === 'long') ? 'success' : 'danger'
    }

    const getInstTypeName = (value) => {
      return dictService.getName('InstType', value) || value || '-'
    }

    const getTradeModeName = (value) => {
      return dictService.getName('TradeMode', value) || value || '-'
    }

    const getPosSourceName = (value) => {
      return dictService.getName('PosSource', value) || value || '-'
    }

    // 加载数据字典
    onMounted(async () => {
      try {
        await dictService.load()
        dictLoaded.value = true
      } catch (error) {
        console.error('加载数据字典失败:', error)
      }
      fetchPositions()
      fetchInconsistentPositions()
      fetchReconciliationStatus()

      // 每30秒刷新一次核算状态（持仓核算每分钟执行一次）
      const reconciliationTimer = setInterval(() => {
        fetchInconsistentPositions()
        fetchReconciliationStatus()
      }, 30000)

      // 组件卸载时清除定时器
      onUnmounted(() => {
        clearInterval(reconciliationTimer)
      })
    })

    return {
      loading,
      positions,
      currentPage,
      pageSize,
      total,
      filterForm,
      totalUpl,
      fetchPositions,
      applyFilter,
      resetFilter,
      handleSizeChange,
      handleCurrentChange,
      getPosSideName,
      getPosSideType,
      getInstTypeName,
      getTradeModeName,
      getPosSourceName,
      isPositionInconsistent,
      reconciliationStatus
    }
  }
}
</script>

<style scoped>
.position-manager {
  padding: 20px;
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

/* 不一致持仓行的红色高亮 */
.position-manager :deep(.inconsistent-row) {
  background-color: rgba(245, 108, 108, 0.15) !important;
}

.position-manager :deep(.inconsistent-row:hover) {
  background-color: rgba(245, 108, 108, 0.25) !important;
}

.position-manager :deep(.inconsistent-row td) {
  background-color: transparent !important;
  border-color: rgba(245, 108, 108, 0.3) !important;
}
</style>
