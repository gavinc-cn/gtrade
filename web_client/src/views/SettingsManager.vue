<template>
  <div class="settings-manager">
    <el-card class="settings-card" shadow="never">
      <template #header>
        <div class="card-head">
          <span class="card-title">设置 / 标的范围</span>
          <el-button type="primary" size="small" :loading="loading" @click="loadData">
            <el-icon><Refresh /></el-icon>
            刷新
          </el-button>
        </div>
      </template>

      <div class="toolbar">
        <el-input
          v-model="keyword"
          placeholder="搜索标的（不区分大小写）"
          clearable
          class="keyword-input"
        >
          <template #prefix>
            <el-icon><Search /></el-icon>
          </template>
        </el-input>
        <el-select v-model="typeFilter" class="type-select" placeholder="标的类型">
          <el-option label="全部类型" value="ALL" />
          <el-option v-for="t in typeOptions" :key="t" :label="t" :value="t" />
        </el-select>
        <el-select v-model="statusFilter" class="status-select" placeholder="订阅状态">
          <el-option
            v-for="s in STATUS_OPTIONS"
            :key="s.value"
            :label="s.label"
            :value="s.value"
          />
        </el-select>
        <el-button @click="selectAllVisible">全选筛选结果</el-button>
        <el-button @click="clearVisible">清空筛选结果</el-button>
      </div>

      <div class="summary-bar">
        已订阅 {{ checked.length }} / 共 {{ instruments.length }} 个标的
        <span class="muted">· 策略在用 {{ strategyUsedCount }} 个（保存时未勾选但策略在用的标的会保留）</span>
      </div>

      <div v-loading="loading" class="panes">
        <!-- 左：全量标的清单（筛选 + 勾选=订阅） -->
        <section class="pane pane-all">
          <div class="pane-head">
            <span class="pane-title">标的清单</span>
            <span class="pane-sub">{{ filtered.length }} 个</span>
          </div>
          <div class="list-wrap">
            <el-empty
              v-if="!loading && filtered.length === 0"
              :description="emptyText"
            />
            <el-checkbox-group v-else v-model="checked" class="inst-group">
              <el-checkbox v-for="i in filtered" :key="keyOf(i)" :value="keyOf(i)" class="inst-item">
                <span class="inst-id">{{ i.inst_id }}</span>
                <el-tag size="small" type="info" class="inst-meta">{{ i.market }}</el-tag>
                <el-tag size="small" class="inst-meta">{{ i.inst_type || '-' }}</el-tag>
                <el-tag v-if="checkedSet.has(keyOf(i))" size="small" type="success" class="inst-meta">
                  已订阅
                </el-tag>
                <el-tag v-else size="small" type="info" class="inst-meta">
                  未订阅
                </el-tag>
                <el-tag v-if="strategyCountOf(keyOf(i))" size="small" type="warning" class="inst-meta">
                  策略使用中（{{ strategyCountOf(keyOf(i)) }}）
                </el-tag>
              </el-checkbox>
            </el-checkbox-group>
          </div>
        </section>

        <!-- 右：已订阅（= 当前勾选集合，独立于左侧筛选，可单条移除） -->
        <section class="pane pane-subscribed">
          <div class="pane-head">
            <span class="pane-title">已订阅</span>
            <span class="pane-sub">{{ subscribedRows.length }} 个</span>
            <el-button
              v-if="subscribedRows.length"
              link
              type="danger"
              size="small"
              class="pane-action"
              @click="clearAllSubscribed"
            >清空已订阅</el-button>
          </div>
          <div class="list-wrap">
            <el-empty
              v-if="!subscribedRows.length"
              description="尚未订阅任何标的"
              :image-size="60"
            />
            <ul v-else class="sub-list">
              <li v-for="row in subscribedRows" :key="row.key" class="sub-item">
                <div class="sub-main">
                  <span class="inst-id">{{ row.inst_id }}</span>
                  <span class="sub-tags">
                    <el-tag size="small" type="info" class="inst-meta">{{ row.market }}</el-tag>
                    <el-tag size="small" class="inst-meta">{{ row.inst_type || '-' }}</el-tag>
                    <el-tag v-if="row.strategyCount" size="small" type="warning" class="inst-meta">
                      策略使用中（{{ row.strategyCount }}）
                    </el-tag>
                    <el-tag v-if="row.missing" size="small" type="danger" class="inst-meta">
                      不在当前清单
                    </el-tag>
                  </span>
                </div>
                <el-button
                  link
                  type="danger"
                  size="small"
                  class="sub-remove"
                  @click="unsubscribe(row.key)"
                >移除</el-button>
              </li>
            </ul>
          </div>
        </section>
      </div>

      <div class="save-bar">
        <el-button type="primary" :loading="saving" @click="save">保存</el-button>
      </div>
    </el-card>
  </div>
</template>

<script>
import { ref, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { getInstrumentScope, setInstrumentScope } from '@/services/settingsService'
import {
  STATUS_ALL,
  STATUS_OPTIONS,
  buildStrategyCounts,
  buildSubscribedRows,
  filterInstruments,
  scopeKey
} from '@/utils/instrumentScope'

export default {
  name: 'SettingsManager',
  setup() {
    const loading = ref(false)
    const saving = ref(false)
    const loadError = ref('')
    const instruments = ref([])                 // [{market, inst_id, inst_type}]
    const checked = ref([])                     // 选择键数组：当前勾选 = 待保存订阅范围
    const owned = ref([])                       // 引擎返回的 owner 行 [{market, inst_id, inst_type, scope, strategies}]
    const keyword = ref('')
    const typeFilter = ref('ALL')
    const statusFilter = ref(STATUS_ALL)

    // 键构造、筛选、右栏展开、策略计数都走 utils/instrumentScope.js 的纯函数（带 node --test 用例）：
    // 「已订阅」以**当前勾选（待保存范围）**为准，勾选/取消即时反映到右栏与状态筛选，
    // 与页面「勾选=保存后的订阅范围、保存即全量替换」的语义一致。
    const keyOf = scopeKey
    // 勾选集合的 Set 视图：模板与筛选里高频判存，避免每次 includes 线性扫描（范围上限 128，属稳妥优化）
    const checkedSet = computed(() => new Set(checked.value))
    const strategyCounts = computed(() => buildStrategyCounts(owned.value))
    // 某标的当前有多少个策略持有（owner 里 strategies 的条数）
    const strategyCountOf = (key) => strategyCounts.value[key] || 0

    const typeOptions = computed(() => [...new Set(instruments.value.map(i => i.inst_type).filter(Boolean))])

    // 左栏列表 = 关键词 ∧ 类型 ∧ 订阅状态 三重筛选
    const filtered = computed(() => filterInstruments(instruments.value, checked.value, {
      keyword: keyword.value,
      typeFilter: typeFilter.value,
      statusFilter: statusFilter.value,
      strategyCounts: strategyCounts.value
    }))

    // 右栏「已订阅」= 当前勾选集合，**不受左栏筛选影响**（否则筛选一变右侧就跟着少几条）；
    // 勾选里若存在不在当前清单中的条目（引擎从 DB 恢复的范围条目，市场信息未加载时不在
    // instruments 里），纯函数会补一行并标 missing，保证任何已勾选条目都能在右栏被移除。
    const subscribedRows = computed(() => buildSubscribedRows(instruments.value, checked.value, strategyCounts.value))

    const strategyUsedCount = computed(() => Object.values(strategyCounts.value).filter(c => c > 0).length)
    const emptyText = computed(() => {
      if (loadError.value) return `加载失败：${loadError.value}`
      if (instruments.value.length === 0) return '暂无标的（请确认引擎已启动并已加载标的）'
      return '没有符合筛选条件的标的'
    })

    const loadData = async () => {
      loading.value = true
      try {
        const res = await getInstrumentScope()
        if (res.success) {
          instruments.value = res.data.instruments || []
          checked.value = (res.data.scope || []).map(s => scopeKey(s))
          owned.value = res.data.owned || []
          loadError.value = ''
        } else {
          loadError.value = res.message || res.error || '加载失败'
          ElMessage.error(loadError.value)
        }
      } catch (e) {
        // 引擎侧失败体用 error 字段，Flask 侧用 message，两个字段都尝试
        loadError.value = e.response?.data?.error || e.response?.data?.message || '引擎不可达'
        ElMessage.error('加载失败：' + loadError.value)
      } finally {
        loading.value = false
      }
    }

    // 全选/清空只作用于左栏「当前筛选结果」，已订阅区域与其它筛选外的勾选不受影响
    const selectAllVisible = () => {
      const keys = filtered.value.map(keyOf)
      checked.value = [...new Set([...checked.value, ...keys])]
    }
    const clearVisible = () => {
      const keys = new Set(filtered.value.map(keyOf))
      checked.value = checked.value.filter(k => !keys.has(k))
    }

    // 右栏单条移除 = 取消该条勾选（真正退订在「保存」时下发）
    const unsubscribe = (key) => {
      checked.value = checked.value.filter(k => k !== key)
    }

    // 右栏「清空已订阅」= 取消全部勾选（含不在当前筛选结果里的），需二次确认
    const clearAllSubscribed = async () => {
      if (!checked.value.length) return
      try {
        await ElMessageBox.confirm(
          `将取消全部 ${checked.value.length} 个标的的勾选（点击「保存」后才会真正退订）。`,
          '确认清空已订阅',
          { type: 'warning' }
        )
      } catch { return }
      checked.value = []
    }

    const save = async () => {
      try {
        await ElMessageBox.confirm(`将保存 ${checked.value.length} 个标的，未勾选的标的会取消订阅（策略在用的会保留）。`, '确认保存', { type: 'warning' })
      } catch { return }
      saving.value = true
      try {
        const scope = checked.value.map(k => {
          const [market, inst_id, inst_type] = k.split('|')
          return { market, inst_id, inst_type }
        })
        const res = await setInstrumentScope(scope)
        if (res.success) {
          const d = res.data || res
          ElMessage.success(`已保存：新增订阅 ${d.applied_cnt || 0}、退订 ${d.removed_cnt || 0}、策略占用保留 ${d.kept_cnt || 0}` +
            (d.unknown_cnt ? `、无效 ${d.unknown_cnt}` : ''))
          await loadData()
        } else {
          ElMessage.error('保存失败：' + (res.message || res.error || '未知错误'))
        }
      } catch (e) {
        // 引擎侧失败体用 error 字段（HTTP 500 由 axios 抛出），Flask 侧用 message
        ElMessage.error('保存失败：' + (e.response?.data?.error || e.response?.data?.message || '引擎不可达'))
      } finally {
        saving.value = false
      }
    }

    onMounted(loadData)
    return { loading, saving, loadError, instruments, checked, checkedSet, owned, keyword, typeFilter, statusFilter,
             STATUS_OPTIONS, typeOptions, filtered, subscribedRows, emptyText, strategyUsedCount, strategyCountOf,
             keyOf, loadData, selectAllVisible, clearVisible, unsubscribe, clearAllSubscribed, save }
  }
}
</script>

<style scoped>
/* 本页布局；深色主题沿用 App.vue 全局 CSS */
.settings-manager {
  padding: 20px;
}

.settings-card {
  background-color: #2c2c2c;
  border-color: #404040;
}

.settings-card :deep(.el-card__header) {
  background-color: #2a2a2a;
  border-color: #404040;
  color: #e0e0e0;
}

.card-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.toolbar {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 12px;
  margin-bottom: 12px;
}

.keyword-input {
  width: 240px;
}

.type-select {
  width: 160px;
}

.status-select {
  width: 140px;
}

.summary-bar {
  font-size: 13px;
  color: #c0c0c0;
  margin-bottom: 12px;
}

.summary-bar .muted {
  color: #909090;
}

/* 左右双栏：左=全量清单（可筛选），右=已订阅（独立区域） */
.panes {
  display: flex;
  align-items: stretch;
  gap: 16px;
}

.pane {
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.pane-all {
  flex: 1 1 0;
}

/* 右栏固定宽度（注意：类名不能叫 .pane-sub，那个类用于 pane-head 里的计数标签） */
.pane-subscribed {
  flex: 0 0 360px;
}

.pane-head {
  display: flex;
  align-items: baseline;
  gap: 8px;
  margin-bottom: 6px;
}

.pane-title {
  font-size: 14px;
  font-weight: bold;
  color: #e0e0e0;
}

.pane-sub {
  font-size: 12px;
  color: #909090;
}

.pane-action {
  margin-left: auto;
}

.list-wrap {
  flex: 1 1 auto;
  min-height: 200px;
  max-height: 62vh;
  overflow-y: auto;
  border: 1px solid #404040;
  border-radius: 4px;
  padding: 8px 12px;
}

.inst-group {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.inst-item {
  width: 100%;
  margin-right: 0;
  padding: 6px 8px;
  border-radius: 4px;
}

.inst-item:hover {
  background-color: #353535;
}

.inst-item :deep(.el-checkbox__label) {
  color: #d0d0d0;
}

.inst-id {
  display: inline-block;
  min-width: 180px;
  font-family: ui-monospace, 'SF Mono', Consolas, 'Courier New', monospace;
  color: #e0e0e0;
}

.inst-meta {
  margin-left: 8px;
}

/* 右栏条目：标的 + 标签 + 单条移除 */
.sub-list {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.sub-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 6px 8px;
  border-radius: 4px;
}

.sub-item:hover {
  background-color: #353535;
}

.sub-main {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  min-width: 0;
}

.sub-item .inst-id {
  min-width: 130px;
}

.sub-tags {
  display: inline-flex;
  flex-wrap: wrap;
  align-items: center;
}

.sub-remove {
  flex: 0 0 auto;
}

.save-bar {
  margin-top: 16px;
  display: flex;
  justify-content: flex-end;
}

/* 窄屏退化为上下布局，避免右栏被压到不可用 */
@media (max-width: 1100px) {
  .panes {
    flex-direction: column;
  }

  .pane-subscribed {
    flex: 1 1 auto;
  }
}
</style>
