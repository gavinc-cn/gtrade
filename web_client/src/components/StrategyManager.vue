<template>
  <div class="strategy-manager">
    <!-- 标题栏和自动刷新控件 -->
    <div class="header-bar">
      <h2 class="page-title">GTrade Strategy Manager</h2>
      <div class="header-controls">
        <el-button
          type="primary"
          @click="showAddStrategyDialog"
        >
          添加策略
        </el-button>
        <div class="auto-refresh-config">
          <span>自动刷新间隔(秒):</span>
          <el-select v-model="refreshInterval" @change="handleRefreshIntervalChange" style="width: 120px;">
            <el-option label="不刷新" :value="0"></el-option>
            <el-option label="3秒" :value="3"></el-option>
            <el-option label="5秒" :value="5"></el-option>
            <el-option label="10秒" :value="10"></el-option>
            <el-option label="30秒" :value="30"></el-option>
            <el-option label="60秒" :value="60"></el-option>
          </el-select>
        </div>
      </div>
    </div>

    <!-- 标签页 -->
    <el-tabs v-model="activeTab" type="card" @tab-change="handleTabChange" class="main-tabs">
      <el-tab-pane
        v-for="template in templates"
        :key="template"
        :label="getTemplateLabel(template)"
        :name="template"
      >
        <div class="tab-content">
          <!-- 操作按钮 -->
          <div class="operation-buttons">
            <el-button
              type="success"
              :icon="VideoPlay"
              @click="batchOperation('start')"
              :disabled="!selectedStrategies.length"
            >
              批量启动
            </el-button>
            <el-button
              type="danger"
              :icon="VideoPause"
              @click="batchOperation('stop')"
              :disabled="!selectedStrategies.length"
            >
              批量停止
            </el-button>
            <el-button
              type="warning"
              :icon="RefreshRight"
              @click="batchOperation('restart')"
              :disabled="!selectedStrategies.length"
            >
              批量重启
            </el-button>
            <el-button
              type="danger"
              :icon="Delete"
              @click="batchDeleteStrategies"
              :disabled="!selectedStrategies.length"
            >
              批量删除
            </el-button>
            <el-button
              type="primary"
              :icon="Refresh"
              @click="loadStrategies"
            >
              刷新
            </el-button>
          </div>

          <!-- 策略表格行数统计 -->
          <div class="table-count-bar">
            <span v-if="strategyFilteredCount !== null">筛选后 {{ strategyFilteredCount }} / 共 {{ currentStrategies.length }} 条</span>
            <span v-else>共 {{ currentStrategies.length }} 条</span>
          </div>

          <!-- 策略表格 -->
          <el-table
            ref="strategyTableRef"
            :data="currentStrategies"
            stripe
            border
            style="width: 100%; margin-top: 8px;"
            @selection-change="handleSelectionChange"
            v-loading="loading"
            @row-contextmenu="handleContextMenu"
            @row-click="handleRowClick"
            @filter-change="handleStrategyFilterChange"
            highlight-current-row
          >
            <el-table-column type="selection" width="55" />

            <!-- 更新时间列 -->
            <el-table-column
              prop="update_time"
              label="更新时间"
              :min-width="getUpdateTimeColumnWidth()"
              align="center"
              sortable
            >
              <template #default="scope">
                <span :style="{ color: getUpdateTimeColor(scope.row.update_time) }">
                  {{ scope.row.update_time || '-' }}
                </span>
              </template>
            </el-table-column>

            <!-- Indicator 列 -->
            <el-table-column
              v-for="indicator in templateConfig.indicator"
              :key="'indicator_' + indicator.id"
              :column-key="'indicator_' + indicator.id"
              :label="indicator.name || indicator.id"
              :min-width="getIndicatorColumnWidth(indicator)"
              align="center"
              show-overflow-tooltip
              sortable
              label-class-name="indicator-col-header"
              :sort-method="(a, b) => sortByIndicator(a, b, indicator.id)"
              :filters="getIndicatorFilters(indicator.id)"
              :filter-method="(value, row) => filterByIndicator(value, row, indicator.id)"
            >
              <template #default="scope">
                {{ scope.row.indicators ? scope.row.indicators[indicator.id] : '-' }}
              </template>
            </el-table-column>

            <!-- Param 列 -->
            <el-table-column
              v-for="param in templateConfig.param"
              :key="'param_' + param.id"
              :column-key="'param_' + param.id"
              :label="param.name || param.id"
              :min-width="getParamColumnWidth(param)"
              align="center"
              show-overflow-tooltip
              sortable
              label-class-name="param-col-header"
              :sort-method="(a, b) => sortByParam(a, b, param.id)"
              :filters="getParamFilters(param.id)"
              :filter-method="(value, row) => filterByParam(value, row, param.id)"
            >
              <template #default="scope">
                {{ scope.row.params ? scope.row.params[param.id] : '-' }}
              </template>
            </el-table-column>

            <!-- 状态列 -->
            <el-table-column
              prop="status"
              column-key="status"
              label="状态"
              :min-width="getStatusColumnWidth()"
              fixed="right"
              align="center"
              sortable
              :filters="getStatusFilters()"
              :filter-method="filterStatus"
            >
              <template #default="scope">
                <el-tag :type="scope.row.status === 1 ? 'success' : 'info'" size="small">
                  {{ scope.row.status === 1 ? '运行中' : '已停止' }}
                </el-tag>
              </template>
            </el-table-column>

            <!-- 操作列 -->
            <el-table-column label="操作" min-width="300" fixed="right" align="center">
              <template #default="scope">
                <div class="operation-btn-group">
                  <el-button
                    size="small"
                    type="success"
                    :icon="VideoPlay"
                    @click.stop="startStrategy(scope.row.strat_name)"
                    :disabled="scope.row.status === 1"
                  >
                    启动
                  </el-button>
                  <el-button
                    size="small"
                    type="danger"
                    :icon="VideoPause"
                    @click.stop="stopStrategy(scope.row.strat_name)"
                    :disabled="scope.row.status !== 1"
                  >
                    停止
                  </el-button>
                  <el-button
                    size="small"
                    type="warning"
                    :icon="RefreshRight"
                    @click.stop="restartStrategy(scope.row.strat_name)"
                  >
                    重启
                  </el-button>
                  <el-button
                    size="small"
                    type="danger"
                    :icon="Delete"
                    @click.stop="deleteStrategy(scope.row.strat_name)"
                  >
                    删除
                  </el-button>
                </div>
              </template>
            </el-table-column>
          </el-table>

          <!-- 策略详情标签页 (委托、成交、持仓) -->
          <div class="strategy-details" v-if="selectedStrategyForDetails">
            <h3>策略详情: {{ selectedStrategyForDetails.strat_name }}</h3>
            <el-tabs v-model="detailsTab" type="border-card">
              <!-- 委托标签 -->
              <el-tab-pane label="委托" name="orders">
                <div class="table-count-bar">
                  <span v-if="ordersFilteredCount !== null">筛选后 {{ ordersFilteredCount }} / 共 {{ ordersList.length }} 条</span>
                  <span v-else>共 {{ ordersList.length }} 条</span>
                </div>
                <el-table
                  ref="ordersTableRef"
                  :data="ordersList"
                  stripe
                  border
                  style="width: 100%"
                  v-loading="ordersLoading"
                  max-height="400"
                  @filter-change="handleOrdersFilterChange"
                >
                  <el-table-column
                    prop="entno"
                    label="委托号"
                    :min-width="calcAdaptiveColumnWidth('委托号', ordersList.map(r => String(r.entno ?? '-')), 100, 260)"
                    show-overflow-tooltip
                    sortable
                    :filters="getUniqueFilters(ordersList, 'entno')"
                    :filter-method="(value, row) => row.entno === value"
                  />
                  <el-table-column
                    prop="inst_id"
                    label="标的"
                    :min-width="calcAdaptiveColumnWidth('标的', ordersList.map(r => String(r.inst_id ?? '-')), 90, 220)"
                    show-overflow-tooltip
                    sortable
                    :filters="getUniqueFilters(ordersList, 'inst_id')"
                    :filter-method="(value, row) => row.inst_id === value"
                  />
                  <el-table-column
                    prop="market"
                    label="市场"
                    :min-width="calcAdaptiveColumnWidth('市场', ordersList.map(r => String(r.market ?? '-')), 70, 120)"
                    show-overflow-tooltip
                    sortable
                    :filters="getUniqueFilters(ordersList, 'market')"
                    :filter-method="(value, row) => row.market === value"
                  />
                  <el-table-column
                    prop="ex_entno"
                    label="交易所委托号"
                    :min-width="calcAdaptiveColumnWidth('交易所委托号', ordersList.map(r => String(r.ex_entno ?? '-')), 130, 280)"
                    show-overflow-tooltip
                  />
                  <el-table-column
                    prop="bs_side"
                    label="买卖"
                    min-width="80"
                    align="center"
                    sortable
                    :filters="getSideFilters(ordersList, 'bs_side')"
                    :filter-method="(value, row) => row.bs_side?.toUpperCase() === value"
                  >
                    <template #default="scope">
                      <el-tag :type="getSideType(scope.row.bs_side)" size="small">
                        {{ getSideText(scope.row.bs_side) }}
                      </el-tag>
                    </template>
                  </el-table-column>
                  <el-table-column prop="price" label="价格" :min-width="calcAdaptiveColumnWidth('价格', ordersList.map(r => String(r.price ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column prop="amount" label="数量" :min-width="calcAdaptiveColumnWidth('数量', ordersList.map(r => String(r.amount ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column prop="filled" label="成交数量" :min-width="calcAdaptiveColumnWidth('成交数量', ordersList.map(r => String(r.filled ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column prop="remain" label="剩余数量" :min-width="calcAdaptiveColumnWidth('剩余数量', ordersList.map(r => String(r.remain ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column
                    prop="status"
                    label="状态"
                    min-width="80"
                    align="center"
                    sortable
                    :filters="getEntrustStatusFilters()"
                    :filter-method="(value, row) => String(row.status) === value"
                  >
                    <template #default="scope">
                      <span class="status-tag" :style="getEntrustStatusStyle(scope.row.status)">
                        {{ getEntrustStatusText(scope.row.status) }}
                      </span>
                    </template>
                  </el-table-column>
                  <el-table-column
                    prop="err_msg"
                    label="废单原因"
                    :min-width="calcAdaptiveColumnWidth('废单原因', ordersList.map(r => String(r.err_msg ?? '-')), 100, 320)"
                    show-overflow-tooltip
                  >
                    <template #default="scope">
                      <span v-if="scope.row.err_msg" style="color: #f56c6c;">{{ scope.row.err_msg }}</span>
                      <span v-else>-</span>
                    </template>
                  </el-table-column>
                  <el-table-column prop="ent_time" label="委托时间" min-width="180" show-overflow-tooltip sortable>
                    <template #default="scope">
                      {{ formatNanoTime(scope.row.ent_time) }}
                    </template>
                  </el-table-column>
                </el-table>
              </el-tab-pane>

              <!-- 成交标签 -->
              <el-tab-pane label="成交" name="trades">
                <div class="table-count-bar">
                  <span v-if="tradesFilteredCount !== null">筛选后 {{ tradesFilteredCount }} / 共 {{ tradesList.length }} 条</span>
                  <span v-else>共 {{ tradesList.length }} 条</span>
                </div>
                <el-table
                  ref="tradesTableRef"
                  :data="tradesList"
                  stripe
                  border
                  style="width: 100%"
                  v-loading="tradesLoading"
                  max-height="400"
                  @filter-change="handleTradesFilterChange"
                >
                  <el-table-column
                    prop="tdno"
                    label="成交号"
                    :min-width="calcAdaptiveColumnWidth('成交号', tradesList.map(r => String(r.tdno ?? '-')), 100, 260)"
                    show-overflow-tooltip
                    sortable
                    :filters="getUniqueFilters(tradesList, 'tdno')"
                    :filter-method="(value, row) => row.tdno === value"
                  />
                  <el-table-column
                    prop="ordno"
                    label="委托号"
                    :min-width="calcAdaptiveColumnWidth('委托号', tradesList.map(r => String(r.ordno ?? '-')), 100, 260)"
                    show-overflow-tooltip
                    sortable
                    :filters="getUniqueFilters(tradesList, 'ordno')"
                    :filter-method="(value, row) => row.ordno === value"
                  />
                  <el-table-column
                    prop="instrument"
                    label="标的"
                    :min-width="calcAdaptiveColumnWidth('标的', tradesList.map(r => String(r.instrument ?? '-')), 90, 220)"
                    show-overflow-tooltip
                    sortable
                    :filters="getUniqueFilters(tradesList, 'instrument')"
                    :filter-method="(value, row) => row.instrument === value"
                  />
                  <el-table-column
                    prop="td_side"
                    label="买卖"
                    min-width="80"
                    align="center"
                    sortable
                    :filters="getSideFilters(tradesList, 'td_side')"
                    :filter-method="(value, row) => row.td_side?.toUpperCase() === value"
                  >
                    <template #default="scope">
                      <el-tag :type="getSideType(scope.row.td_side)" size="small">
                        {{ getSideText(scope.row.td_side) }}
                      </el-tag>
                    </template>
                  </el-table-column>
                  <el-table-column prop="td_px" label="成交价" :min-width="calcAdaptiveColumnWidth('成交价', tradesList.map(r => String(r.td_px ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column prop="td_qty" label="成交量" :min-width="calcAdaptiveColumnWidth('成交量', tradesList.map(r => String(r.td_qty ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column prop="filled_time" label="成交时间" min-width="180" show-overflow-tooltip sortable>
                    <template #default="scope">
                      {{ formatNanoTime(scope.row.filled_time) }}
                    </template>
                  </el-table-column>
                </el-table>
              </el-tab-pane>

              <!-- 持仓标签 -->
              <el-tab-pane label="持仓" name="positions">
                <div class="table-count-bar">
                  <span v-if="positionsFilteredCount !== null">筛选后 {{ positionsFilteredCount }} / 共 {{ positionsList.length }} 条</span>
                  <span v-else>共 {{ positionsList.length }} 条</span>
                </div>
                <el-table
                  ref="positionsTableRef"
                  :data="positionsList"
                  stripe
                  border
                  style="width: 100%"
                  v-loading="positionsLoading"
                  max-height="400"
                  @filter-change="handlePositionsFilterChange"
                >
                  <el-table-column
                    prop="instrument"
                    label="标的"
                    :min-width="calcAdaptiveColumnWidth('标的', positionsList.map(r => String(r.instrument ?? '-')), 90, 220)"
                    show-overflow-tooltip
                    sortable
                    :filters="getUniqueFilters(positionsList, 'instrument')"
                    :filter-method="(value, row) => row.instrument === value"
                  />
                  <el-table-column
                    prop="pos_side"
                    label="持仓方向"
                    min-width="100"
                    align="center"
                    sortable
                    :filters="getPosSideFilters(positionsList, 'pos_side')"
                    :filter-method="(value, row) => row.pos_side === value"
                  >
                    <template #default="scope">
                      <el-tag :type="getPosSideType(scope.row.pos_side)" size="small">
                        {{ getPosSideName(scope.row.pos_side) }}
                      </el-tag>
                    </template>
                  </el-table-column>
                  <el-table-column prop="position" label="持仓量" :min-width="calcAdaptiveColumnWidth('持仓量', positionsList.map(r => String(r.position ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column prop="available" label="可用" :min-width="calcAdaptiveColumnWidth('可用', positionsList.map(r => String(r.available ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column prop="avg_price" label="均价" :min-width="calcAdaptiveColumnWidth('均价', positionsList.map(r => String(r.avg_price ?? '-')), 90, 200)" align="right" sortable />
                  <el-table-column prop="unrealized_pnl" label="未实现盈亏" :min-width="calcAdaptiveColumnWidth('未实现盈亏', positionsList.map(r => String(r.unrealized_pnl ?? '-')), 110, 220)" align="right" sortable />
                </el-table>
              </el-tab-pane>

              <!-- 日志标签 -->
              <el-tab-pane label="日志" name="logs">
                <div class="table-count-bar">
                  <span>共 {{ logsList.length }} 条</span>
                </div>
                <el-table
                  :data="logsList"
                  stripe
                  border
                  style="width: 100%"
                  v-loading="logsLoading"
                  max-height="400"
                >
                  <el-table-column prop="log_time" label="时间" min-width="180" show-overflow-tooltip sortable>
                    <template #default="scope">
                      {{ formatNanoTime(scope.row.log_time) }}
                    </template>
                  </el-table-column>
                  <el-table-column prop="log_level" label="等级" min-width="80" align="center">
                    <template #default="scope">
                      <el-tag
                        :type="scope.row.log_level === 'E' ? 'danger' : scope.row.log_level === 'W' ? 'warning' : 'primary'"
                        size="small"
                      >
                        {{ scope.row.log_level === 'E' ? 'ERROR' : scope.row.log_level === 'W' ? 'WARN' : 'INFO' }}
                      </el-tag>
                    </template>
                  </el-table-column>
                  <el-table-column prop="content" label="内容" :min-width="calcAdaptiveColumnWidth('内容', logsList.map(r => String(r.content ?? '-')), 200, 1000)" />
                </el-table>
              </el-tab-pane>

              <!-- 指标标签 -->
              <el-tab-pane label="指标" name="indicators">
                <div class="kv-grid" v-if="selectedStrategyForDetails && Object.keys(selectedStrategyForDetails.indicators || {}).length > 0">
                  <div
                    class="kv-item kv-item--indicator"
                    v-for="(value, key) in selectedStrategyForDetails.indicators"
                    :key="key"
                  >
                    <span class="kv-key kv-key--indicator">{{ getIndicatorName(key) }}</span>
                    <span class="kv-value">{{ value }}</span>
                  </div>
                </div>
                <el-empty v-else description="暂无指标数据" :image-size="60" />
              </el-tab-pane>

              <!-- 参数标签 -->
              <el-tab-pane label="参数" name="params">
                <div class="kv-grid" v-if="selectedStrategyForDetails && Object.keys(selectedStrategyForDetails.params || {}).length > 0">
                  <div
                    class="kv-item kv-item--param"
                    v-for="(value, key) in selectedStrategyForDetails.params"
                    :key="key"
                  >
                    <span class="kv-key kv-key--param">{{ getParamName(key) }}</span>
                    <span class="kv-value">{{ value }}</span>
                  </div>
                </div>
                <el-empty v-else description="暂无参数数据" :image-size="60" />
              </el-tab-pane>
            </el-tabs>
          </div>
        </div>
      </el-tab-pane>
    </el-tabs>

    <!-- 右键菜单 -->
    <el-dropdown
      trigger="contextmenu"
      :style="{
        position: 'fixed',
        left: contextMenuPosition.x + 'px',
        top: contextMenuPosition.y + 'px',
        display: contextMenuVisible ? 'block' : 'none'
      }"
    >
      <span></span>
      <template #dropdown>
        <el-dropdown-menu>
          <el-dropdown-item
            :icon="VideoPlay"
            @click="startStrategy(contextMenuStrategy?.strat_name)"
          >
            启动
          </el-dropdown-item>
          <el-dropdown-item
            :icon="VideoPause"
            @click="stopStrategy(contextMenuStrategy?.strat_name)"
          >
            停止
          </el-dropdown-item>
          <el-dropdown-item
            :icon="RefreshRight"
            @click="restartStrategy(contextMenuStrategy?.strat_name)"
          >
            重启
          </el-dropdown-item>
          <el-dropdown-item
            :icon="Delete"
            @click="deleteStrategy(contextMenuStrategy?.strat_name)"
            divided
          >
            删除
          </el-dropdown-item>
        </el-dropdown-menu>
      </template>
    </el-dropdown>

    <!-- 添加策略对话框 -->
    <el-dialog
      v-model="addStrategyDialogVisible"
      title="添加策略"
      width="600px"
    >
      <el-form :model="addStrategyForm" label-width="100px">
        <el-form-item label="选择策略">
          <el-select
            v-model="addStrategyForm.selectedConfigs"
            multiple
            placeholder="请选择策略配置文件"
            style="width: 100%"
            :loading="loadingConfigFiles"
          >
            <el-option
              v-for="config in availableConfigFiles"
              :key="config.filename"
              :label="config.name + ' (' + config.template + ')'"
              :value="config.filename"
            >
              <span>{{ config.name }}</span>
              <span style="float: right; color: #8492a6; font-size: 12px">{{ config.template }}</span>
            </el-option>
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <span class="dialog-footer">
          <el-button @click="addStrategyDialogVisible = false">取消</el-button>
          <el-button
            type="primary"
            @click="createStrategiesFromConfigs"
            :loading="creatingStrategies"
            :disabled="!addStrategyForm.selectedConfigs.length"
          >
            确定
          </el-button>
        </span>
      </template>
    </el-dialog>
  </div>
</template>

<script>
import { ref, onMounted, onUnmounted, computed, watch, nextTick } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { VideoPlay, VideoPause, RefreshRight, Refresh, Delete } from '@element-plus/icons-vue'
import request from '@/utils/request'
import { dictService } from '@/services/dictService'
import { subscribe, unsubscribe, isOpen } from '@/services/pushStream'

// 策略表推送 topic（全表一条，无 key）；服务端增量事件按 strat_name 合并
const PUSH_TOPIC_STRATEGIES = 'strategies'

export default {
  name: 'StrategyManager',
  setup() {
    const templates = ref([])
    const activeTab = ref('')
    const strategies = ref({})
    const templateConfigs = ref({})
    const selectedStrategies = ref([])
    const loading = ref(false)
    const contextMenuVisible = ref(false)
    const contextMenuPosition = ref({ x: 0, y: 0 })
    const contextMenuStrategy = ref(null)
    const selectedStrategyByTemplate = ref({})

    // 自动刷新相关
    const refreshInterval = ref(10)
    const refreshTimer = ref(null)

    // 策略详情相关
    const selectedStrategyForDetails = ref(null)
    const detailsTab = ref('orders')
    const ordersList = ref([])
    const tradesList = ref([])
    const positionsList = ref([])
    const logsList = ref([])
    const logsLoading = ref(false)
    const ordersLoading = ref(false)
    const tradesLoading = ref(false)
    const positionsLoading = ref(false)
    const dictLoaded = ref(false)

    // 表格行数统计
    const strategyTableRef = ref(null)
    const strategyFilteredCount = ref(null)
    const ordersTableRef = ref(null)
    const ordersFilteredCount = ref(null)
    const tradesTableRef = ref(null)
    const tradesFilteredCount = ref(null)
    const positionsTableRef = ref(null)
    const positionsFilteredCount = ref(null)

    const countDomRows = (tableInstance) => {
      // tableInstance may be an array when ref is inside v-for
      const instance = Array.isArray(tableInstance) ? tableInstance[0] : tableInstance
      const el = instance?.$el
      if (!el) return null
      return el.querySelectorAll('.el-table__body-wrapper tbody tr.el-table__row').length
    }

    const hasActiveFilters = (filters) =>
      Object.values(filters).some(v => Array.isArray(v) && v.length > 0)

    const handleStrategyFilterChange = (filters) => {
      if (!hasActiveFilters(filters)) { strategyFilteredCount.value = null; return }
      nextTick(() => {
        const ref = strategyTableRef.value
        strategyFilteredCount.value = countDomRows(ref)
      })
    }
    const handleOrdersFilterChange = (filters) => {
      if (!hasActiveFilters(filters)) { ordersFilteredCount.value = null; return }
      nextTick(() => { ordersFilteredCount.value = countDomRows(ordersTableRef.value) })
    }
    const handleTradesFilterChange = (filters) => {
      if (!hasActiveFilters(filters)) { tradesFilteredCount.value = null; return }
      nextTick(() => { tradesFilteredCount.value = countDomRows(tradesTableRef.value) })
    }
    const handlePositionsFilterChange = (filters) => {
      if (!hasActiveFilters(filters)) { positionsFilteredCount.value = null; return }
      nextTick(() => { positionsFilteredCount.value = countDomRows(positionsTableRef.value) })
    }

    // 添加策略相关
    const addStrategyDialogVisible = ref(false)
    const availableConfigFiles = ref([])
    const loadingConfigFiles = ref(false)
    const creatingStrategies = ref(false)
    const addStrategyForm = ref({
      selectedConfigs: []
    })

    // API 基础 URL（request 实例已配置 baseURL，这里保留作为路径前缀）
    const API_BASE = '/api'

    // 当前模板的策略列表
    const currentStrategies = computed(() => {
      return strategies.value[activeTab.value] || []
    })

    // 切换标签或数据刷新时重置筛选计数
    watch(activeTab, () => { strategyFilteredCount.value = null })
    watch(currentStrategies, () => { strategyFilteredCount.value = null })
    watch(ordersList, () => { ordersFilteredCount.value = null })
    watch(tradesList, () => { tradesFilteredCount.value = null })
    watch(positionsList, () => { positionsFilteredCount.value = null })

    // 获取模板标签（包含策略数量）
    const getTemplateLabel = (template) => {
      const templateStrategies = strategies.value[template] || []
      const totalCount = templateStrategies.length
      if (totalCount === 0) return template

      const runningCount = templateStrategies.filter(strategy => strategy.status === 1).length
      return `${template}(${runningCount}/${totalCount})`
    }

    // 当前模板的配置
    const templateConfig = computed(() => {
      return templateConfigs.value[activeTab.value] || { indicator: [], param: [] }
    })

    // 加载模板列表
    const loadTemplates = async () => {
      try {
        const response = await request.get(`${API_BASE}/template/list`)
        if (response.data.success) {
          templates.value = response.data.templates
          if (templates.value.length > 0) {
            activeTab.value = templates.value[0]
            await loadTemplateConfig(activeTab.value)
            // 并行加载所有模板的策略列表（用于显示标签数量）
            await loadAllTemplatesStrategies()
            selectStrategyForDetailsByTemplate(activeTab.value)
          }
        }
      } catch (error) {
        ElMessage.error('加载模板列表失败: ' + error.message)
      }
    }

    /**
     * 把后端返回的策略行统一成表格用的结构。
     * 接口（/api/strategy/list、/api/strategies）与推送事件（strategies topic）用同一份行结构，
     * 三条路径共用本函数，避免字段名（param/indicator vs params/indicators）各处不一致。
     */
    const mapStrategyRow = (strat) => ({
      id: strat.id || strat.strat_name,
      strat_name: strat.strat_name,
      strat_template: strat.strat_template,
      status: strat.status || 0,
      params: strat.param || {},
      indicators: strat.indicator || {},
      create_time: strat.create_time,
      update_time: strat.update_time
    })

    // 加载所有模板的策略列表
    const loadAllTemplatesStrategies = async () => {
      loading.value = true
      try {
        await Promise.all(templates.value.map(async (template) => {
          const response = await request.get(`${API_BASE}/strategy/list/${template}`)
          const strategiesList = response.data.strategies || []
          strategies.value[template] = strategiesList.map(mapStrategyRow)
        }))
      } catch (error) {
        ElMessage.error('加载策略列表失败: ' + error.message)
      } finally {
        loading.value = false
      }
    }

    // 加载模板配置
    const loadTemplateConfig = async (templateName) => {
      try {
        const response = await request.get(`${API_BASE}/template/config/${templateName}`)
        templateConfigs.value[templateName] = response.data
      } catch (error) {
        ElMessage.error('加载模板配置失败: ' + error.message)
      }
    }

    // 加载策略列表（silent=true 时不显示 loading 遮罩，避免自动刷新时闪烁）
    const loadStrategies = async (silent = false) => {
      if (!activeTab.value) return

      if (!silent) loading.value = true
      try {
        const response = await request.get(`${API_BASE}/strategy/list/${activeTab.value}`)

        // 解析策略数据
        const strategiesList = response.data.strategies || []

        // 解析实际的param和indicator数据
        strategies.value[activeTab.value] = strategiesList.map(mapStrategyRow)
        syncSelectedStrategyForDetails()
      } catch (error) {
        ElMessage.error('加载策略列表失败: ' + error.message)
      } finally {
        if (!silent) loading.value = false
      }
    }

    // ── 策略表推送（SSE `strategies` topic） ────────────────────────────────
    // 语义：订阅时/每 60s 一帧 full（全量替换各模板桶），其余为增量（按 strat_name 合并、
    // removed 删除）。增量绝不丢帧（服务端队列满会断流，重连后重新收全量）。
    /** 全量替换：按 strat_template 重建桶，保持已有模板的展示顺序 */
    const replaceAllStrategies = (rows) => {
      const next = {}
      templates.value.forEach(t => { next[t] = [] })
      rows.forEach(row => {
        const tpl = row.strat_template
        if (!tpl) return
        if (!next[tpl]) next[tpl] = []
        next[tpl].push(mapStrategyRow(row))
      })
      strategies.value = next
    }

    /** 增量合并：逐行 upsert（按 strat_name 定位），removed 从所有桶里删除 */
    const mergeStrategies = (rows, removed) => {
      const next = { ...strategies.value }
      rows.forEach(row => {
        const tpl = row.strat_template
        if (!tpl) return
        const item = mapStrategyRow(row)
        const list = [...(next[tpl] || [])]
        const idx = list.findIndex(s => s.strat_name === item.strat_name)
        if (idx >= 0) list[idx] = item
        else list.push(item)
        next[tpl] = list
      })
      if (removed && removed.length) {
        const gone = new Set(removed)
        Object.keys(next).forEach(tpl => {
          next[tpl] = (next[tpl] || []).filter(s => !gone.has(s.strat_name))
        })
      }
      strategies.value = next
    }

    /** 推送事件入口（event='strategies' 数据帧，'upstream_error' 上游失败） */
    const handleStrategiesEvent = (payload, event) => {
      if (event === 'upstream_error') {
        console.warn('[push] 策略表上游失败:', payload && (payload.message || payload.code))
        return
      }
      if (payload.full) replaceAllStrategies(payload.changed || [])
      else mergeStrategies(payload.changed || [], payload.removed || [])
      syncSelectedStrategyForDetails()
    }

    // 标签切换
    const handleTabChange = async (tabName) => {
      if (!templateConfigs.value[tabName]) {
        await loadTemplateConfig(tabName)
      }
      await loadStrategies()
      selectStrategyForDetailsByTemplate(tabName)
    }

    // 选择变化
    const handleSelectionChange = (selection) => {
      selectedStrategies.value = selection.map(item => item.strat_name)
    }

    // 启动策略
    const startStrategy = async (strategyId) => {
      try {
        // 经 web_server 转发到 gtrade HTTP gateway
        const response = await request.post(`/api/strategy/start/${strategyId}`)
        if (response.data.success) {
          ElMessage.success('启动成功')

          // 立即更新本地状态，创建全新对象以确保响应式更新
          const currentList = strategies.value[activeTab.value]
          if (currentList) {
            const strategyIndex = currentList.findIndex(s => s.strat_name === strategyId)
            if (strategyIndex !== -1) {
              console.log(`启动策略 ${strategyId}，更新状态: 0 -> 1`)

              // 创建新的策略列表，并为修改的策略创建新对象
              strategies.value[activeTab.value] = currentList.map((strat, index) => {
                if (index === strategyIndex) {
                  // 创建全新对象，确保响应式系统能检测到变化
                  return { ...strat, status: 1 }
                }
                return strat
              })

              // 使用 nextTick 确保 DOM 更新
              await nextTick()
              console.log(`策略 ${strategyId} 状态已更新，UI已刷新`)
            }
          }

          // 延迟3秒后从服务器重新加载，给gtrade时间更新数据库
          setTimeout(() => {
            console.log('延迟刷新：从服务器同步策略状态')
            loadStrategies()
          }, 3000)
        } else {
          ElMessage.error('启动失败: ' + (response.data.error || '未知错误'))
        }
      } catch (error) {
        ElMessage.error('启动失败: ' + (error.response?.data?.error || error.response?.data?.message || error.message))
      }
    }

    // 停止策略
    const stopStrategy = async (strategyId) => {
      try {
        // 经 web_server 转发到 gtrade HTTP gateway
        const response = await request.post(`/api/strategy/stop/${strategyId}`)
        if (response.data.success) {
          ElMessage.success('停止成功')

          // 立即更新本地状态，创建全新对象以确保响应式更新
          const currentList = strategies.value[activeTab.value]
          if (currentList) {
            const strategyIndex = currentList.findIndex(s => s.strat_name === strategyId)
            if (strategyIndex !== -1) {
              console.log(`停止策略 ${strategyId}，更新状态: 1 -> 0`)

              // 创建新的策略列表，并为修改的策略创建新对象
              strategies.value[activeTab.value] = currentList.map((strat, index) => {
                if (index === strategyIndex) {
                  // 创建全新对象，确保响应式系统能检测到变化
                  return { ...strat, status: 0 }
                }
                return strat
              })

              // 使用 nextTick 确保 DOM 更新
              await nextTick()
              console.log(`策略 ${strategyId} 状态已更新，UI已刷新`)
            }
          }

          // 延迟3秒后从服务器重新加载，给gtrade时间更新数据库
          // 如果配置了自动刷新，也会定期同步
          setTimeout(() => {
            console.log('延迟刷新：从服务器同步策略状态')
            loadStrategies()
          }, 3000)
        } else {
          ElMessage.error('停止失败: ' + (response.data.error || '未知错误'))
        }
      } catch (error) {
        ElMessage.error('停止失败: ' + (error.response?.data?.error || error.response?.data?.message || error.message))
      }
    }

    // 重启策略
    const restartStrategy = async (strategyId) => {
      try {
        // 经 web_server 转发到 gtrade HTTP gateway
        const response = await request.post(`/api/strategy/restart/${strategyId}`)
        if (response.data.success) {
          ElMessage.success('重启成功')

          // 立即更新本地状态，创建全新对象以确保响应式更新
          const currentList = strategies.value[activeTab.value]
          if (currentList) {
            const strategyIndex = currentList.findIndex(s => s.strat_name === strategyId)
            if (strategyIndex !== -1) {
              console.log(`重启策略 ${strategyId}，更新状态 -> 1`)

              // 创建新的策略列表，并为修改的策略创建新对象
              strategies.value[activeTab.value] = currentList.map((strat, index) => {
                if (index === strategyIndex) {
                  // 创建全新对象，确保响应式系统能检测到变化
                  return { ...strat, status: 1 }
                }
                return strat
              })

              // 使用 nextTick 确保 DOM 更新
              await nextTick()
              console.log(`策略 ${strategyId} 状态已更新，UI已刷新`)
            }
          }

          // 延迟3秒后从服务器重新加载，给gtrade时间更新数据库
          setTimeout(() => {
            console.log('延迟刷新：从服务器同步策略状态')
            loadStrategies()
          }, 3000)
        } else {
          ElMessage.error('重启失败: ' + (response.data.error || '未知错误'))
        }
      } catch (error) {
        ElMessage.error('重启失败: ' + (error.response?.data?.error || error.response?.data?.message || error.message))
      }
    }

    // 删除策略
    const deleteStrategy = async (strategyId) => {
      try {
        // 确认删除
        await ElMessageBox.confirm(
          `确定要删除策略 "${strategyId}" 吗？此操作不可恢复！`,
          '删除策略确认',
          {
            confirmButtonText: '确定删除',
            cancelButtonText: '取消',
            type: 'warning',
            confirmButtonClass: 'el-button--danger'
          }
        )

        // 调用 gtrade HTTP gateway 删除策略
        const response = await request.delete(`/api/strategy/delete/${strategyId}`)
        if (response.data.success) {
          ElMessage.success('删除成功')

          // 立即从服务器重新加载策略列表
          await loadStrategies()
        } else {
          ElMessage.error('删除失败: ' + (response.data.error || '策略可能不存在'))
        }
      } catch (error) {
        if (error !== 'cancel') {
          ElMessage.error('删除失败: ' + (error.response?.data?.error || error.response?.data?.message || error.message))
        }
      }
    }

    // 批量操作
    const batchOperation = async (operation) => {
      if (selectedStrategies.value.length === 0) {
        ElMessage.warning('请先选择策略')
        return
      }

      const operationText = {
        start: '启动',
        stop: '停止',
        restart: '重启'
      }[operation]

      try {
        await ElMessageBox.confirm(
          `确定要${operationText}选中的 ${selectedStrategies.value.length} 个策略吗?`,
          '批量操作确认',
          {
            confirmButtonText: '确定',
            cancelButtonText: '取消',
            type: 'warning'
          }
        )

        loading.value = true
        // 经 web_server 转发到 gtrade HTTP gateway
        const response = await request.post(`/api/strategy/batch`, {
          operation,
          strategy_ids: selectedStrategies.value
        })

        if (response.data.success) {
          const { success_count, fail_count } = response.data
          ElMessage.success(`${operationText}完成: 成功 ${success_count} 个, 失败 ${fail_count} 个`)
          await loadStrategies()
        } else {
          ElMessage.error(`批量${operationText}失败: ` + (response.data.error || '未知错误'))
        }
      } catch (error) {
        if (error !== 'cancel') {
          ElMessage.error(`批量${operationText}失败: ` + (error.response?.data?.error || error.response?.data?.message || error.message))
        }
      } finally {
        loading.value = false
      }
    }

    // 批量删除策略
    const batchDeleteStrategies = async () => {
      if (selectedStrategies.value.length === 0) {
        ElMessage.warning('请先选择策略')
        return
      }

      try {
        await ElMessageBox.confirm(
          `确定要删除选中的 ${selectedStrategies.value.length} 个策略吗？此操作不可恢复！`,
          '批量删除确认',
          {
            confirmButtonText: '确定删除',
            cancelButtonText: '取消',
            type: 'warning',
            confirmButtonClass: 'el-button--danger'
          }
        )

        loading.value = true
        let successCount = 0
        let failCount = 0

        // 逐个删除策略
        for (const strategyId of selectedStrategies.value) {
          try {
            const response = await request.delete(`/api/strategy/delete/${strategyId}`)
            if (response.data.success) {
              successCount++
            } else {
              failCount++
            }
          } catch (error) {
            failCount++
            console.error(`删除策略失败 ${strategyId}:`, error)
          }
        }

        // 显示结果
        if (successCount > 0) {
          ElMessage.success(`批量删除完成: 成功 ${successCount} 个, 失败 ${failCount} 个`)
        } else {
          ElMessage.warning(`批量删除失败: ${failCount} 个`)
        }

        // 刷新策略列表
        await loadStrategies()
      } catch (error) {
        if (error !== 'cancel') {
          ElMessage.error('批量删除失败: ' + (error.response?.data?.error || error.response?.data?.message || error.message))
        }
      } finally {
        loading.value = false
      }
    }

    // 右键菜单
    const handleContextMenu = (row, column, event) => {
      event.preventDefault()
      contextMenuStrategy.value = row
      contextMenuPosition.value = { x: event.clientX, y: event.clientY }
      contextMenuVisible.value = true

      // 点击其他地方关闭菜单
      const closeMenu = () => {
        contextMenuVisible.value = false
        document.removeEventListener('click', closeMenu)
      }
      setTimeout(() => {
        document.addEventListener('click', closeMenu)
      }, 100)
    }

    // 判断更新时间是否过期 (>20秒黄色, >60秒红色)
    const isUpdateStale = (updateTime) => {
      if (!updateTime) return false
      const updateDate = new Date(updateTime)
      const now = new Date()
      const diffSeconds = (now - updateDate) / 1000
      return diffSeconds > 20
    }

    // 获取更新时间的颜色 (>20秒黄色, >60秒红色)
    const getUpdateTimeColor = (updateTime) => {
      if (!updateTime) return ''
      const updateDate = new Date(updateTime)
      const now = new Date()
      const diffSeconds = (now - updateDate) / 1000

      if (diffSeconds > 60) {
        return '#ff4d4f'  // 红色
      } else if (diffSeconds > 20) {
        return '#faad14'  // 黄色
      }
      return ''  // 默认颜色
    }

    // 处理行点击事件
    const handleRowClick = (row) => {
      selectedStrategyForDetails.value = row
      selectedStrategyByTemplate.value[activeTab.value] = row.strat_name
      loadStrategyDetails(row.strat_name)
    }

    const clearStrategyDetails = () => {
      selectedStrategyForDetails.value = null
      ordersList.value = []
      tradesList.value = []
      positionsList.value = []
      logsList.value = []
    }

    const selectStrategyForDetailsByTemplate = (templateName) => {
      const list = strategies.value[templateName] || []
      if (list.length === 0) {
        clearStrategyDetails()
        return
      }

      const savedName = selectedStrategyByTemplate.value[templateName]
      const target = savedName ? list.find(item => item.strat_name === savedName) : list[0]

      if (!target) {
        clearStrategyDetails()
        return
      }

      selectedStrategyForDetails.value = target
      selectedStrategyByTemplate.value[templateName] = target.strat_name
      loadStrategyDetails(target.strat_name)
    }

    const syncSelectedStrategyForDetails = () => {
      if (!selectedStrategyForDetails.value) return
      const list = strategies.value[activeTab.value] || []
      const targetName = selectedStrategyForDetails.value.strat_name
      const match = list.find(item => item.strat_name === targetName)

      if (match) {
        selectedStrategyForDetails.value = match
        // 自动刷新时静默刷新当前激活的详情标签数据（不显示 loading 遮罩，避免闪烁）
        const strategyId = match.id || match.strat_name
        if (detailsTab.value === 'orders') {
          loadOrders(strategyId, true)
        } else if (detailsTab.value === 'trades') {
          loadTrades(strategyId, true)
        } else if (detailsTab.value === 'positions') {
          loadPositions(strategyId, true)
        } else if (detailsTab.value === 'logs') {
          loadLogs(strategyId, true)
        }
      } else {
        clearStrategyDetails()
      }
    }

    // 加载策略详情 (委托、成交、持仓)
    const loadStrategyDetails = async (strategyId) => {
      detailsTab.value = 'orders'
      await loadOrders(strategyId)
      await loadTrades(strategyId)
      await loadPositions(strategyId)
      await loadLogs(strategyId)
    }

    // 加载委托列表
    const loadOrders = async (strategyId, silent = false) => {
      if (!silent) ordersLoading.value = true
      try {
        const response = await request.get(`${API_BASE}/strategy/${strategyId}/orders`)
        ordersList.value = response.data.orders || []
      } catch (error) {
        ElMessage.error('加载委托列表失败: ' + error.message)
        if (!silent) ordersList.value = []
      } finally {
        if (!silent) ordersLoading.value = false
      }
    }

    // 加载成交列表
    const loadTrades = async (strategyId, silent = false) => {
      if (!silent) tradesLoading.value = true
      try {
        const response = await request.get(`${API_BASE}/strategy/${strategyId}/trades`)
        tradesList.value = response.data.trades || []
      } catch (error) {
        ElMessage.error('加载成交列表失败: ' + error.message)
        if (!silent) tradesList.value = []
      } finally {
        if (!silent) tradesLoading.value = false
      }
    }

    // 加载持仓列表
    const loadPositions = async (strategyId, silent = false) => {
      if (!silent) positionsLoading.value = true
      try {
        const response = await request.get(`${API_BASE}/strategy/${strategyId}/positions`)
        positionsList.value = response.data.positions || []
      } catch (error) {
        ElMessage.error('加载持仓列表失败: ' + error.message)
        if (!silent) positionsList.value = []
      } finally {
        if (!silent) positionsLoading.value = false
      }
    }

    // 加载策略日志
    const loadLogs = async (strategyId, silent = false) => {
      if (!silent) logsLoading.value = true
      try {
        const response = await request.get(`${API_BASE}/strategy/${strategyId}/logs`)
        logsList.value = response.data.logs || []
      } catch (error) {
        ElMessage.error('加载日志失败: ' + error.message)
        if (!silent) logsList.value = []
      } finally {
        if (!silent) logsLoading.value = false
      }
    }

    // 格式化纳秒时间戳
    const formatNanoTime = (nanoTime) => {
      if (!nanoTime) return '-'
      // 如果是字符串，先转换为BigInt再转换为毫秒
      // 如果是数字，直接转换（兼容旧数据）
      let milliseconds
      if (typeof nanoTime === 'string') {
        // 使用BigInt处理大整数，避免精度丢失
        milliseconds = Number(BigInt(nanoTime) / BigInt(1000000))
      } else {
        milliseconds = Math.floor(nanoTime / 1000000)
      }
      const date = new Date(milliseconds)
      return date.toLocaleString('zh-CN', { hour12: false })
    }

    const getEntrustStatusText = (status) => {
      if (dictLoaded.value) {
        return dictService.getName('EntrustStatus', status)
      }
      const fallback = {
        '0': '未报',
        '1': '正报',
        '2': '已报',
        '3': '部成',
        '4': '全部成交',
        '5': '已报待撤',
        '6': '场内撤单',
        '7': '部成待撤',
        '8': '部成部撤',
        '9': '废单',
        'I': '冻结',
        'A': '预埋单'
      }
      return fallback[String(status)] || String(status)
    }

    const getEntrustStatusType = (status) => {
      if (dictLoaded.value) {
        return dictService.getType('EntrustStatus', status)
      }
      return 'info'
    }

    const getEntrustStatusStyle = (status) => {
      const color = dictService.getEntrustStatusColor(status)
      return `background-color: ${color} !important; border-color: ${color} !important; color: #ffffff !important;`
    }

    const getEntrustStatusFilters = () => {
      const counts = {}
      ordersList.value.forEach(item => {
        const v = item.status
        if (v !== undefined && v !== null && v !== '') {
          const key = String(v)
          counts[key] = (counts[key] || 0) + 1
        }
      })
      const opts = Object.entries(counts).map(([v, count]) => ({
        text: `${getEntrustStatusText(v)}(${count})`,
        value: v
      }))
      if (opts.length > 0) return opts
      return dictLoaded.value ? dictService.getOptions('EntrustStatus').map(o => ({ text: o.label, value: String(o.value) })) : []
    }

    const getSideText = (side) => {
      // 使用数据字典服务获取买卖方向显示文本
      if (dictLoaded.value) {
        return dictService.getName('BuySellSide', side)
      }
      // 降级处理
      const upperSide = String(side).toUpperCase()
      return upperSide === 'B' ? '买' : '卖'
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

    const getSideFilters = (list = null, field = null) => {
      // 从数据字典获取买卖方向筛选选项
      let baseOptions
      if (dictLoaded.value) {
        const options = dictService.getOptions('BuySellSide')
        baseOptions = options.map(o => ({ text: o.label, value: o.value.toUpperCase() }))
      } else {
        baseOptions = [{ text: '买', value: 'B' }, { text: '卖', value: 'S' }]
      }
      if (!list || !field) return baseOptions
      // 统计各选项数量
      const counts = {}
      list.forEach(item => {
        const val = item[field]
        if (val !== undefined && val !== null) {
          const key = String(val).toUpperCase()
          counts[key] = (counts[key] || 0) + 1
        }
      })
      return baseOptions.map(opt => ({
        ...opt,
        text: counts[opt.value] !== undefined ? `${opt.text}(${counts[opt.value]})` : opt.text
      }))
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

    const getPosSideFilters = (list = null, field = null) => {
      // 从数据字典获取持仓方向筛选选项
      let baseOptions
      if (dictLoaded.value) {
        const options = dictService.getOptions('PosSide')
        baseOptions = options.map(o => ({ text: o.label, value: o.value }))
      } else {
        baseOptions = [{ text: '多', value: 'l' }, { text: '空', value: 's' }, { text: '净', value: 'n' }]
      }
      if (!list || !field) return baseOptions
      // 统计各选项数量
      const counts = {}
      list.forEach(item => {
        const val = item[field]
        if (val !== undefined && val !== null) {
          const key = String(val)
          counts[key] = (counts[key] || 0) + 1
        }
      })
      return baseOptions.map(opt => ({
        ...opt,
        text: counts[opt.value] !== undefined ? `${opt.text}(${counts[opt.value]})` : opt.text
      }))
    }

    // 粗略估算中英文混排的显示宽度，中文按 2 个字符处理
    const getDisplayLength = (text) => {
      return Array.from(String(text ?? '')).reduce((length, char) => {
        return length + (char.charCodeAt(0) > 255 ? 2 : 1)
      }, 0)
    }

    // 根据字段ID获取指标的中文名称，找不到时降级显示原始ID
    const getIndicatorName = (key) => {
      const found = templateConfig.value.indicator.find(i => i.id === key)
      return found?.name || key
    }

    // 根据字段ID获取参数的中文名称，找不到时降级显示原始ID
    const getParamName = (key) => {
      const found = templateConfig.value.param.find(p => p.id === key)
      return found?.name || key
    }

    // 根据列标题与当前数据内容自适应列宽
    const calcAdaptiveColumnWidth = (label, values, minWidth = 90, maxWidth = 420) => {
      const headerLength = getDisplayLength(label)
      const valueMaxLength = values.reduce((maxLength, value) => {
        const valueLength = getDisplayLength(value ?? '-')
        return Math.max(maxLength, valueLength)
      }, 0)
      const maxLength = Math.max(headerLength, valueMaxLength)
      const estimatedWidth = maxLength * 8 + 48
      return Math.max(minWidth, Math.min(maxWidth, estimatedWidth))
    }

    const getUpdateTimeColumnWidth = () => {
      const values = currentStrategies.value.map(strategy => strategy.update_time || '-')
      return calcAdaptiveColumnWidth('更新时间', values, 160, 220)
    }

    const getStatusColumnWidth = () => {
      const values = currentStrategies.value.map(strategy => strategy.status === 1 ? '运行中' : '已停止')
      return calcAdaptiveColumnWidth('状态', values, 100, 140)
    }

    // 根据参数定义获取列宽（自适应表头+内容）
    const getParamColumnWidth = (param) => {
      const label = param?.name || param?.id || ''
      const paramId = param?.id
      const values = currentStrategies.value.map(strategy => strategy.params?.[paramId] ?? '-')
      return calcAdaptiveColumnWidth(label, values)
    }

    // 根据指标定义获取列宽（自适应表头+内容）
    const getIndicatorColumnWidth = (indicator) => {
      const label = indicator?.name || indicator?.id || ''
      const indicatorId = indicator?.id
      const values = currentStrategies.value.map(strategy => strategy.indicators?.[indicatorId] ?? '-')
      return calcAdaptiveColumnWidth(label, values)
    }

    // 自动刷新间隔变化处理
    const handleRefreshIntervalChange = (interval) => {
      // 保存到 localStorage
      localStorage.setItem('strategyRefreshInterval', interval)

      // 清除旧定时器
      if (refreshTimer.value) {
        clearInterval(refreshTimer.value)
        refreshTimer.value = null
      }

      // 设置新定时器（降级用：推送正常时由 SSE 增量推送更新，这里直接跳过，避免多余请求）
      if (interval > 0) {
        refreshTimer.value = setInterval(() => {
          if (isOpen()) return
          loadStrategies(true)  // 静默刷新，不显示 loading 遮罩
        }, interval * 1000)
      }
    }

    // 按指标排序
    const sortByIndicator = (a, b, indicatorId) => {
      const aVal = a.indicators?.[indicatorId] ?? ''
      const bVal = b.indicators?.[indicatorId] ?? ''

      // 尝试转换为数字进行比较
      const aNum = Number(aVal)
      const bNum = Number(bVal)

      if (!isNaN(aNum) && !isNaN(bNum)) {
        return aNum - bNum
      }

      // 字符串比较
      return String(aVal).localeCompare(String(bVal))
    }

    // 获取指标的筛选选项（带计数）
    const getIndicatorFilters = (indicatorId) => {
      const countMap = new Map()
      currentStrategies.value.forEach(strategy => {
        const val = strategy.indicators?.[indicatorId]
        if (val !== undefined && val !== null && val !== '') {
          const key = String(val)
          if (!countMap.has(key)) countMap.set(key, { value: val, count: 0 })
          countMap.get(key).count++
        }
      })
      return Array.from(countMap.values()).map(({ value: val, count }) => ({
        text: `${String(val)}(${count})`,
        value: val
      }))
    }

    // 按指标筛选
    const filterByIndicator = (value, row, indicatorId) => {
      const cellValue = row.indicators?.[indicatorId]
      return cellValue === value
    }

    // 按参数排序
    const sortByParam = (a, b, paramId) => {
      const aVal = a.params?.[paramId] ?? ''
      const bVal = b.params?.[paramId] ?? ''

      // 尝试转换为数字进行比较
      const aNum = Number(aVal)
      const bNum = Number(bVal)

      if (!isNaN(aNum) && !isNaN(bNum)) {
        return aNum - bNum
      }

      // 字符串比较
      return String(aVal).localeCompare(String(bVal))
    }

    // 获取参数的筛选选项（带计数）
    const getParamFilters = (paramId) => {
      const countMap = new Map()
      currentStrategies.value.forEach(strategy => {
        const val = strategy.params?.[paramId]
        if (val !== undefined && val !== null && val !== '') {
          const key = String(val)
          if (!countMap.has(key)) countMap.set(key, { value: val, count: 0 })
          countMap.get(key).count++
        }
      })
      return Array.from(countMap.values()).map(({ value: val, count }) => ({
        text: `${String(val)}(${count})`,
        value: val
      }))
    }

    // 按参数筛选
    const filterByParam = (value, row, paramId) => {
      const cellValue = row.params?.[paramId]
      return cellValue === value
    }

    // 获取策略状态筛选选项（带计数）
    const getStatusFilters = () => {
      const counts = { 1: 0, 0: 0 }
      currentStrategies.value.forEach(s => {
        if (s.status === 1) counts[1]++
        else counts[0]++
      })
      return [
        { text: `运行中(${counts[1]})`, value: 1 },
        { text: `已停止(${counts[0]})`, value: 0 }
      ]
    }

    // 按状态筛选
    const filterStatus = (value, row) => {
      return row.status === value
    }

    // 显示添加策略对话框
    const showAddStrategyDialog = async () => {
      addStrategyDialogVisible.value = true
      addStrategyForm.value.selectedConfigs = []
      await loadAvailableConfigFiles()
    }

    // 加载可用的配置文件列表
    const loadAvailableConfigFiles = async () => {
      loadingConfigFiles.value = true
      try {
        const response = await request.get(`${API_BASE}/strategy/config_files`)
        if (response.data.success) {
          availableConfigFiles.value = response.data.data
        } else {
          ElMessage.error('加载配置文件列表失败')
        }
      } catch (error) {
        ElMessage.error('加载配置文件列表失败: ' + error.message)
      } finally {
        loadingConfigFiles.value = false
      }
    }

    // 从配置文件批量创建策略
    const createStrategiesFromConfigs = async () => {
      if (addStrategyForm.value.selectedConfigs.length === 0) {
        ElMessage.warning('请至少选择一个配置文件')
        return
      }

      creatingStrategies.value = true
      try {
        const response = await request.post(`${API_BASE}/strategies/batch_from_config`, {
          config_files: addStrategyForm.value.selectedConfigs
        })

        if (response.data.success) {
          const result = response.data.data
          const successCount = result.success_count
          const failCount = result.fail_count

          if (successCount > 0) {
            ElMessage.success(`成功创建 ${successCount} 个策略${failCount > 0 ? `，失败 ${failCount} 个` : ''}`)
          } else {
            ElMessage.warning(`创建失败: ${failCount} 个`)
          }

          // 关闭对话框
          addStrategyDialogVisible.value = false

          // 显示失败的详细信息（先关闭对话框，再显示 alert，避免 overlay 残留）
          if (failCount > 0 && result.failed.length > 0) {
            const failedMessages = result.failed.map(f => `${f.filename}: ${f.error}`).join('\n')
            await ElMessageBox.alert(failedMessages, '创建失败详情', {
              confirmButtonText: '确定',
              type: 'warning'
            })
          }

          // 如果有成功创建的策略，切换到对应的模板标签页并刷新
          if (successCount > 0 && result.created && result.created.length > 0) {
            // 获取第一个成功创建的策略对应的模板
            const firstCreatedFilename = result.created[0].filename
            const configInfo = availableConfigFiles.value.find(c => c.filename === firstCreatedFilename)

            if (configInfo && configInfo.template) {
              const targetTemplate = configInfo.template

              // 检查模板是否已存在，如果不存在需要重新加载模板列表
              if (!templates.value.includes(targetTemplate)) {
                // 重新加载模板列表
                const templateResponse = await request.get(`${API_BASE}/template/list`)
                if (templateResponse.data.success) {
                  templates.value = templateResponse.data.templates
                }
              }

              // 切换到新策略所属的模板标签页
              if (targetTemplate !== activeTab.value) {
                activeTab.value = targetTemplate
              }

              // 加载该模板的配置（如果尚未加载）
              if (!templateConfigs.value[targetTemplate]) {
                await loadTemplateConfig(targetTemplate)
              }
            }
          }

          // 刷新策略列表
          await loadStrategies()
        } else {
          ElMessage.error('批量创建策略失败: ' + (response.data.error || '未知错误'))
        }
      } catch (error) {
        ElMessage.error('批量创建策略失败: ' + error.message)
      } finally {
        creatingStrategies.value = false
      }
    }

    // 获取唯一值的筛选选项（通用方法，带计数）
    const getUniqueFilters = (list, prop) => {
      const countMap = new Map()
      list.forEach(item => {
        const val = item[prop]
        if (val !== undefined && val !== null && val !== '') {
          const key = String(val)
          if (!countMap.has(key)) countMap.set(key, { value: val, count: 0 })
          countMap.get(key).count++
        }
      })
      return Array.from(countMap.values()).map(({ value: val, count }) => ({
        text: `${String(val)}(${count})`,
        value: val
      }))
    }

    // 监听 detailsTab 变化, 自动加载对应数据
    watch(detailsTab, (newTab) => {
      if (!selectedStrategyForDetails.value) return

      const strategyId = selectedStrategyForDetails.value.id || selectedStrategyForDetails.value.strat_name

      if (newTab === 'orders' && ordersList.value.length === 0) {
        loadOrders(strategyId)
      } else if (newTab === 'trades' && tradesList.value.length === 0) {
        loadTrades(strategyId)
      } else if (newTab === 'positions' && positionsList.value.length === 0) {
        loadPositions(strategyId)
      } else if (newTab === 'logs' && logsList.value.length === 0) {
        loadLogs(strategyId)
      }
    })

    onMounted(() => {
      loadTemplates()
      dictService.load().then(() => { dictLoaded.value = true }).catch(() => { dictLoaded.value = true })

      // 策略表推送：订阅后服务端立即回一帧全量，之后按行增量推送（refreshInterval 降级为兜底轮询）
      subscribe(PUSH_TOPIC_STRATEGIES, handleStrategiesEvent)

      // 从 localStorage 恢复刷新间隔，默认10秒
      const savedInterval = localStorage.getItem('strategyRefreshInterval')
      if (savedInterval !== null) {
        refreshInterval.value = parseInt(savedInterval)
      }
      handleRefreshIntervalChange(refreshInterval.value)

      // 修改筛选面板的 Confirm 按钮为 Revert 反选按钮
      const initFilterButtons = () => {
        // 定时检查并修改筛选面板按钮
        const checkAndModifyButtons = () => {
          // 查找所有筛选面板中的 Confirm 按钮（多种选择器）
          const filterBottoms = document.querySelectorAll('.el-table-filter__bottom')

          filterBottoms.forEach(bottom => {
            // 查找包含 "Confirm" 文本的按钮
            const buttons = bottom.querySelectorAll('button')
            buttons.forEach(confirmBtn => {
              if (confirmBtn.textContent.includes('Confirm') && !confirmBtn.dataset.revertInitialized) {
                // 标记已初始化
                confirmBtn.dataset.revertInitialized = 'true'

                console.log('初始化 Revert 按钮', confirmBtn)

                // 获取筛选面板的复选框组
                const filterWrap = confirmBtn.closest('.el-table-filter__wrap') || confirmBtn.closest('.el-popper')
                const checkboxGroup = filterWrap?.querySelector('.el-table-filter__checkbox-group')

                // 保留原始按钮但隐藏（用于自动应用筛选）
                confirmBtn.style.display = 'none'
                confirmBtn.dataset.originalConfirm = 'true'

                // 创建新的 Revert 按钮
                const revertBtn = document.createElement('button')
                revertBtn.textContent = 'Revert'
                revertBtn.className = confirmBtn.className
                revertBtn.classList.remove('is-disabled')
                revertBtn.type = 'button'
                revertBtn.dataset.revertInitialized = 'true'

                // 插入新按钮
                confirmBtn.parentNode?.insertBefore(revertBtn, confirmBtn)

                // 为 Revert 按钮添加点击事件
                revertBtn.addEventListener('click', (e) => {
                  e.preventDefault()
                  e.stopPropagation()

                  console.log('点击 Revert 按钮')

                  if (checkboxGroup) {
                    // 真正的反选：切换所有复选框状态
                    const checkboxes = checkboxGroup.querySelectorAll('.el-checkbox')

                    // 临时禁用自动应用筛选，避免每次切换都触发
                    const inputs = checkboxGroup.querySelectorAll('.el-checkbox__input input')
                    inputs.forEach(input => {
                      input.dataset.skipAutoFilter = 'true'
                    })

                    // 反选所有复选框
                    checkboxes.forEach(checkbox => {
                      const input = checkbox.querySelector('input[type="checkbox"]')
                      if (input) {
                        input.click() // 通过点击来触发反选
                      }
                    })

                    // 恢复自动应用筛选
                    setTimeout(() => {
                      inputs.forEach(input => {
                        delete input.dataset.skipAutoFilter
                      })
                    }, 100)
                  }
                }, true)

                // 添加复选框变化监听，实现勾选后自动应用筛选（不关闭面板）
                if (checkboxGroup) {
                  const checkboxes = checkboxGroup.querySelectorAll('.el-checkbox__input input')
                  checkboxes.forEach(checkbox => {
                    if (!checkbox.dataset.autoFilterInitialized) {
                      checkbox.dataset.autoFilterInitialized = 'true'

                      checkbox.addEventListener('change', () => {
                        // 检查是否应该跳过自动筛选（例如在反选时）
                        if (checkbox.dataset.skipAutoFilter) {
                          console.log('跳过自动应用筛选')
                          return
                        }

                        console.log('复选框状态改变，准备自动应用筛选（不关闭面板）')

                        // 查找隐藏的原始确认按钮
                        const originalBtn = bottom.querySelector('button[data-original-confirm="true"]')

                        if (originalBtn) {
                          console.log('找到原始确认按钮，自动点击')

                          // 在应用筛选之前，找到当前活动的筛选触发器
                          // 方法1: 查找所有筛选触发器，找到有活动类的那个
                          let activeTrigger = null
                          const allTriggers = document.querySelectorAll('.el-table__column-filter-trigger')

                          allTriggers.forEach(trigger => {
                            // Element Plus 在打开筛选时会给触发器添加特定类或属性
                            // 检查是否有相关的 popper 是可见的
                            const ariaDescribedBy = trigger.getAttribute('aria-describedby')
                            if (ariaDescribedBy) {
                              const popper = document.getElementById(ariaDescribedBy)
                              if (popper && popper.style.display !== 'none' && popper.offsetParent !== null) {
                                activeTrigger = trigger
                                console.log('找到活动的筛选触发器')
                              }
                            }
                          })

                          // 如果上述方法没找到，尝试从面板向上查找到表格列，再找到对应的触发器
                          if (!activeTrigger) {
                            const filterWrap = originalBtn.closest('.el-table-filter__wrap') || originalBtn.closest('.el-popper')
                            if (filterWrap) {
                              const popperId = filterWrap.id
                              if (popperId) {
                                activeTrigger = document.querySelector(`[aria-describedby="${popperId}"]`)
                                console.log('通过 popper ID 找到触发器')
                              }
                            }
                          }

                          // 点击确认按钮应用筛选
                          originalBtn.click()

                          // 等待面板关闭后重新打开特定的筛选面板
                          if (activeTrigger) {
                            setTimeout(() => {
                              console.log('重新打开特定的筛选面板')
                              activeTrigger.click()
                            }, 100)
                          } else {
                            console.log('未找到活动的筛选触发器')
                          }
                        } else {
                          console.log('未找到原始确认按钮')
                        }
                      })
                    }
                  })
                }
              }
            })

            // 将 Reset 按钮改为全选按钮
            buttons.forEach(resetBtn => {
              if (resetBtn.textContent.trim() === 'Reset' && !resetBtn.dataset.selectAllInitialized) {
                resetBtn.dataset.selectAllInitialized = 'true'
                resetBtn.style.display = 'none'

                const filterWrap = resetBtn.closest('.el-table-filter__wrap') || resetBtn.closest('.el-popper')
                const checkboxGroup = filterWrap?.querySelector('.el-table-filter__checkbox-group')

                const selectAllBtn = document.createElement('button')
                selectAllBtn.textContent = '全选'
                selectAllBtn.className = resetBtn.className
                selectAllBtn.type = 'button'
                selectAllBtn.dataset.selectAllInitialized = 'true'

                resetBtn.parentNode?.insertBefore(selectAllBtn, resetBtn)

                selectAllBtn.addEventListener('click', (e) => {
                  e.preventDefault()
                  e.stopPropagation()

                  if (checkboxGroup) {
                    const inputs = checkboxGroup.querySelectorAll('.el-checkbox__input input')
                    // 临时禁用自动应用，避免每次点击都触发
                    inputs.forEach(input => { input.dataset.skipAutoFilter = 'true' })
                    // 勾选所有未选中的复选框
                    inputs.forEach(input => {
                      if (!input.checked) input.click()
                    })
                    // 恢复后统一应用筛选
                    setTimeout(() => {
                      inputs.forEach(input => delete input.dataset.skipAutoFilter)
                      const originalConfirmBtn = bottom.querySelector('button[data-original-confirm="true"]')
                      if (originalConfirmBtn) originalConfirmBtn.click()
                    }, 50)
                  }
                }, true)
              }
            })
          })
        }

        // 立即执行一次
        checkAndModifyButtons()

        // 定时检查（每100ms检查一次）
        const intervalId = setInterval(checkAndModifyButtons, 100)

        // 使用 MutationObserver 监听 DOM 变化
        const observer = new MutationObserver(() => {
          checkAndModifyButtons()
        })

        observer.observe(document.body, {
          childList: true,
          subtree: true,
          attributes: true,
          attributeFilter: ['style', 'class']
        })

        // 存储 intervalId 以便后续清理
        window._filterButtonInterval = intervalId
      }

      initFilterButtons()
    })

    onUnmounted(() => {
      // 清除定时器
      if (refreshTimer.value) {
        clearInterval(refreshTimer.value)
      }

      // 清除筛选按钮检查定时器
      if (window._filterButtonInterval) {
        clearInterval(window._filterButtonInterval)
      }

      // 退订策略表推送（引用计数归零后服务端停止查库；SSE 连接由单例保留，其他页面不受影响）
      unsubscribe(PUSH_TOPIC_STRATEGIES, handleStrategiesEvent)
    })

    return {
      templates,
      activeTab,
      currentStrategies,
      templateConfig,
      selectedStrategies,
      loading,
      contextMenuVisible,
      contextMenuPosition,
      contextMenuStrategy,
      refreshInterval,
      selectedStrategyForDetails,
      detailsTab,
      ordersList,
      tradesList,
      positionsList,
      logsList,
      logsLoading,
      loadLogs,
      ordersLoading,
      tradesLoading,
      positionsLoading,
      addStrategyDialogVisible,
      availableConfigFiles,
      loadingConfigFiles,
      creatingStrategies,
      addStrategyForm,
      getTemplateLabel,
      handleTabChange,
      handleSelectionChange,
      handleRowClick,
      startStrategy,
      stopStrategy,
      restartStrategy,
      deleteStrategy,
      batchOperation,
      batchDeleteStrategies,
      handleContextMenu,
      loadStrategies,
      isUpdateStale,
      getUpdateTimeColor,
      formatNanoTime,
      getEntrustStatusText,
      getEntrustStatusType,
      getEntrustStatusStyle,
      getEntrustStatusFilters,
      getSideText,
      getSideType,
      getSideFilters,
      getPosSideName,
      getPosSideType,
      getPosSideFilters,
      getUpdateTimeColumnWidth,
      getStatusColumnWidth,
      getParamColumnWidth,
      getIndicatorColumnWidth,
      calcAdaptiveColumnWidth,
      getIndicatorName,
      getParamName,
      handleRefreshIntervalChange,
      sortByIndicator,
      getIndicatorFilters,
      filterByIndicator,
      sortByParam,
      getParamFilters,
      filterByParam,
      getStatusFilters,
      filterStatus,
      getUniqueFilters,
      strategyTableRef,
      strategyFilteredCount,
      handleStrategyFilterChange,
      ordersTableRef,
      ordersFilteredCount,
      handleOrdersFilterChange,
      tradesTableRef,
      tradesFilteredCount,
      handleTradesFilterChange,
      positionsTableRef,
      positionsFilteredCount,
      handlePositionsFilterChange,
      showAddStrategyDialog,
      loadAvailableConfigFiles,
      createStrategiesFromConfigs,
      VideoPlay,
      VideoPause,
      RefreshRight,
      Refresh,
      Delete
    }
  }
}
</script>

<style scoped>
.status-tag {
  display: inline-block;
  padding: 0 8px;
  height: 24px;
  line-height: 24px;
  border-radius: 4px;
  font-size: 12px;
  color: #ffffff;
}

.strategy-manager {
  width: 100%;
}

.table-count-bar {
  font-size: 13px;
  color: #909399;
  margin-top: 16px;
}

.header-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  height: 60px;
  background-color: #1e1e1e;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.3);
  margin-bottom: 20px;
}

.page-title {
  margin: 0;
  font-size: 20px;
  font-weight: bold;
  color: #fff;
}

.header-controls {
  display: flex;
  align-items: center;
  gap: 20px;
  flex-shrink: 0;
}

.auto-refresh-config {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-shrink: 0;
}

.auto-refresh-config span {
  font-size: 14px;
  color: #e0e0e0;
  white-space: nowrap;
}

.main-tabs {
  padding: 0 20px;
}

.tab-content {
  padding: 20px 0;
}

.operation-buttons {
  display: flex;
  gap: 10px;
  margin-bottom: 20px;
}

.strategy-details {
  margin-top: 30px;
  padding: 20px;
  background: #1a1a1a;
  border-radius: 4px;
  border: 1px solid #333;
}

.strategy-details h3 {
  margin: 0 0 15px 0;
  font-size: 16px;
  font-weight: 600;
  color: #e0e0e0;
}

/* 策略详情内的 el-tabs (border-card) */
.strategy-details :deep(.el-tabs--border-card) {
  background: #1e1e1e;
  border-color: #3a3a3a;
}

.strategy-details :deep(.el-tabs--border-card > .el-tabs__header) {
  background: #252525;
  border-bottom-color: #3a3a3a;
}

.strategy-details :deep(.el-tabs--border-card > .el-tabs__header .el-tabs__item) {
  color: #909090;
  border-color: transparent;
}

.strategy-details :deep(.el-tabs--border-card > .el-tabs__header .el-tabs__item.is-active) {
  background: #1e1e1e;
  color: #e0e0e0;
  border-top-color: #409eff;
  border-right-color: #3a3a3a;
  border-left-color: #3a3a3a;
}

.strategy-details :deep(.el-tabs--border-card > .el-tabs__header .el-tabs__item:hover) {
  color: #c0c0c0;
}

.strategy-details :deep(.el-tabs--border-card > .el-tabs__content) {
  background: #1e1e1e;
  padding: 12px;
}

/* 策略详情内的 el-table */
.strategy-details :deep(.el-table) {
  background-color: #1e1e1e;
  color: #d0d0d0;
}

.strategy-details :deep(.el-table tr) {
  background-color: #1e1e1e;
}

.strategy-details :deep(.el-table th.el-table__cell) {
  background-color: #252525;
  color: #a0a0a0;
  border-bottom-color: #3a3a3a;
}

.strategy-details :deep(.el-table td.el-table__cell) {
  border-bottom-color: #2e2e2e;
}

.strategy-details :deep(.el-table--striped .el-table__body tr.el-table__row--striped td.el-table__cell) {
  background-color: #222222;
}

.strategy-details :deep(.el-table__body tr:hover > td.el-table__cell) {
  background-color: #2a2a2a;
}

.strategy-details :deep(.el-table--border::after),
.strategy-details :deep(.el-table--border::before),
.strategy-details :deep(.el-table__inner-wrapper::before) {
  background-color: #3a3a3a;
}

.strategy-details :deep(.el-table--border .el-table__cell) {
  border-right-color: #2e2e2e;
}

.strategy-details :deep(.el-table__empty-text) {
  color: #606060;
}

/* 空状态 */
.strategy-details :deep(.el-empty__description p) {
  color: #606060;
}

.operation-btn-group {
  display: flex;
  gap: 4px;
  justify-content: center;
  flex-wrap: nowrap;
}

.kv-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 8px;
  padding: 8px 0;
}

.kv-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 12px;
  background: #252525;
  border: 1px solid #3a3a3a;
  border-radius: 4px;
  min-width: 0;
}

.kv-key {
  font-weight: 600;
  color: #909090;
  white-space: nowrap;
  flex-shrink: 0;
  font-size: 13px;
}

.kv-key::after {
  content: ':';
}

.kv-value {
  color: #d0d0d0;
  font-size: 13px;
  word-break: break-all;
}

/* 指标项：蓝色边框 */
.kv-item--indicator {
  border-color: #1e3a52;
}

.kv-key--indicator {
  color: #5b9bd5;
}

/* 参数项：橙色边框 */
.kv-item--param {
  border-color: #4a2e10;
}

.kv-key--param {
  color: #c8914a;
}

/* 指标列表头：蓝色系 */
:deep(th.el-table__cell.indicator-col-header) {
  background-color: #1a2a3a !important;
  color: #5b9bd5 !important;
}

/* 参数列表头：橙色系 */
:deep(th.el-table__cell.param-col-header) {
  background-color: #2a1f10 !important;
  color: #c8914a !important;
}

.operation-btn-group .el-button {
  padding: 5px 8px;
  font-size: 12px;
}

/* 禁用按钮样式 - 显示为灰色 */
.el-button.is-disabled,
.el-button.is-disabled:hover,
.el-button.is-disabled:focus,
.el-button.is-disabled:active {
  background-color: #4a4a4a !important;
  border-color: #555555 !important;
  color: #808080 !important;
  opacity: 0.6;
  cursor: not-allowed;
}

.el-button--success.is-disabled,
.el-button--success.is-disabled:hover,
.el-button--success.is-disabled:focus,
.el-button--success.is-disabled:active {
  background-color: #4a4a4a !important;
  border-color: #555555 !important;
  color: #808080 !important;
}

.el-button--danger.is-disabled,
.el-button--danger.is-disabled:hover,
.el-button--danger.is-disabled:focus,
.el-button--danger.is-disabled:active {
  background-color: #4a4a4a !important;
  border-color: #555555 !important;
  color: #808080 !important;
}

.el-button--warning.is-disabled,
.el-button--warning.is-disabled:hover,
.el-button--warning.is-disabled:focus,
.el-button--warning.is-disabled:active {
  background-color: #4a4a4a !important;
  border-color: #555555 !important;
  color: #808080 !important;
}

.el-button--primary.is-disabled,
.el-button--primary.is-disabled:hover,
.el-button--primary.is-disabled:focus,
.el-button--primary.is-disabled:active {
  background-color: #4a4a4a !important;
  border-color: #555555 !important;
  color: #808080 !important;
}

/* 筛选面板优化 - 保留底部按钮区域 */
:deep(.el-table-filter__checkbox-group) {
  padding-bottom: 8px !important;
}

:deep(.el-table-filter__content) {
  padding-bottom: 8px !important;
}

/* 筛选面板底部按钮样式 */
:deep(.el-table-filter__bottom) {
  padding: 8px;
  border-top: 1px solid #404040;
}

:deep(.el-table-filter__bottom .el-button) {
  margin: 0 4px;
}
</style>
