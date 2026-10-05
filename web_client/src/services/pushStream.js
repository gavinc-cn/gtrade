/**
 * 推送通道客户端（**二通道**：quote / trade），模块级单例，跨路由/跨组件共享。
 *
 * 契约见 doc_ai/plan/202610/20261004_1300_二通道推送与补查方案_rev4.md §2–§6：
 * - **通道**：`depth:*` 走 quote（可丢/可合并的快照），其余（策略指标/委托/成交/持仓/资金）
 *   走 trade（不可静默丢）；两条独立 SSE 连接，各自订阅集、心跳、重连与补查。
 * - **帧**：服务端每个数据帧带 `seq`（连接内序号）与 `ts`（服务端 epoch ms），
 *   客户端用 `ts` 计算自身滞后（`getLagMs()`）；心跳是普通事件帧 `event: ping`
 *   （不是 SSE 注释行），看门狗 = 2× 心跳周期（quote 20s / trade 10s）。
 * - **closing**：服务端主动断开前会发 `event: closing{reason}`；收到即**立即重连**
 *   （不走退避），并在重连后按游标补查。
 * - **通道内补查**：`query(what, opts)` → POST /api/stream/query（带 req_id），
 *   结果作为 `event: result` 沿**同一条流**回流，按 req_id 配对；重连后自动按
 *   `by_entnos`（已知在途）+ `entno_gt` / `tdno_gt`（增量）补齐。
 * - **实体守卫**：委托按 `status_id`、成交按 `tdno` 判定新旧/去重（见 pushState.js），
 *   补查结果与推送事件交叠时也不会倒挂。
 *
 * 页面用法（组件只关心"订阅/退订 + 收数据回调 + 是否降级"）：
 *   import { subscribe, unsubscribe, onStatus, loadConfig } from '@/services/pushStream'
 *   onMounted(() => {
 *     loadConfig()
 *     const off = onStatus(s => { ... })          // 'open' -> 停轮询；其他 -> 起轮询
 *     subscribe('depth:okx_dummy:BTC-USDT-SWAP', onDepth)   // 通道由 topic 自动推导
 *     subscribe('order', onOrder)
 *   })
 *   onUnmounted(() => { unsubscribe(topic, onDepth); off() })
 *
 * 回调签名：handler(payload, event)
 *   event = 'depth' | 'strategies' | 'order' | 'trade' | 'position' | 'balance' | 'upstream_error'
 *   payload 为服务端事件 data（含 topic/ts/seq，增量事件另带 cursor）。
 */
import request from '@/utils/request'
import {
  CHANNEL_QUOTE,
  CHANNEL_TRADE,
  buildRecoveryPlan,
  channelOfTopic,
  createCursorStore,
  frameLagMs,
  isTerminalOrder,
  watchdogMsFor
} from './pushState'

/** 服务端未下发配置时的兜底值（与 web_server/config.py 的 WEB_PUSH_DEFAULTS 对齐） */
const DEFAULT_CONFIG = {
  enabled: true,
  degraded_after_ms: 10000,
  watchdog_ms: 90000,
  ping_interval_quote_ms: 10000,
  ping_interval_trade_ms: 5000,
  fallback_depth_poll_ms: 1000,
  fallback_strategies_poll_ms: 10000
}

/** 服务端事件名（与 web_server/push_hub.py 的常量一致） */
const EVENT_READY = 'ready'
const EVENT_PING = 'ping'
const EVENT_CLOSING = 'closing'
const EVENT_RESULT = 'result'
const EVENT_UPSTREAM_ERROR = 'upstream_error'

/** 重连退避：1s 起、倍增至 10s 上限；closing（服务端主动断开）走立即重连 */
const RETRY_MIN_MS = 1000
const RETRY_MAX_MS = 10000
/** 看门狗巡检周期 */
const WATCHDOG_TICK_MS = 3000
/** 服务端明确返回"推送通道已关闭"后的重试间隔 */
const DISABLED_RETRY_MS = 60000
/** 补查请求超时 */
const QUERY_TIMEOUT_MS = 8000

const CHANNEL_DEFS = [CHANNEL_QUOTE, CHANNEL_TRADE]

function makeChannel(name) {
  return {
    name,
    es: null,
    streamId: '',
    status: 'idle',
    urlTopics: [],
    confirmed: new Set(),
    connecting: false,
    retryTimer: null,
    retryDelay: RETRY_MIN_MS,
    degradeTimer: null,
    firstFailAt: 0,
    lastEventAt: 0,
    lastFrameTs: 0,
    lagMs: null,
    openedOnce: false,
    nextReqId: 1,
    pending: new Map()      // req_id -> {resolve, reject, timer}
  }
}

const state = {
  refs: new Map(),            // topic -> 引用计数（计数归零才真正退订）
  handlers: new Map(),        // topic -> Set<handler>
  channels: Object.fromEntries(CHANNEL_DEFS.map(n => [n, makeChannel(n)])),
  status: 'idle',             // idle | connecting | open | degraded（聚合状态，页面只看这个）
  statusCbs: new Set(),
  noticeCbs: new Set(),       // closing/上游错误等提示（页面可弹提示，不用也可）
  cfg: { ...DEFAULT_CONFIG },
  cfgPromise: null,
  watchdogTimer: null,
  cursors: createCursorStore(),
  openOrders: new Map(),      // entno -> status（补查 by_entnos 的输入）
  seenTrades: new Set()       // tdno 去重集合（实体守卫；供消费方复用）
}

function now() {
  return Date.now()
}

function setStatus(status) {
  if (state.status === status) return
  state.status = status
  for (const cb of [...state.statusCbs]) {
    try {
      cb(status)
    } catch (e) {
      console.error('[push] 状态回调异常', e)
    }
  }
}

function notify(text) {
  for (const cb of [...state.noticeCbs]) {
    try {
      cb(text)
    } catch (e) {
      console.error('[push] 提示回调异常', e)
    }
  }
}

/** 某通道当前应订阅的 topic 全集 */
function topicsOf(channelName) {
  return [...state.refs.keys()].filter(t => channelOfTopic(t) === channelName)
}

/** 某通道的心跳/看门狗参数（按服务端下发的分通道配置） */
function pingMsOf(channelName) {
  return channelName === CHANNEL_TRADE
    ? Number(state.cfg.ping_interval_trade_ms) || DEFAULT_CONFIG.ping_interval_trade_ms
    : Number(state.cfg.ping_interval_quote_ms) || DEFAULT_CONFIG.ping_interval_quote_ms
}

/** 派发一次事件到该 topic 的所有订阅者 */
function dispatch(topic, payload, event) {
  const cbs = state.handlers.get(topic)
  if (!cbs) return
  for (const cb of [...cbs]) {
    try {
      cb(payload, event)
    } catch (e) {
      console.error(`[push] 处理 ${topic} 事件异常`, e)
    }
  }
}

/** 记录交易类帧的游标与在途集合（补查依据 + 实体守卫输入） */
function noteTradeFrame(topic, payload) {
  const data = payload?.data || {}
  if (topic === 'order') {
    const entno = String(data.entno || payload.cursor || '')
    if (entno) {
      state.cursors.note(entno, undefined)
      if (isTerminalOrder(data)) state.openOrders.delete(entno)
      else state.openOrders.set(entno, String(data.status || ''))
    }
  } else if (topic === 'trade') {
    const tdno = String(data.tdno || payload.cursor || '')
    if (tdno) state.cursors.note(undefined, tdno)
  }
}

/** 给某通道的 EventSource 注册事件监听（幂等）：数据事件 + 控制事件 */
function ensureListeners(ch) {
  const es = ch.es
  if (!es) return
  const addDataListener = type => {
    if (!type || es.__types.has(type)) return
    es.__types.add(type)
    es.addEventListener(type, e => {
      ch.lastEventAt = now()
      let payload
      try {
        payload = JSON.parse(e.data)
      } catch (err) {
        console.error('[push] 事件解析失败', type, e.data)
        return
      }
      ch.lastFrameTs = Number(payload.ts) || ch.lastFrameTs
      ch.lagMs = frameLagMs(payload.ts)
      const topic = payload.topic || type
      if (topic === 'order' || topic === 'trade') noteTradeFrame(topic, payload)
      dispatch(topic, payload, type)
    })
  }

  // 数据事件名 = topic 类型
  for (const topic of topicsOf(ch.name)) addDataListener(topic.split(':')[0])
  addDataListener(EVENT_UPSTREAM_ERROR)

  if (!es.__controlBound) {
    es.__controlBound = true

    es.addEventListener(EVENT_READY, e => {
      ch.lastEventAt = now()
      let info = {}
      try {
        info = JSON.parse(e.data)
      } catch (err) {
        console.error('[push] ready 解析失败', e.data)
      }
      ch.streamId = info.stream_id || ''
      reconcileTopics(ch)
      ch.openedOnce = true
      runRecovery(ch)
    })

    // 心跳是普通事件帧：更新活性 + 用它的 ts 算滞后（客户端据此自测"我落后了多久"）
    es.addEventListener(EVENT_PING, e => {
      ch.lastEventAt = now()
      try {
        ch.lagMs = frameLagMs(JSON.parse(e.data).ts)
      } catch {
        /* 心跳解析失败不影响活性判定 */
      }
    })

    // 服务端主动断开（溢出/慢消费者/写超时）：知情 -> 立即重连 + 重连后补查
    es.addEventListener(EVENT_CLOSING, e => {
      let reason = 'closing'
      try {
        reason = JSON.parse(e.data).reason || reason
      } catch {
        /* 用默认原因 */
      }
      console.warn(`[push] 服务端主动断开(${reason})，立即重连`)
      notify(`推送通道已重建（${reason}）`)
      handleFailure(ch, reason, 0)
    })

    es.addEventListener(EVENT_RESULT, e => {
      let payload
      try {
        payload = JSON.parse(e.data)
      } catch {
        return
      }
      ch.lastEventAt = now()
      const pending = ch.pending.get(payload.req_id)
      if (!pending) return
      ch.pending.delete(payload.req_id)
      clearTimeout(pending.timer)
      pending.resolve(payload)
    })
  }
}

/** 取一次性握手票据（EventSource 不能带请求头，token 不能直接进 URL） */
async function fetchTicket() {
  const { data } = await request.post('/api/stream/ticket', null, { silent: true, timeout: 5000 })
  return (data && data.ticket) || ''
}

/** 控制面 POST（切标的/退订）；stream_id 失效则立即重建该通道连接 */
function sendControl(ch, path, topics) {
  if (!ch.streamId || !topics.length) return
  request.post(path, { stream_id: ch.streamId, topics }, { silent: true, timeout: 5000 })
    .catch(e => {
      if (e.response?.status === 404) {
        console.warn(`[push:${ch.name}] stream_id 已失效，重建连接`)
        dropConnection(ch)
        scheduleReconnect(ch, 0)
      } else if (e.response?.status === 503) {
        ch.status = 'degraded'
        recomputeStatus()
      }
    })
}

/** 关闭某通道的连接（不改 status；由调用方决定后续状态） */
function dropConnection(ch) {
  if (ch.es) {
    ch.es.onopen = null
    ch.es.onerror = null
    ch.es.close()
    ch.es = null
  }
  ch.streamId = ''
  ch.urlTopics = []
  ch.confirmed.clear()
  for (const [, pending] of ch.pending) {
    clearTimeout(pending.timer)
    pending.reject(new Error('connection dropped'))
  }
  ch.pending.clear()
}

/** 安排某通道重连（delayMs 省略时用退避值） */
function scheduleReconnect(ch, delayMs) {
  if (ch.retryTimer) return
  const delay = delayMs === undefined ? ch.retryDelay : delayMs
  ch.retryDelay = Math.min(Math.max(ch.retryDelay * 2, RETRY_MIN_MS), RETRY_MAX_MS)
  ch.retryTimer = setTimeout(() => {
    ch.retryTimer = null
    connect(ch)
  }, delay)
}

/** 连接失败/断开/被服务端关闭后的统一处理 */
function handleFailure(ch, reason, immediateDelay) {
  dropConnection(ch)
  ch.status = 'connecting'
  recomputeStatus()
  if (!ch.firstFailAt) ch.firstFailAt = now()
  if (reason && reason !== 'error') console.warn(`[push:${ch.name}] 连接不可用(${reason})`)
  const elapsed = now() - ch.firstFailAt
  if (elapsed >= state.cfg.degraded_after_ms) {
    ch.status = 'degraded'
    recomputeStatus()
  } else if (!ch.degradeTimer) {
    ch.degradeTimer = setTimeout(() => {
      ch.degradeTimer = null
      if (ch.status !== 'open') {
        ch.status = 'degraded'
        recomputeStatus()
      }
    }, Math.max(0, state.cfg.degraded_after_ms - elapsed))
  }
  // closing / 显式重建走立即重连（服务端已告知原因，不需要退避等待）
  scheduleReconnect(ch, immediateDelay)
}

/** ready 到达后对齐订阅集：URL 里已被退订的发退订，建流期间新订阅的补订阅（避免竞态丢订阅） */
function reconcileTopics(ch) {
  ch.confirmed = new Set(ch.urlTopics.filter(t => state.refs.has(t)))
  const stale = ch.urlTopics.filter(t => !state.refs.has(t))
  if (stale.length) sendControl(ch, '/api/stream/unsubscribe', stale)
  const missing = topicsOf(ch.name).filter(t => !ch.confirmed.has(t))
  if (missing.length) {
    sendControl(ch, '/api/stream/subscribe', missing)
    missing.forEach(t => ch.confirmed.add(t))
  }
}

/** 建立（或重建）某通道的 SSE 连接：URL 带该通道的 topic 全集 + channel 参数 */
async function connect(ch) {
  if (ch.es || ch.connecting) return
  const topics = topicsOf(ch.name)
  if (!topics.length) {
    ch.status = 'idle'
    recomputeStatus()
    return
  }
  ch.connecting = true
  try {
    const cfg = await loadConfig()
    if (!cfg.enabled) {
      ch.status = 'degraded'
      recomputeStatus()
      return
    }
    const ticket = await fetchTicket()
    if (!ticket) throw new Error('ticket 获取失败')
    ch.urlTopics = topics
    const base = request.defaults.baseURL || ''
    const url = `${base}/api/stream?channel=${ch.name}`
      + `&topics=${encodeURIComponent(ch.urlTopics.join(','))}`
      + `&ticket=${encodeURIComponent(ticket)}`
    const es = new EventSource(url)
    es.__types = new Set()
    ch.es = es
    ensureListeners(ch)
    es.onopen = () => {
      ch.firstFailAt = 0
      ch.retryDelay = RETRY_MIN_MS
      ch.lastEventAt = now()
      if (ch.degradeTimer) {
        clearTimeout(ch.degradeTimer)
        ch.degradeTimer = null
      }
      ch.status = 'open'
      recomputeStatus()
    }
    es.onerror = () => handleFailure(ch, 'error')
  } catch (e) {
    const status503 = e.response?.status === 503
    if (status503) {
      ch.status = 'degraded'
      recomputeStatus()
      scheduleReconnect(ch, DISABLED_RETRY_MS)
    } else {
      handleFailure(ch, e.message || 'connect')
    }
  } finally {
    ch.connecting = false
  }
}

/** 聚合状态：页面只看这一个（所有"有订阅的通道"都 open 才算 open） */
function recomputeStatus() {
  const active = CHANNEL_DEFS.filter(n => topicsOf(n).length > 0)
  if (!active.length) return setStatus('idle')
  if (active.some(n => state.channels[n].status === 'degraded')) return setStatus('degraded')
  if (active.every(n => state.channels[n].status === 'open')) return setStatus('open')
  return setStatus('connecting')
}

/** 看门狗：某通道长时间收不到任何帧（含心跳）→ 重建该通道连接（阈值 = 2× 心跳） */
function startWatchdog() {
  if (state.watchdogTimer) return
  state.watchdogTimer = setInterval(() => {
    for (const name of CHANNEL_DEFS) {
      const ch = state.channels[name]
      if (ch.status !== 'open' || !topicsOf(name).length) continue
      if (now() - ch.lastEventAt > watchdogMsFor(pingMsOf(name))) {
        console.warn(`[push:${name}] 看门狗超时（${now() - ch.lastEventAt}ms 无帧），重建连接`)
        handleFailure(ch, 'watchdog')
      }
    }
  }, WATCHDOG_TICK_MS)
}

/** 发一次通道内补查（req_id 配对；结果沿同一条流回流） */
function queryOn(ch, what, opts = {}) {
  return new Promise((resolve, reject) => {
    if (!ch.streamId) {
      reject(new Error('stream not ready'))
      return
    }
    const reqId = ch.nextReqId++
    const timer = setTimeout(() => {
      ch.pending.delete(reqId)
      reject(new Error('query timeout'))
    }, QUERY_TIMEOUT_MS)
    ch.pending.set(reqId, { resolve, reject, timer })
    request.post('/api/stream/query', {
      stream_id: ch.streamId,
      req_id: reqId,
      what,
      cursor: opts.cursor,
      entnos: opts.entnos,
      limit: opts.limit
    }, { silent: true, timeout: QUERY_TIMEOUT_MS }).catch(e => {
      const pending = ch.pending.get(reqId)
      if (!pending) return
      ch.pending.delete(reqId)
      clearTimeout(timer)
      reject(e)
    })
  })
}

/**
 * 通道内补查（对外 API）。委托/成交/策略指标都走 trade 通道。
 * @returns Promise<{req_id, what, count, has_more, data}>
 */
export function query(what, opts = {}) {
  return queryOn(state.channels[CHANNEL_TRADE], what, opts)
}

/** 重连后的补查编排（rev4 §4）：先批量刷在途委托，再补离线期间的增量 */
async function runRecovery(ch) {
  if (ch.name !== CHANNEL_TRADE) return
  const plan = buildRecoveryPlan(
    { lastEntno: state.cursors.lastEntno, lastTdno: state.cursors.lastTdno },
    [...state.openOrders.keys()]
  )
  if (!plan.length) return
  for (const step of plan) {
    try {
      const res = await queryOn(ch, step.what, { cursor: step.cursor, entnos: step.entnos })
      const rows = res.data || []
      if (!rows.length) continue
      // 补查结果当作"事件"派发给订阅者：消费者用同一套实体守卫处理，不会重复/倒挂
      const topic = step.what === 'trades' ? 'trade' : 'order'
      const event = topic
      for (const row of rows) {
        const payload = topic === 'trade'
          ? { topic, ts: res.ts, cursor: String(row.tdno || ''), recovered: true, data: row }
          : { topic, ts: res.ts, cursor: String(row.entno || ''), status_id: row.status_id, recovered: true, data: row }
        noteTradeFrame(topic, payload)
        dispatch(topic, payload, event)
      }
      console.info(`[push] 重连补查 ${step.what} 补齐 ${rows.length} 条`)
    } catch (e) {
      console.warn(`[push] 重连补查失败 what=${step.what}`, e.message || e)
    }
  }
}

/** 拉取服务端推送配置（只拉一次；失败则用兜底值，不阻塞推送建立） */
export function loadConfig() {
  if (state.cfgPromise) return state.cfgPromise
  state.cfgPromise = request.get('/api/settings/push', { silent: true, timeout: 5000 })
    .then(({ data }) => {
      state.cfg = { ...DEFAULT_CONFIG, ...((data && data.data) || {}) }
      return state.cfg
    })
    .catch(() => state.cfg)
  return state.cfgPromise
}

/** 当前生效的推送配置（未拉取到时为兜底值） */
export function getConfig() {
  return state.cfg
}

/** 订阅 topic（通道由 topic 推导）；同一 topic 多人订阅只建立一份上游采样 */
export function subscribe(topic, handler) {
  if (!topic) return
  const refs = state.refs.get(topic) || 0
  state.refs.set(topic, refs + 1)
  if (handler) {
    if (!state.handlers.has(topic)) state.handlers.set(topic, new Set())
    state.handlers.get(topic).add(handler)
  }
  const ch = state.channels[channelOfTopic(topic)]
  if (ch.es) {
    ensureListeners(ch)
    if (refs === 0 && !ch.confirmed.has(topic)) {
      // 连接已建立：走控制面增量订阅（ready 前 streamId 为空，留给 reconcileTopics 补）
      sendControl(ch, '/api/stream/subscribe', [topic])
      ch.confirmed.add(topic)
    }
  } else {
    connect(ch)
  }
  startWatchdog()
}

/** 退订 topic（幂等）；引用计数归零时通知服务端停止该 topic 的上游采样 */
export function unsubscribe(topic, handler) {
  if (!topic) return
  const cbs = state.handlers.get(topic)
  if (cbs && handler) cbs.delete(handler)
  const refs = (state.refs.get(topic) || 0) - 1
  if (refs > 0) {
    state.refs.set(topic, refs)
    return
  }
  state.refs.delete(topic)
  state.handlers.delete(topic)
  const ch = state.channels[channelOfTopic(topic)]
  if (ch.confirmed.delete(topic)) sendControl(ch, '/api/stream/unsubscribe', [topic])
  recomputeStatus()
}

/** 注册连接状态监听；返回注销函数。status: idle|connecting|open|degraded */
export function onStatus(cb) {
  state.statusCbs.add(cb)
  cb(state.status)
  return () => state.statusCbs.delete(cb)
}

/** 注册"通道被服务端重建/上游异常"等提示；返回注销函数 */
export function onNotice(cb) {
  state.noticeCbs.add(cb)
  return () => state.noticeCbs.delete(cb)
}

/** 当前聚合连接状态 */
export function getStatus() {
  return state.status
}

/** 各通道明细状态（诊断/调试） */
export function getChannelStatus() {
  return Object.fromEntries(CHANNEL_DEFS.map(n => [n, {
    status: state.channels[n].status,
    topics: topicsOf(n),
    lagMs: state.channels[n].lagMs,
    streamId: state.channels[n].streamId
  }]))
}

/** 帧滞后（毫秒）：服务端 ts 到本地现在的差值；无法判定时为 null */
export function getLagMs(channel = CHANNEL_TRADE) {
  return state.channels[channel]?.lagMs ?? null
}

/** 推送是否可用（页面据此决定是否启用轮询兜底） */
export function isOpen() {
  return state.status === 'open'
}

/** 当前已订阅的 topic（诊断用） */
export function getTopics() {
  return [...state.refs.keys()]
}

export default {
  subscribe,
  unsubscribe,
  onStatus,
  onNotice,
  getStatus,
  getChannelStatus,
  getLagMs,
  isOpen,
  getTopics,
  query,
  loadConfig,
  getConfig
}
