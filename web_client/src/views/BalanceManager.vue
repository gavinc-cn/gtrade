<template>
  <div class="balance-manager">
    <el-card class="filter-card">
      <template #header>
        <div class="card-header">
          <span>资金查询</span>
          <el-button type="primary" size="small" @click="fetchBalances">
            <el-icon><Refresh /></el-icon>
            刷新
          </el-button>
        </div>
      </template>

      <el-form :inline="true" :model="filterForm" class="filter-form">
        <el-form-item label="账户ID">
          <el-input v-model="filterForm.account_id" placeholder="请输入账户ID" clearable />
        </el-form-item>
        <el-form-item label="币种">
          <el-input v-model="filterForm.currency" placeholder="请输入币种" clearable />
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
          <span>资金列表</span>
          <div class="summary-info">
            <span>共 {{ total }} 条记录</span>
            <span style="margin-left: 20px;">总资产(USDT 等价):
              <span style="color: #67C23A; font-weight: bold;">
                {{ totalAsset.toFixed(2) }}
              </span>
            </span>
          </div>
        </div>
      </template>

      <el-table
        :data="balances"
        style="width: 100%"
        stripe
        border
        v-loading="loading"
        :row-class-name="getRowClass"
      >
        <el-table-column prop="market" label="市场" width="100" />
        <el-table-column prop="account_id" label="账户ID" min-width="150" show-overflow-tooltip />
        <el-table-column prop="currency" label="币种" width="100">
          <template #default="scope">
            <el-tag type="info" size="small">{{ scope.row.currency }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="total" label="总余额" width="160" align="right">
          <template #default="scope">
            <span style="font-weight: bold;">
              {{ formatAmount(scope.row.total) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="available" label="可用余额" width="160" align="right">
          <template #default="scope">
            <span style="color: #67C23A;">
              {{ formatAmount(scope.row.available) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="frozen" label="冻结余额" width="160" align="right">
          <template #default="scope">
            <span :style="{ color: scope.row.frozen > 0 ? '#E6A23C' : '#909399' }">
              {{ formatAmount(scope.row.frozen) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column label="冻结比例" width="110" align="center">
          <template #default="scope">
            <el-progress
              v-if="scope.row.total > 0"
              :percentage="Math.round((scope.row.frozen / scope.row.total) * 100)"
              :color="getFrozenColor(scope.row.frozen, scope.row.total)"
              :stroke-width="8"
              style="width: 80px;"
            />
            <span v-else>-</span>
          </template>
        </el-table-column>
        <el-table-column prop="datetime" label="更新时间" min-width="180" />
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

export default {
  name: 'BalanceManager',
  setup() {
    const loading = ref(false)

    // 资金数据
    const balances = ref([])
    const currentPage = ref(1)
    const pageSize = ref(20)
    const total = ref(0)
    const filterForm = ref({
      account_id: '',
      currency: ''
    })

    // 计算总资产（仅供参考，非精确折算）
    const totalAsset = computed(() => {
      return balances.value.reduce((sum, bal) => {
        // 对于稳定币或USDT类资产直接累加，其他资产暂不折算
        const stableCoins = ['USDT', 'USDC', 'BUSD', 'DAI', 'TUSD']
        if (stableCoins.includes(bal.currency?.toUpperCase())) {
          return sum + (bal.total || 0)
        }
        return sum
      }, 0)
    })

    // 获取资金列表
    const fetchBalances = async () => {
      loading.value = true
      try {
        const params = {
          page: currentPage.value,
          page_size: pageSize.value
        }

        if (filterForm.value.account_id) {
          params.account_id = filterForm.value.account_id
        }
        if (filterForm.value.currency) {
          params.currency = filterForm.value.currency
        }

        const response = await request.get('/api/balances', { params })

        if (response.data.success) {
          balances.value = response.data.data.balances
          total.value = response.data.data.total
          ElMessage.success('资金数据已刷新')
        } else {
          ElMessage.error('获取资金数据失败')
        }
      } catch (error) {
        console.error('获取资金数据失败:', error)
        ElMessage.error('获取资金数据失败: ' + (error.response?.data?.error || error.message))
      } finally {
        loading.value = false
      }
    }

    // 过滤
    const applyFilter = () => {
      currentPage.value = 1
      fetchBalances()
    }

    const resetFilter = () => {
      filterForm.value = {
        account_id: '',
        currency: ''
      }
      currentPage.value = 1
      fetchBalances()
    }

    // 分页
    const handleSizeChange = (val) => {
      pageSize.value = val
      currentPage.value = 1
      fetchBalances()
    }

    const handleCurrentChange = (val) => {
      currentPage.value = val
      fetchBalances()
    }

    // 格式化金额，根据数值大小动态调整小数位数
    const formatAmount = (val) => {
      if (val === null || val === undefined) return '-'
      const num = Number(val)
      if (num === 0) return '0'
      if (Math.abs(num) >= 1) return num.toFixed(4)
      return num.toFixed(8)
    }

    // 根据冻结比例返回进度条颜色
    const getFrozenColor = (frozen, total) => {
      if (!total || total === 0) return '#909399'
      const ratio = frozen / total
      if (ratio < 0.3) return '#67C23A'
      if (ratio < 0.7) return '#E6A23C'
      return '#F56C6C'
    }

    // 行样式：冻结比例高的行高亮
    const getRowClass = ({ row }) => {
      if (row.total > 0 && row.frozen / row.total > 0.7) {
        return 'high-frozen-row'
      }
      return ''
    }

    onMounted(() => {
      fetchBalances()
    })

    return {
      loading,
      balances,
      currentPage,
      pageSize,
      total,
      filterForm,
      totalAsset,
      fetchBalances,
      applyFilter,
      resetFilter,
      handleSizeChange,
      handleCurrentChange,
      formatAmount,
      getFrozenColor,
      getRowClass
    }
  }
}
</script>

<style scoped>
.balance-manager {
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

:deep(.high-frozen-row) {
  background-color: rgba(245, 108, 108, 0.08) !important;
}
</style>
