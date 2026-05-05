<template>
  <div class="portfolio-position-manager">
    <el-card class="filter-card">
      <template #header>
        <div class="card-header">
          <span>组合持仓查询</span>
          <el-button type="primary" size="small" @click="fetchPortfolioPositions">
            <el-icon><Refresh /></el-icon>
            刷新
          </el-button>
        </div>
      </template>

      <el-form :inline="true" :model="filterForm" class="filter-form">
        <el-form-item label="账户ID">
          <el-input v-model="filterForm.account_id" placeholder="请输入账户ID" clearable />
        </el-form-item>
        <el-form-item label="组合名称">
          <el-input v-model="filterForm.portfolio" placeholder="请输入组合名称" clearable />
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
          <span>组合持仓列表</span>
          <div class="summary-info">
            <span>总持仓: {{ total }} 个</span>
            <span style="margin-left: 20px;">总浮动盈亏:
              <span :style="{ color: totalUpl >= 0 ? '#67C23A' : '#F56C6C' }">
                {{ totalUpl >= 0 ? '+' : '' }}{{ totalUpl.toFixed(2) }} USDT
              </span>
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
      >
        <el-table-column prop="market" label="市场" width="100" />
        <el-table-column prop="account_id" label="账户ID" width="150" />
        <el-table-column prop="portfolio" label="组合" width="150" />
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
import { ref, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import request from '@/utils/request'
import { dictService } from '@/services/dictService'

// 使用相对路径，通过Vite代理访问后端API
const API_BASE_URL = ''

export default {
  name: 'PortfolioPositionManager',
  setup() {
    const loading = ref(false)
    const dictLoaded = ref(false)

    // 组合持仓数据
    const positions = ref([])
    const currentPage = ref(1)
    const pageSize = ref(20)
    const total = ref(0)
    const filterForm = ref({
      account_id: '',
      portfolio: '',
      instrument: ''
    })

    // 计算总浮动盈亏
    const totalUpl = computed(() => {
      return positions.value.reduce((sum, pos) => {
        return sum + (pos.upl || 0)
      }, 0)
    })

    // 获取组合持仓列表
    const fetchPortfolioPositions = async () => {
      loading.value = true
      try {
        const params = {
          page: currentPage.value,
          page_size: pageSize.value
        }

        if (filterForm.value.account_id) {
          params.account_id = filterForm.value.account_id
        }
        if (filterForm.value.portfolio) {
          params.portfolio = filterForm.value.portfolio
        }
        if (filterForm.value.instrument) {
          params.instrument = filterForm.value.instrument
        }

        const response = await request.get('/api/portfolio_positions', { params })

        if (response.data.success) {
          positions.value = response.data.data.positions
          total.value = response.data.data.total
          ElMessage.success('组合持仓数据已刷新')
        } else {
          ElMessage.error('获取组合持仓数据失败')
        }
      } catch (error) {
        console.error('获取组合持仓数据失败:', error)
        ElMessage.error('获取组合持仓数据失败: ' + (error.response?.data?.error || error.message))
      } finally {
        loading.value = false
      }
    }

    // 过滤
    const applyFilter = () => {
      currentPage.value = 1
      fetchPortfolioPositions()
    }

    const resetFilter = () => {
      filterForm.value = {
        account_id: '',
        portfolio: '',
        instrument: ''
      }
      currentPage.value = 1
      fetchPortfolioPositions()
    }

    // 分页
    const handleSizeChange = (val) => {
      pageSize.value = val
      currentPage.value = 1
      fetchPortfolioPositions()
    }

    const handleCurrentChange = (val) => {
      currentPage.value = val
      fetchPortfolioPositions()
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
      fetchPortfolioPositions()
    })

    return {
      loading,
      positions,
      currentPage,
      pageSize,
      total,
      filterForm,
      totalUpl,
      fetchPortfolioPositions,
      applyFilter,
      resetFilter,
      handleSizeChange,
      handleCurrentChange,
      getPosSideName,
      getPosSideType,
      getInstTypeName,
      getTradeModeName,
      getPosSourceName
    }
  }
}
</script>

<style scoped>
.portfolio-position-manager {
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
</style>
