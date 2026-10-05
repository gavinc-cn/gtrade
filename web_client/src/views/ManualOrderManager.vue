<template>
  <div class="manual-order" :class="side === 'buy' ? 'is-buy' : 'is-sell'">
    <!-- 引擎不可用/未开放下单接口时的降级横幅 -->
    <el-alert
      v-if="engineBanner"
      :title="engineBanner"
      type="warning"
      show-icon
      :closable="true"
      class="engine-banner"
      @close="engineBanner = ''"
    />

    <div class="ticket-grid">
      <!-- ① 委托票据（下单表单） -->
      <el-card class="panel ticket-card" shadow="never">
        <template #header>
          <div class="card-head">
            <span class="card-title">委托票据</span>
            <span class="mono card-sub">OKX · {{ instId || '未选择品种' }}</span>
          </div>
        </template>

        <div class="ticket-form">
          <div class="field-row">
            <div class="field">
              <label>账户</label>
              <el-select
                v-model="accountId"
                filterable
                allow-create
                default-first-option
                placeholder="选择或输入账户ID"
                size="large"
                class="dark-select"
              >
                <el-option v-for="a in accountOptions" :key="a" :label="a" :value="a" />
              </el-select>
              <div v-if="accountsLoadFailed" class="field-hint warn">资金列表不可用，可手动输入账户ID</div>
            </div>
            <div class="field">
              <label>市场</label>
              <el-select
                v-model="market"
                filterable
                allow-create
                default-first-option
                placeholder="okx"
                size="large"
                class="dark-select"
              >
                <el-option v-for="m in MARKET_OPTIONS" :key="m" :label="m" :value="m" />
              </el-select>
            </div>
          </div>

          <div class="field-row">
            <div class="field">
              <label>品种</label>
              <el-select
                v-model="instId"
                filterable
                default-first-option
                placeholder="如 BTC-USDT-SWAP"
                size="large"
                class="dark-select"
              >
                <el-option v-for="i in instOptions" :key="i" :label="i" :value="i" />
              </el-select>
              <div v-if="!instOptions.length" class="field-hint warn">
                请先在"设置"页配置标的范围
              </div>
            </div>
            <div class="field">
              <label>组合 <span class="field-hint">选填</span></label>
              <el-select
                v-model="portfolio"
                filterable
                allow-create
                default-first-option
                clearable
                placeholder="默认（不指定）"
                size="large"
                class="dark-select"
              >
                <el-option v-for="p in portfolioOptions" :key="p" :label="p" :value="p" />
              </el-select>
            </div>
          </div>

          <div class="field">
            <label>保证金模式</label>
            <div class="seg">
              <button
                v-for="m in tdModeOptions"
                :key="m.value"
                type="button"
                class="seg-btn"
                :class="{ active: tdMode === m.value }"
                @click="tdMode = m.value"
              >{{ m.label }}</button>
            </div>
            <div v-if="tdModeHint" class="td-hint">{{ tdModeHint }}</div>
          </div>

          <div class="field">
            <label>方向</label>
            <div class="side-toggle">
              <button
                type="button"
                class="side-btn buy"
                :class="{ active: side === 'buy' }"
                @click="side = 'buy'"
              >买入</button>
              <button
                type="button"
                class="side-btn sell"
                :class="{ active: side === 'sell' }"
                @click="side = 'sell'"
              >卖出</button>
            </div>
          </div>

          <div class="field">
            <label>委托类型</label>
            <div class="seg">
              <button
                v-for="t in ORD_TYPES"
                :key="t.value"
                type="button"
                class="seg-btn"
                :class="{ active: ordType === t.value }"
                @click="onOrdTypeChange(t.value)"
              >{{ t.label }}</button>
            </div>
          </div>

          <div class="field-row">
            <div class="field">
              <label>价格 <span v-if="isMarket" class="field-hint">市价免填</span></label>
              <input
                v-model="px"
                type="text"
                class="mono-input"
                :disabled="isMarket"
                placeholder="点击盘口档位填入"
                inputmode="decimal"
              >
            </div>
            <div class="field">
              <label>数量{{ szUnit }}</label>
              <input
                v-model="sz"
                type="text"
                class="mono-input"
                placeholder="0.000"
                inputmode="decimal"
                @keyup.enter="openPreview"
              >
            </div>
          </div>

          <div class="notional mono">
            <span class="muted">预估金额</span>
            <b>{{ estNotional }}</b>
            <span v-if="isContract" class="muted">合约未估算（名义价值需 ctVal）</span>
            <template v-else>
              <span class="muted">USDT</span>
              <span v-if="isMarket" class="muted">（按盘口一档参考价）</span>
            </template>
          </div>

          <button type="button" class="submit-btn" :disabled="!canSubmit" @click="openPreview">
            {{ side === 'buy' ? '预览买入委托' : '预览卖出委托' }}
          </button>
        </div>
      </el-card>

      <!-- ② 盘口十档（点选填价） -->
      <el-card class="panel depth-card" shadow="never">
        <template #header>
          <div class="card-head">
            <span class="card-title">盘口</span>
            <span class="card-sub">
              <el-tag v-if="depthMock" size="small" type="warning" effect="dark">模拟行情</el-tag>
              <span class="mono muted">{{ depthTime }}</span>
            </span>
          </div>
        </template>

        <!-- 单边盘口（如只有买盘）也要渲染：bbo 只有一侧挂单时是常态，不能只认卖档 -->
        <div v-if="askRows.length || bidRows.length" class="ladder">
          <div
            v-for="row in askRows"
            :key="'a' + row.lv"
            class="lrow ask"
            :class="{ sel: isPxSelected(row.px) }"
            :title="'点击填入卖档价格 ' + row.pxFmt"
            @click="fillPx(row.px)"
          >
            <span class="bar" :style="{ width: row.barPct }" />
            <span class="lv mono">{{ row.lv }}</span>
            <span class="px mono">{{ row.pxFmt }}</span>
            <span class="sz mono">{{ row.szFmt }}</span>
          </div>

          <div class="mid mono">
            <span>{{ midDisplay }}</span>
            <span class="muted">中间价 · 价差 {{ spreadDisplay }}</span>
          </div>

          <div
            v-for="row in bidRows"
            :key="'b' + row.lv"
            class="lrow bid"
            :class="{ sel: isPxSelected(row.px) }"
            :title="'点击填入买档价格 ' + row.pxFmt"
            @click="fillPx(row.px)"
          >
            <span class="bar" :style="{ width: row.barPct }" />
            <span class="lv mono">{{ row.lv }}</span>
            <span class="px mono">{{ row.pxFmt }}</span>
            <span class="sz mono">{{ row.szFmt }}</span>
          </div>

          <div class="ladder-hint muted">点击档位价格填入委托价</div>
        </div>
        <div v-else class="depth-empty muted">
          {{ depthError ? '盘口数据不可用：' + depthError : '等待盘口数据…' }}
        </div>
      </el-card>
    </div>

    <!-- ③ 24 小时内委托：跟随表单账户/市场，状态与品种为客户端过滤 -->
    <el-card class="panel open-card" shadow="never">
      <template #header>
        <div class="card-head">
          <span class="card-title">24小时内委托</span>
          <span class="card-sub">
            <span class="mono muted">{{ accountId || '全部账户' }} · {{ market }} · 共 {{ visibleOrders.length }} 笔</span>
            <el-checkbox v-model="onlyCurrentInst" size="small" class="inst-check">仅当前品种</el-checkbox>
            <el-select v-model="statusFilter" size="small" class="status-filter" placeholder="全部状态">
              <el-option label="全部状态" value="" />
              <el-option v-for="s in statusOptions" :key="s.value" :label="s.label" :value="s.value" />
            </el-select>
          </span>
        </div>
      </template>

      <div v-if="ordersError" class="field-hint warn orders-error">委托列表不可用：{{ ordersError }}</div>

      <el-table :data="visibleOrders" size="small" :row-class-name="rowClassName" empty-text="24 小时内暂无委托">
        <el-table-column prop="entno" label="委托号" min-width="150">
          <template #default="scope"><span class="mono">{{ scope.row.entno }}</span></template>
        </el-table-column>
        <el-table-column prop="inst" label="品种" min-width="120">
          <template #default="scope"><span class="mono">{{ scope.row.inst }}</span></template>
        </el-table-column>
        <el-table-column prop="portfolio" label="组合" min-width="90">
          <template #default="scope"><span class="mono muted">{{ scope.row.portfolio || '—' }}</span></template>
        </el-table-column>
        <el-table-column label="方向" width="70">
          <template #default="scope">
            <span class="side-tag" :class="isBuySide(scope.row.bs) ? 't-buy' : 't-sell'">
              {{ isBuySide(scope.row.bs) ? '买' : '卖' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column label="价格" width="110" align="right">
          <template #default="scope"><span class="mono">{{ scope.row.price }}</span></template>
        </el-table-column>
        <el-table-column label="数量" width="110" align="right">
          <template #default="scope"><span class="mono">{{ scope.row.amount }}</span></template>
        </el-table-column>
        <el-table-column label="已成交" width="110" align="right">
          <template #default="scope"><span class="mono">{{ scope.row.filled }}</span></template>
        </el-table-column>
        <el-table-column label="剩余" width="110" align="right">
          <template #default="scope"><span class="mono">{{ scope.row.remain }}</span></template>
        </el-table-column>
        <el-table-column label="状态" width="90">
          <template #default="scope">
            <el-tag size="small" effect="dark" :color="statusColor(scope.row.status)" style="border: none;">
              {{ statusText(scope.row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="时间" min-width="150">
          <template #default="scope"><span class="mono muted">{{ scope.row.time }}</span></template>
        </el-table-column>
        <el-table-column label="操作" width="90" fixed="right">
          <template #default="scope">
            <el-button v-if="isActiveStatus(scope.row.status)" type="danger" size="small" plain @click="onCancel(scope.row)">撤单</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <!-- 下单确认票据：所有字段回显后才允许发送 -->
    <el-dialog
      v-model="previewVisible"
      width="430px"
      class="ticket-dialog"
      :close-on-click-modal="false"
    >
      <template #header>
        <div class="ticket-head" :class="side">
          <span class="ticket-head-side">{{ side === 'buy' ? '买入' : '卖出' }}</span>
          <span class="mono">{{ instId }}</span>
        </div>
      </template>

      <div class="ticket-body mono">
        <div class="trow"><span>账户</span><b>{{ accountId }}</b></div>
        <div class="trow"><span>市场</span><b>{{ market }}</b></div>
        <div class="trow">
          <span>品种</span>
          <b>{{ instId }}<span v-if="selectedInstType" class="muted"> · {{ selectedInstType }}</span></b>
        </div>
        <div class="trow">
          <span>组合</span>
          <b>{{ portfolio || '默认（不指定）' }}</b>
        </div>
        <div class="trow"><span>保证金模式</span><b>{{ tdModeLabel }}</b></div>
        <div class="trow"><span>委托类型</span><b>{{ ordTypeLabel }}</b></div>
        <div class="trow">
          <span>价格</span>
          <b>{{ isMarket ? '市价（按盘口撮合）' : px }}</b>
        </div>
        <div class="trow"><span>数量</span><b>{{ sz }} {{ isContract ? '张' : '币' }}</b></div>
        <div class="trow">
          <span>预估金额</span>
          <b>{{ isContract ? '—（合约名义价值需 ctVal，本页不估算）' : estNotional + ' USDT' }}</b>
        </div>
      </div>
      <div class="ticket-note muted">发出后委托先由引擎受理（返回本地委托号），交易所确认异步完成，状态以下方列表为准。</div>

      <template #footer>
        <el-button @click="previewVisible = false">取消</el-button>
        <el-button type="primary" class="confirm-btn" :loading="submitting" @click="confirmSend">
          确认发送
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { dictService } from '@/services/dictService'
import { placeOrder, cancelOrder, getDepth, getOrders, getBalances, getPortfolioPositions } from '@/services/tradeService'
import { getInstrumentScope } from '@/services/settingsService'
import { subscribe, unsubscribe, onStatus, loadConfig, getConfig } from '@/services/pushStream'
import { acceptOrder } from '@/services/pushState'

// ── 页面常量（demo 按 BTC-USDT-SWAP 常规精度，换品种时在此调整） ─────────────
const PX_DECIMALS = 1   // 价格最多小数位
const SZ_DECIMALS = 3   // 数量最多小数位
const DEPTH_LEVELS = 5  // 盘口显示档位数（买卖各 N 档）
// 仅用于"是否显示撤单按钮"的委托状态集合：数字码为 order.status CHAR(1)，
// 英文串为后端可能的字符串状态（与 dictService 的兼容表一致）
const ACTIVE_STATUS = new Set(['0', '1', '2', '3', 'submitted', 'partially_filled'])
// 委托列表拉取上限（按 ent_time 倒序取最新 N 条，再在客户端过滤 24h 窗口）
const ORDER_FETCH_LIMIT = 1000
const WINDOW_24H_NS = 24 * 3600 * 1000 * 1000000  // 24 小时，纳秒
// 市场候选（取自仓库实际配置：config/*.yml、strategy_config；可手动输入其他值）
const MARKET_OPTIONS = ['okx', 'okx_dummy', 'ctp']
// 状态过滤降级表（dictService 可用时以其为准，name 与 dict.yml 一致）
const STATUS_FALLBACK = [
  { value: '0', label: '未报' }, { value: '1', label: '正报' }, { value: '2', label: '已报' },
  { value: '3', label: '部成' }, { value: '4', label: '全部成交' }, { value: '5', label: '已报待撤' },
  { value: '6', label: '场内撤单' }, { value: '7', label: '部成待撤' }, { value: '8', label: '部成部撤' },
  { value: '9', label: '废单' }, { value: 'I', label: '冻结' }, { value: 'A', label: '预埋单' }
]
// 深度行情轮询失败时的降级策略：false = 如实显示"盘口数据不可用"（默认）。
// 曾经为 true（demo 便利）：失败时随机造一条十档并在页头标注「模拟行情」——行情来源不可辨，
// 与真行情混在一起极易误判，故默认关闭；确需演示时才置 true。
const MOCK_DEPTH = false

// tdMode 有双重语义，**必须与标的 instType 联动**：对现货，cash 是现货、cross/isolated 是币币
// 杠杆（本系统不做，OKX 的 MARGIN 直接复用现货 instId）；对 SWAP/FUTURES/OPTION，cross/isolated
// 是**合约保证金模式**，与现货/杠杆无关。所以"全仓/逐仓"两个按钮只有合约标的才该出现。
// types 与 mock_exchange/mockex/core/instrument_index.py 的 _ALLOWED_TD_MODES 一一对应
// （MARGIN 已不在出厂清单内，列出只为与 mock 校验表对齐）。
const TD_MODES = [
  { value: 'cash',     label: '现货', types: ['SPOT'] },
  { value: 'cross',    label: '全仓', types: ['SWAP', 'FUTURES', 'OPTION', 'MARGIN'] },
  { value: 'isolated', label: '逐仓', types: ['SWAP', 'FUTURES', 'OPTION', 'MARGIN'] }
]
const ORD_TYPES = [
  { value: 'limit', label: '限价' },
  { value: 'market', label: '市价' },
  { value: 'post_only', label: 'Post Only' },
  { value: 'fok', label: 'FOK' },
  { value: 'ioc', label: 'IOC' }
]

export default {
  name: 'ManualOrderManager',
  setup() {
    // ── 表单状态 ────────────────────────────────────────────────────────────
    const accountId = ref('')
    const instId = ref('')
    const market = ref('okx')        // 市场取值须与账户归属市场一致，否则引擎查不到 inst_id_code
    const portfolio = ref('')        // 组合（可选）：后端 HttpPlaceOrderReq 暂无此字段，payload 预留、引擎目前忽略
    const tdMode = ref('cash')       // 选中标的时按 inst_type 自动校正（见 tdModeOptions 后的 watch）
    const side = ref('buy')          // buy | sell，页面强调色随它切换
    const ordType = ref('limit')     // limit | market | post_only | fok | ioc
    const px = ref('')
    const sz = ref('')

    const accountOptions = ref([])
    const accountsLoadFailed = ref(false)
    const portfolioOptions = ref([])
    const instOptions = ref([])
    // 设置页范围的原始行 [{market, inst_id, inst_type}]：inst_type 联动 tdMode 选项与数量单位
    // （见 tdModeOptions / szUnit）—— 同一份 tdMode 对现货与合约语义不同，不能写死一组选项
    const scopeRows = ref([])

    // ── 由标的 inst_type 派生的表单约束 ──────────────────────────────────────
    // 放在这里而非「校验与提交」段：estNotional（下方盘口段）也要用 isContract，
    // 定义紧跟 scopeRows 可避免跨 70 行的前向引用。
    // 选中标的对应的范围行：inst_type 是 (market, inst_id, inst_type) 三元组的一维，同 instId 在
    // 不同市场可能不同类型，故优先同市场命中；market 为空或未命中再退回只按 inst_id 匹配。
    const selectedScopeRow = computed(() =>
      scopeRows.value.find(s => s.inst_id === instId.value && (!market.value || s.market === market.value)) ||
      scopeRows.value.find(s => s.inst_id === instId.value) || null
    )
    // 当前选中标的的 inst_type（来自设置页范围），决定 tdMode 与数量单位的合法取值
    const selectedInstType = computed(() => selectedScopeRow.value?.inst_type || '')

    // 该 inst_type 允许的 tdMode 选项；inst_type 未知（范围尚未加载完/老数据）时给出全部，
    // 由 mock 协议层报错兜底 —— 明确报错优于页面按猜测静默拦住合法委托。
    const tdModeOptions = computed(() => {
      const instType = selectedInstType.value
      if (!instType) return TD_MODES
      const opts = TD_MODES.filter(m => m.types.includes(instType))
      return opts.length ? opts : TD_MODES
    })

    // 切换标的时把 tdMode 校正到该 inst_type 的合法取值：合约标的停在 cash 会被 mock 协议层
    // 拒（tdMode 与 instType 不匹配），联动比拦截更符合"选了合约就是要下合约单"的直觉。
    watch([instId, selectedInstType], () => {
      const opts = tdModeOptions.value
      if (opts.length && !opts.some(m => m.value === tdMode.value)) tdMode.value = opts[0].value
    }, { immediate: true })

    // 段控下方说明：同一个"全仓/逐仓"标识对现货与合约含义不同，不解释容易被误读为"只能做现货"
    const tdModeHint = computed(() => {
      const instType = selectedInstType.value
      if (!instId.value || !instType) return ''
      return instType === 'SPOT'
        ? '现货标的：只用 cash。cross/isolated 在现货含义是币币杠杆，本系统不提供'
        : `${instType} 标的：cross/isolated 是合约保证金模式（全仓/逐仓）`
    })

    // 是否合约类标的（sz 单位为"张"）：决定数量单位与预估金额能否计算
    const isContract = computed(() => ['SWAP', 'FUTURES', 'OPTION'].includes(selectedInstType.value))

    // 数量单位：OKX 合约的 sz 单位是"张"（1 张面值见清单 ctVal，如 BTC-USDT-SWAP = 0.01 BTC），
    // 现货/杠杆的 sz 才是币数 —— 单位看错会出现百倍级误单，必须在输入框上标明。
    const szUnit = computed(() => (isContract.value ? '（张）' : '（币）'))

    // 账户所属市场（account_id → market，来自 balance 行）：引擎按 (market, inst) 查行情缓存与
    // inst_id_code，市场与账户错配会同时查不到盘口和合约，故选中账户后自动对齐「市场」字段
    const accountMarketMap = {}
    const syncMarketFromAccount = () => {
      const m = accountMarketMap[accountId.value]
      if (m && m !== market.value) market.value = m
    }

    // 买卖方向：dict.yml 的 TradeSide 取值是小写 'b'/'s'，展示前统一大写归一
    // （与 OrderManager/TradeManager 同款降级处理；直接比 'B' 会让买单显示成卖）
    const isBuySide = bs => String(bs).toUpperCase() === 'B'

    const isMarket = computed(() => ordType.value === 'market')
    const tdModeLabel = computed(() => TD_MODES.find(m => m.value === tdMode.value)?.label || tdMode.value)
    const ordTypeLabel = computed(() => ORD_TYPES.find(t => t.value === ordType.value)?.label || ordType.value)

    // ── 盘口状态 ────────────────────────────────────────────────────────────
    const depthMock = ref(false)
    const depthError = ref('')
    const depthTs = ref(0)
    const asks = ref([])   // [[price, size], ...] 卖档，price 升序（一档在前）
    const bids = ref([])   // 买档，price 降序（一档在前）

    const maxLadderSize = computed(() => {
      // 深度条按最大挂量归一化
      const all = [...asks.value.slice(0, DEPTH_LEVELS), ...bids.value.slice(0, DEPTH_LEVELS)]
      return Math.max(...all.map(d => Number(d[1]) || 0), 0.0000001)
    })

    // 卖档自上而下渲染：从高价到一档（升序数组倒序取前 N）
    const askRows = computed(() => asks.value.slice(0, DEPTH_LEVELS).map((d, i) => ladderRow(d, DEPTH_LEVELS - i, -1)))
    const bidRows = computed(() => bids.value.slice(0, DEPTH_LEVELS).map((d, i) => ladderRow(d, i + 1, 1)))

    const ladderRow = (d, lv, dir) => {
      const pxNum = Number(d[0])
      const szNum = Number(d[1])
      return {
        lv: String(lv).padStart(2, '0'),
        px: pxNum,
        pxFmt: fmtNum(pxNum, PX_DECIMALS),
        szFmt: fmtNum(szNum, SZ_DECIMALS),
        // dir=-1 时条从右侧生长（卖档），dir=1 时从左侧生长（买档）
        barPct: Math.max(4, Math.round((szNum / maxLadderSize.value) * 100)) + '%',
        dir
      }
    }

    const bestAsk = computed(() => (asks.value.length ? Number(asks.value[0][0]) : NaN))
    const bestBid = computed(() => (bids.value.length ? Number(bids.value[0][0]) : NaN))
    const midDisplay = computed(() => {
      if (isNaN(bestAsk.value) || isNaN(bestBid.value)) return '—'
      return fmtNum((bestAsk.value + bestBid.value) / 2, PX_DECIMALS)
    })
    const spreadDisplay = computed(() => {
      if (isNaN(bestAsk.value) || isNaN(bestBid.value)) return '—'
      return fmtNum(bestAsk.value - bestBid.value, PX_DECIMALS)
    })
    const depthTime = computed(() => {
      if (!depthTs.value) return ''
      const d = new Date(Math.floor(depthTs.value / 1000000))
      return d.toLocaleTimeString('zh-CN', { hour12: false })
    })

    // ── 金额估算：限价用填入价，市价用对手一档（买看卖一、卖看买一） ──────────
    // 预估金额 = 数量 × 参考价，只对现货成立（sz 是币数）。合约的 sz 是张数，名义价值还需
    // × ctVal（BTC-USDT-SWAP = 0.01 → 差 100 倍），而 ctVal 不在本页数据链路里
    // （范围/盘口/委托接口都不带），故合约不给数字 —— 少显示一个数好过显示一个差 100 倍的数。
    const estNotional = computed(() => {
      if (isContract.value) return '—'
      const qty = parseFloat(sz.value)
      let price = isMarket.value ? NaN : parseFloat(px.value)
      if (isMarket.value) {
        price = side.value === 'buy' ? bestAsk.value : bestBid.value
      }
      if (!qty || isNaN(price)) return '—'
      return (qty * price).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })
    })

    // ── 委托列表（24 小时窗口，客户端按账户/市场/品种/状态过滤） ────────────
    const recentOrders = ref([])     // 已按 24h + 账户 + 市场过滤的原始行
    const statusFilter = ref('')     // '' = 全部状态
    const onlyCurrentInst = ref(false)
    const ordersError = ref('')
    const highlightEntno = ref('')

    // 状态过滤选项：数据字典优先，失败时用降级表
    const statusOptions = ref([])
    const loadStatusOptions = () => {
      const opts = dictService.getOptions('EntrustStatus')
      statusOptions.value = (opts && opts.length) ? opts : STATUS_FALLBACK
    }
    // 客户端过滤：状态 + 仅当前品种（fetch 不带过滤参数，切换零请求）
    const visibleOrders = computed(() => recentOrders.value.filter(o =>
      (!statusFilter.value || o.status === statusFilter.value) &&
      (!onlyCurrentInst.value || o.inst === instId.value)
    ))

    // ── 校验与提交 ──────────────────────────────────────────────────────────
    const previewVisible = ref(false)
    const submitting = ref(false)
    const engineBanner = ref('')

    const canSubmit = computed(() =>
      !!accountId.value && !!instId.value && !submitting.value &&
      szOk(sz.value) && (isMarket.value || pxOk(px.value))
    )

    const pxOk = v => new RegExp(`^\\d+(\\.\\d{1,${PX_DECIMALS}})?$`).test(String(v).trim())
    const szOk = v => { const n = parseFloat(v); return n > 0 && new RegExp(`^\\d+(\\.\\d{1,${SZ_DECIMALS}})?$`).test(String(v).trim()) }

    const isPxSelected = p => {
      const v = parseFloat(px.value)
      return !isNaN(v) && Math.abs(v - p) < 1e-9
    }
    // 点选盘口档位 → 填入委托价（签名交互：盘口即输入）
    const fillPx = p => {
      px.value = p.toFixed(PX_DECIMALS)
    }
    const onOrdTypeChange = v => {
      ordType.value = v
      // 切到市价时价格不再参与，清空避免误导
      if (v === 'market') px.value = ''
    }

    const openPreview = () => {
      if (!accountId.value) { ElMessage.warning('请先选择账户'); return }
      if (!instId.value) { ElMessage.warning('请先填写品种'); return }
      if (!isMarket.value && !pxOk(px.value)) { ElMessage.warning(`价格需为正数且最多 ${PX_DECIMALS} 位小数`); return }
      if (!szOk(sz.value)) { ElMessage.warning(`数量需为正数且最多 ${SZ_DECIMALS} 位小数`); return }
      engineBanner.value = ''
      previewVisible.value = true
    }

    const buildPayload = () => {
      const payload = {
        account_id: String(accountId.value).trim(),
        inst_id: String(instId.value).trim(),
        market: String(market.value).trim() || 'okx',
        td_mode: tdMode.value,
        side: side.value,          // buy | sell（网关要求的字符串枚举）
        ord_type: ordType.value,   // limit | market | post_only | fok | ioc
        sz: String(parseFloat(sz.value))
      }
      if (!isMarket.value) payload.px = String(parseFloat(px.value))
      // 组合：可选；引擎（OnHttpPlaceOrder → FillNewEntByReq）已支持，空值回落为 policy_no(mcp_trade)
      if (portfolio.value && String(portfolio.value).trim()) payload.portfolio = String(portfolio.value).trim()
      return payload
    }

    const confirmSend = async () => {
      submitting.value = true
      try {
        const data = await placeOrder(buildPayload())
        if (data.success) {
          ElMessage.success('委托已受理，委托号 ' + data.order_id)
          highlightEntno.value = String(data.order_id)
          previewVisible.value = false
          fetchOrders()
        } else {
          ElMessage.error('下单失败：' + (data.error || '未知错误'))
        }
      } catch (e) {
        handleEngineError(e, '下单')
      } finally {
        submitting.value = false
      }
    }

    // 网关错误的统一降级：404 = 编译未开 HTTP 交易；502/无响应 = 引擎未运行
    const handleEngineError = (e, action) => {
      if (e.response?.status === 404) {
        engineBanner.value = `当前引擎未开放${action}接口（需以 GTRADE_ENABLE_HTTP_TRADE 编译），仅可查看盘口行情`
      } else if (!e.response || e.response.status === 502) {
        engineBanner.value = '无法连接交易引擎，请确认 gtrade 主程序已运行'
      } else {
        ElMessage.error(`${action}失败：` + (e.response.data?.error || e.response.data?.message || e.message))
      }
    }

    const onCancel = async (row) => {
      try {
        await ElMessageBox.confirm(`确定撤销委托 ${row.entno}（${isBuySide(row.bs) ? '买' : '卖'} ${row.price} × ${row.amount}）？`, '撤单确认', {
          confirmButtonText: '确认撤单',
          cancelButtonText: '取消',
          type: 'warning'
        })
      } catch {
        return // 用户取消
      }
      try {
        const data = await cancelOrder(row.accountId, row.entno)
        if (data.success) {
          ElMessage.success('撤单请求已提交')
          fetchOrders()
        } else {
          ElMessage.error('撤单失败：' + (data.error || '未知错误'))
        }
      } catch (e) {
        handleEngineError(e, '撤单')
      }
    }

    // ── 数据加载 ────────────────────────────────────────────────────────────
    const tickDepth = async () => {
      if (document.hidden || !instId.value) return
      try {
        // market 必须跟随表单：引擎按 (market, inst_id) 查行情缓存，写死 okx 会永远查不到
        // 只订阅了 okx_dummy 的标的（页面下拉里的市场选择将形同虚设）
        const data = await getDepth(instId.value, market.value)
        if (data.success) {
          asks.value = data.asks || []
          bids.value = data.bids || []
          depthTs.value = data.timestamp
          depthMock.value = false
          depthError.value = ''
        } else {
          throw new Error(data.error || '返回失败')
        }
      } catch (e) {
        if (MOCK_DEPTH) {
          applyMockDepth()
        } else {
          depthError.value = e.response?.data?.message || e.response?.data?.error || e.message
        }
      }
    }

    // 模拟盘口：围绕缓慢漂移的中间价生成挂量随机的十档，仅 demo 展示用
    let simMid = 64200.0
    const applyMockDepth = () => {
      simMid = Math.max(1000, simMid + (Math.random() - 0.5) * 8)
      const ladder = (base, dir) => Array.from({ length: 20 }, (_, i) => [
        Number((base + dir * (i + 1) * 0.5).toFixed(PX_DECIMALS)),
        Number((Math.random() * 3 + 0.05).toFixed(SZ_DECIMALS))
      ])
      asks.value = ladder(simMid + 0.5, 1)   // 卖档向上，一档在前
      bids.value = ladder(simMid - 0.5, -1)  // 买档向下，一档在前
      depthTs.value = Date.now() * 1000000
      depthMock.value = true
      depthError.value = ''
    }

    // 委托行 → 视图行（轮询与推送共用同一套映射，保证两条来源渲染一致）
    const mapOrderRow = o => ({
      entno: String(o.entno),
      accountId: o.account_id,
      inst: o.inst_id,
      portfolio: o.portfolio || '',
      bs: o.bs_side,
      price: fmtNum(Number(o.price), PX_DECIMALS),
      amount: fmtNum(Number(o.amount), SZ_DECIMALS),
      filled: fmtNum(Number(o.filled), SZ_DECIMALS),
      remain: fmtNum(Number(o.remain), SZ_DECIMALS),
      status: String(o.status),
      time: fmtNsTime(o.ent_time),
      statusId: Number(o.status_id ?? -1)   // 实体守卫用：只接受更新的版本
    })

    // 事件是否属于当前视图范围（24h + 账户 + 市场）
    const inOrderScope = o => {
      if (Number(o.ent_time) < Date.now() * 1000000 - WINDOW_24H_NS) return false
      if (accountId.value && o.account_id !== accountId.value) return false
      if (market.value && (o.market || '') !== market.value) return false
      return true
    }

    // 推送到的委托事件：按 status_id 守卫后 upsert（推送与重连补查共用，重复/倒挂都安全）
    const handleOrderEvent = payload => {
      const row = payload?.data
      if (!row || !row.entno) return
      if (!inOrderScope(row)) return
      const entno = String(row.entno)
      const idx = recentOrders.value.findIndex(o => o.entno === entno)
      const prev = idx >= 0 ? recentOrders.value[idx] : null
      if (!acceptOrder(prev, { ...row, status_id: row.status_id ?? payload.status_id })) return
      const mapped = mapOrderRow(row)
      if (idx >= 0) recentOrders.value.splice(idx, 1, mapped)
      else recentOrders.value.unshift(mapped)
    }

    const tickOrders = async () => {
      if (document.hidden) return
      // 推送通道健康时不做轮询：委托状态由 trade 通道推送（含断线重连后的补查）
      if (pushStatus.value === 'open') return
      try {
        // 服务端只支持 policy_no/inst_id/status 精确过滤，无时间范围参数：
        // 按 ent_time 倒序取最新 ORDER_FETCH_LIMIT 条，24h/账户/市场/品种/状态全部在客户端过滤
        const data = await getOrders({ page: 1, page_size: ORDER_FETCH_LIMIT })
        if (!data.success) throw new Error(data.error || data.message || '返回失败')
        ordersError.value = ''
        recentOrders.value = (data.data.orders || [])
          .filter(inOrderScope)
          .map(mapOrderRow)
      } catch (e) {
        ordersError.value = e.message || '网络错误'
      }
    }

    const loadAccounts = async () => {
      try {
        const data = await getBalances({ page: 1, page_size: 500 })
        if (data.success) {
          const rows = data.data.balances || []
          accountOptions.value = [...new Set(rows.map(b => b.account_id).filter(Boolean))]
          // 账户 → 所属市场（balance 行的 market 列），供 syncMarketFromAccount 对齐「市场」字段
          rows.forEach(b => { if (b.account_id && b.market) accountMarketMap[b.account_id] = b.market })
          accountsLoadFailed.value = false
          if (!accountId.value && accountOptions.value.length) {
            accountId.value = accountOptions.value[0]
            syncMarketFromAccount()
          }
        } else {
          accountsLoadFailed.value = true
        }
      } catch {
        accountsLoadFailed.value = true
      }
    }

    // 标的范围（设置页）→ 下拉选项；失败时保持空并给出提示。
    // 保留整行 (market, inst_id, inst_type) 到 scopeRows，inst_type 供 tdModeOptions/szUnit 联动
    const loadInstOptions = async () => {
      try {
        const res = await getInstrumentScope()
        if (res.success) {
          scopeRows.value = res.data.scope || []
          instOptions.value = [...new Set(scopeRows.value.map(s => s.inst_id))]
          if (!instOptions.value.includes(instId.value)) instId.value = instOptions.value[0] || ''
        }
      } catch (e) {
        console.warn('标的范围加载失败:', e)
      }
    }

    // 组合下拉选项：从组合持仓表 distinct 推导（系统无组合列表专有接口）
    const loadPortfolios = async () => {
      try {
        const data = await getPortfolioPositions({ page: 1, page_size: 500 })
        if (data.success) {
          portfolioOptions.value = [...new Set((data.data.positions || []).map(p => p.portfolio).filter(Boolean))]
        }
      } catch {
        // 组合列表不可用时仍可手动输入（el-select allow-create 兜底）
      }
    }

    // 数据字典（委托状态显示），失败时降级为原值，与 OrderManager 一致
    const loadDict = async () => {
      try {
        await dictService.load()
      } catch (e) {
        console.warn('数据字典加载失败，使用本地降级:', e)
      }
    }

    const statusText = s => dictService.getName('EntrustStatus', s) || String(s)
    const statusColor = s => dictService.getEntrustStatusColor(s)
    // 是否活跃（可撤）状态，控制撤单按钮显示
    const isActiveStatus = s => ACTIVE_STATUS.has(String(s))

    const rowClassName = ({ row }) => (row.entno === highlightEntno.value ? 'row-hl' : '')

    // ── 工具函数 ────────────────────────────────────────────────────────────
    function fmtNum(n, decimals) {
      if (isNaN(n)) return '—'
      return n.toLocaleString('zh-CN', { minimumFractionDigits: decimals, maximumFractionDigits: decimals })
    }
    function fmtNsTime(ns) {
      if (!ns || ns === '0') return '-'
      const d = new Date(Math.floor(Number(ns) / 1000000))
      return d.toLocaleString('zh-CN', {
        month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false
      })
    }

    // ── 盘口推送（SSE 单例）＋ 降级轮询 ─────────────────────────────────────
    // 正常路径：订阅 depth:<market>:<inst>，由服务端"变化即推"（下限 0.5s，上限 60s 保底）；
    // 推送不可用（status != 'open'）时回落到 1s 轮询，恢复后立即停掉轮询。
    const pushStatus = ref('idle')   // idle | connecting | open | degraded
    const depthTopic = ref('')       // 当前订阅的 depth topic（'' = 未订阅）
    let fallbackTimer = null         // 降级轮询定时器
    let offPushStatus = null         // 推送状态监听注销函数

    /** 处理一次推送事件：event='depth' 为数据帧，'upstream_error' 为上游失败（连接不断） */
    const handleDepthEvent = (payload, event) => {
      if (event === 'upstream_error') {
        depthError.value = payload.message || payload.code || '盘口数据不可用'
        return
      }
      const d = payload.data || {}
      asks.value = d.asks || []
      bids.value = d.bids || []
      depthTs.value = d.timestamp || 0
      depthMock.value = false
      depthError.value = ''
    }

    const startDepthFallback = () => {
      if (fallbackTimer) return
      fallbackTimer = setInterval(tickDepth, getConfig().fallback_depth_poll_ms || 1000)
    }
    const stopDepthFallback = () => {
      if (fallbackTimer) {
        clearInterval(fallbackTimer)
        fallbackTimer = null
      }
    }

    /** 订阅当前标的的盘口推送（切标的退旧订新，SSE 流不断） */
    const subscribeDepth = () => {
      if (!market.value || !instId.value) return
      const topic = `depth:${market.value}:${instId.value}`
      if (depthTopic.value === topic) return
      if (depthTopic.value) unsubscribe(depthTopic.value, handleDepthEvent)
      depthTopic.value = topic
      subscribe(topic, handleDepthEvent)
    }

    /** 退订盘口推送（页面隐藏/卸载时调用；服务端计数归零即停止该标的上游采样） */
    const unsubscribeDepth = () => {
      if (!depthTopic.value) return
      unsubscribe(depthTopic.value, handleDepthEvent)
      depthTopic.value = ''
    }

    // 页面隐藏只退盘口（省流量），SSE 连接保留；恢复可见重新订阅（立即收到 initial 帧）
    const handleVisibilityChange = () => {
      if (document.hidden) {
        unsubscribeDepth()
        stopDepthFallback()
        return
      }
      subscribeDepth()
      tickDepth()
      if (pushStatus.value !== 'open') startDepthFallback()
    }

    // ── 生命周期：页面隐藏时跳过轮询，卸载时清理 ──────────────────────────────
    let orderTimer = null
    watch([instId, market], () => {
      asks.value = []
      bids.value = []
      depthMock.value = false
      depthError.value = ''
      tickDepth()          // 先给一帧（推送首帧到达前不空白）
      subscribeDepth()     // 切标的不重连：控制面退旧 + 订新
    })
    // 账户/市场切换时委托列表作用域随之变化；状态/品种过滤为纯客户端，无需 refetch
    watch([accountId, market], () => tickOrders())
    watch(accountId, () => syncMarketFromAccount())

    onMounted(() => {
      loadDict()
      loadInstOptions()
      loadAccounts()
      loadPortfolios()
      loadStatusOptions()
      tickDepth()
      tickOrders()
      orderTimer = setInterval(tickOrders, 3000)

      // 委托状态推送（trade 通道）：健康时 tickOrders 不发请求，由事件驱动更新
      subscribe('order', handleOrderEvent)

      // 推送：配置（降级轮询间隔由服务端下发）→ 状态监听 → 订阅当前标的
      loadConfig()
      offPushStatus = onStatus(s => {
        pushStatus.value = s
        if (s === 'open') stopDepthFallback()
        else if (!document.hidden) startDepthFallback()
      })
      subscribeDepth()
      document.addEventListener('visibilitychange', handleVisibilityChange)
    })
    onUnmounted(() => {
      document.removeEventListener('visibilitychange', handleVisibilityChange)
      unsubscribe('order', handleOrderEvent)
      unsubscribeDepth()
      stopDepthFallback()
      clearInterval(orderTimer)
      if (offPushStatus) offPushStatus()
    })

    return {
      TD_MODES, ORD_TYPES, MARKET_OPTIONS,
      accountId, instId, market, portfolio, tdMode, side, ordType, px, sz,
      accountOptions, accountsLoadFailed, portfolioOptions, instOptions,
      isMarket, tdModeLabel, ordTypeLabel,
      depthMock, depthError, depthTime, askRows, bidRows, midDisplay, spreadDisplay,
      recentOrders, visibleOrders, statusFilter, statusOptions, onlyCurrentInst,
      ordersError, highlightEntno,
      previewVisible, submitting, engineBanner,
      canSubmit, estNotional, tdModeOptions, tdModeHint, selectedInstType, szUnit, isContract,
      isPxSelected, fillPx, onOrdTypeChange, openPreview, confirmSend, onCancel,
      statusText, statusColor, isActiveStatus, isBuySide, rowClassName
    }
  }
}
</script>

<style scoped>
/* ── 设计令牌：墨盘底 / 票据面 / 买红卖绿（改 SIDE 两个值即可整体翻转） ── */
.manual-order {
  --panel: #353535;
  --panel-2: #2c2c2c;
  --line: #404040;
  --text: #e0e0e0;
  --muted: #909090;
  --buy: #e5484d;
  --sell: #30a46c;
  --mono: ui-monospace, 'SF Mono', 'JetBrains Mono', Consolas, 'Courier New', monospace;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  --accent: var(--buy);
  --accent-dim: rgba(229, 72, 77, 0.16);
}
.manual-order.is-sell {
  --accent: var(--sell);
  --accent-dim: rgba(48, 164, 108, 0.16);
}
.mono { font-family: var(--mono); font-variant-numeric: tabular-nums; }
.muted { color: var(--muted); }

.engine-banner { border-radius: 4px; }

.ticket-grid {
  display: grid;
  grid-template-columns: minmax(360px, 440px) 1fr;
  gap: 16px;
  align-items: stretch;
}

/* ── 面板通用：显式深色，不依赖全局主题 ── */
.panel {
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 6px;
}
.panel :deep(.el-card__header) {
  padding: 12px 16px;
  border-bottom: 1px solid var(--line);
}
.panel :deep(.el-card__body) { padding: 16px; }
.card-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}
.card-title { font-weight: 600; font-size: 14px; color: var(--text); }
.card-sub { font-size: 12px; display: inline-flex; align-items: center; gap: 8px; color: #a8a8a8; }

/* ── ① 委托票据 ── */
.ticket-form { display: flex; flex-direction: column; gap: 14px; }
.field { display: flex; flex-direction: column; gap: 6px; flex: 1; }
.field > label { font-size: 12px; color: var(--muted); }
.field-hint { font-size: 11px; color: var(--muted); font-weight: 400; }
.field-hint.warn, .warn { color: #d9a53a; }
.field-row { display: flex; gap: 12px; }

/* 下拉框深色化（下拉弹层为全局样式，不在本页范围内） */
.dark-select :deep(.el-input__wrapper),
.dark-select :deep(.el-input__inner) {
  background: var(--panel-2);
  color: var(--text);
  box-shadow: 0 0 0 1px var(--line) inset;
}

/* 分段选择（保证金模式 / 委托类型） */
.seg { display: flex; border: 1px solid var(--line); border-radius: 4px; overflow: hidden; }
.seg-btn {
  flex: 1;
  padding: 7px 0;
  font-size: 12px;
  background: var(--panel-2);
  color: var(--muted);
  border: none;
  border-right: 1px solid var(--line);
  cursor: pointer;
}
.seg-btn:last-child { border-right: none; }
.seg-btn:hover { color: var(--text); background: #333; }
.seg-btn.active { background: var(--accent-dim); color: var(--accent); font-weight: 600; }

/* 非现货标的的拦截提示：合约单需要 cross/isolated，本页只提供现货 */
/* 段控下方说明行：现在是解释性文案（tdMode 与 instType 的含义差异），非拦截告警，故用 muted */
.td-hint { margin-top: 6px; font-size: 11px; line-height: 1.5; color: var(--muted); }

/* 买/卖大开关：页面的状态机 */
.side-toggle { display: flex; gap: 10px; }
.side-btn {
  flex: 1;
  padding: 12px 0;
  font-size: 15px;
  font-weight: 700;
  letter-spacing: 4px;
  border-radius: 4px;
  border: 1px solid var(--line);
  background: var(--panel-2);
  color: var(--muted);
  cursor: pointer;
}
.side-btn.buy.active { background: rgba(229, 72, 77, 0.18); border-color: var(--buy); color: var(--buy); }
.side-btn.sell.active { background: rgba(48, 164, 108, 0.18); border-color: var(--sell); color: var(--sell); }
.side-btn:focus-visible, .seg-btn:focus-visible, .submit-btn:focus-visible {
  outline: 2px solid var(--accent);
  outline-offset: 2px;
}

/* 价格/数量输入（原生 input，保证等宽与深色一致） */
.mono-input {
  font-family: var(--mono);
  font-variant-numeric: tabular-nums;
  font-size: 16px;
  padding: 9px 12px;
  background: var(--panel-2);
  border: 1px solid var(--line);
  border-radius: 4px;
  color: var(--text);
  width: 100%;
  box-sizing: border-box;
}
.mono-input:disabled { opacity: 0.45; cursor: not-allowed; }
.mono-input:focus { outline: none; border-color: var(--accent); }
.mono-input::placeholder { color: #6b6b6b; font-size: 13px; }

.notional { display: flex; align-items: baseline; gap: 8px; font-size: 14px; color: var(--text); }
.notional b { font-size: 18px; }

.submit-btn {
  padding: 13px 0;
  font-size: 15px;
  font-weight: 700;
  letter-spacing: 2px;
  border: none;
  border-radius: 4px;
  background: var(--accent);
  color: #fff;
  cursor: pointer;
}
.submit-btn:hover:not(:disabled) { filter: brightness(1.1); }
.submit-btn:disabled { opacity: 0.4; cursor: not-allowed; }

/* ── ② 盘口梯 ── */
.ladder { display: flex; flex-direction: column; }
.lrow {
  position: relative;
  display: grid;
  grid-template-columns: 34px 1fr 110px;
  align-items: center;
  padding: 3px 10px;
  cursor: pointer;
  line-height: 1.5;
}
.lrow .bar {
  position: absolute;
  top: 2px;
  bottom: 2px;
  pointer-events: none;
}
.lrow.ask .bar { right: 0; background: rgba(48, 164, 108, 0.22); }
.lrow.bid .bar { left: 0; background: rgba(229, 72, 77, 0.22); }
.lrow:hover { background: rgba(255, 255, 255, 0.05); }
.lrow.ask.sel { box-shadow: inset 0 0 0 1px var(--sell); border-radius: 3px; }
.lrow.bid.sel { box-shadow: inset 0 0 0 1px var(--buy); border-radius: 3px; }
.lrow .lv { color: var(--muted); font-size: 11px; }
.lrow .px { text-align: right; }
.lrow.ask .px { color: var(--sell); }
.lrow.bid .px { color: var(--buy); }
.lrow .sz { text-align: right; color: var(--text); font-size: 12px; }
.mid {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  padding: 7px 10px;
  margin: 2px 0;
  border-top: 1px solid var(--line);
  border-bottom: 1px solid var(--line);
  font-size: 15px;
  color: var(--text);
  background: var(--panel-2);
}
.ladder-hint { font-size: 11px; text-align: center; padding-top: 10px; }
.depth-empty { padding: 40px 0; text-align: center; font-size: 13px; }

/* ── ③ 委托列表 ── */
.orders-error { padding-bottom: 8px; }
.status-filter { width: 130px; }
.inst-check :deep(.el-checkbox__label) { color: var(--muted); font-size: 12px; padding-left: 6px; }
.status-filter :deep(.el-input__wrapper),
.status-filter :deep(.el-input__inner) {
  background: var(--panel-2);
  color: var(--text);
  box-shadow: 0 0 0 1px var(--line) inset;
  height: 26px;
}
.side-tag { font-weight: 700; font-size: 12px; }
.side-tag.t-buy { color: var(--buy); }
.side-tag.t-sell { color: var(--sell); }
.manual-order :deep(.el-table .row-hl) { background: rgba(64, 158, 255, 0.12) !important; }
.manual-order :deep(.el-table .el-button.is-plain) { background: transparent; }

/* ── 确认票据弹窗 ── */
/* 弹窗深色化：字面量颜色（不依赖页面作用域的 CSS 变量，避免传送后失效） */
.manual-order :deep(.ticket-dialog.el-dialog) {
  background: #353535;
  border: 1px solid #404040;
}
.manual-order :deep(.ticket-dialog .el-dialog__headerbtn .el-dialog__close) { color: #909090; }
.ticket-head {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: -6px 0;
  padding: 8px 14px;
  border-radius: 4px;
  color: #fff;
}
.ticket-head.buy { background: #e5484d; }
.ticket-head.sell { background: #30a46c; }
.ticket-head-side { font-size: 17px; font-weight: 800; letter-spacing: 6px; }
.ticket-body { display: flex; flex-direction: column; }
.trow {
  display: flex;
  justify-content: space-between;
  padding: 9px 2px;
  border-bottom: 1px dashed var(--line);
  font-size: 13px;
}
.trow span { color: var(--muted); }
.trow b { color: var(--text); font-weight: 600; }
.ticket-note { font-size: 11px; margin-top: 10px; }
.confirm-btn {
  background: var(--accent) !important;
  border-color: var(--accent) !important;
}

/* ── 响应式与可访问性 ── */
@media (max-width: 900px) {
  .ticket-grid { grid-template-columns: 1fr; }
}
@media (prefers-reduced-motion: reduce) {
  .manual-order * { transition: none !important; animation: none !important; }
}
</style>
