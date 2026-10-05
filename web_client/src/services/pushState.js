/**
 * 推送通道的**纯逻辑**：通道归属、实体守卫、游标持久化、滞后计算、补查计划。
 *
 * 单独成文件的原因：这些是"正确性关键"的部分（漏一条成交 / 用旧状态覆盖新状态），
 * 但它们不依赖浏览器与 axios，可以在 Node 下直接单测（见 web_client/test/pushState.test.mjs）。
 * 契约见 doc_ai/plan/202610/20261004_1300_二通道推送与补查方案_rev4.md §3/§4。
 */

/** 通道名（与服务端 web_server/push_hub.py 的 CHANNEL_* 对齐） */
export const CHANNEL_QUOTE = 'quote'
export const CHANNEL_TRADE = 'trade'

/** 由 topic 推导所属通道：盘口快照走 quote，其余（策略指标/委托/成交/持仓/资金）走 trade */
export function channelOfTopic(topic) {
  if (typeof topic !== 'string') return CHANNEL_TRADE
  return topic.startsWith('depth:') ? CHANNEL_QUOTE : CHANNEL_TRADE
}

/**
 * 实体守卫：委托只接受"更新"的版本。
 *
 * 服务端每次收到确认回报都会 `++status_id`（引擎侧单调），因此它可以判定新旧；
 * 断线补查与推送可能交叠，没有这道守卫就可能用旧状态覆盖新状态。
 *
 * @param prev 已持有的委托行（可为 undefined）
 * @param incoming 新到的委托行
 * @returns {boolean} 是否应采纳
 */
export function acceptOrder(prev, incoming) {
  if (!incoming) return false
  if (!prev) return true
  const prevId = Number(prev.status_id ?? -1)
  const nextId = Number(incoming.status_id ?? -1)
  // status_id 缺失（老数据/外系统委托）时退回"终态优先 + 只前进不后退"的保守规则
  if (!Number.isFinite(prevId) || !Number.isFinite(nextId) || prevId < 0 || nextId < 0) {
    return String(prev.status || '') !== String(incoming.status || '')
  }
  return nextId >= prevId
}

/** 订单终态（与服务端 _TERMINAL_ORDER_STATUS 对齐）：终态后仍可收到补查结果，但不会再有新状态 */
export const TERMINAL_ORDER_STATUS = new Set(['4', '6', '8', '9'])

export function isTerminalOrder(order) {
  return TERMINAL_ORDER_STATUS.has(String(order?.status ?? ''))
}

/**
 * 实体守卫：成交按 tdno 去重（tdno 是引擎本地单调号，全局唯一）。
 *
 * @param seen Set<string> 已处理的 tdno（本地维护，可裁剪）
 * @param tdno 新到的成交号
 * @param maxSize 上限（超过时按插入顺序裁剪，避免无限增长）
 * @returns {boolean} 是否应采纳（false = 重复）
 */
export function acceptTrade(seen, tdno, maxSize = 2000) {
  const key = tdno === undefined || tdno === null ? '' : String(tdno)
  if (!key || key === '0') return false
  if (seen.has(key)) return false
  seen.add(key)
  if (seen.size > maxSize) {
    // Set 保持插入顺序：删掉最早的若干条
    const drop = seen.size - maxSize
    let i = 0
    for (const k of seen) {
      seen.delete(k)
      if (++i >= drop) break
    }
  }
  return true
}

/**
 * 帧滞后：服务端 `ts`（epoch ms）到本地现在的差值。
 * 服务端没给 ts（老版本/过渡期）时返回 null，调用方应视为"无法判定"而非"落后 0"。
 */
export function frameLagMs(frameTs, nowMs = Date.now()) {
  const ts = Number(frameTs)
  if (!Number.isFinite(ts) || ts <= 0) return null
  return Math.max(0, nowMs - ts)
}

/** 游标存储：last_entno / last_tdno。优先 localStorage，不可用时退化为内存（Node 测试/隐私模式） */
export function createCursorStore(storage, key = 'gtrade.push.cursors') {
  const mem = { last_entno: 0, last_tdno: 0 }
  let backing = null
  try {
    backing = storage || (typeof localStorage !== 'undefined' ? localStorage : null)
  } catch {
    backing = null
  }
  if (backing) {
    try {
      const raw = backing.getItem(key)
      if (raw) Object.assign(mem, JSON.parse(raw))
    } catch {
      /* 解析失败按空游标处理 */
    }
  }
  const persist = () => {
    if (!backing) return
    try {
      backing.setItem(key, JSON.stringify({ last_entno: mem.last_entno, last_tdno: mem.last_tdno }))
    } catch {
      /* 配额/隐私模式：忽略 */
    }
  }
  return {
    get lastEntno() {
      return mem.last_entno
    },
    get lastTdno() {
      return mem.last_tdno
    },
    /** 用一帧的游标推进水位（只前进，不后退——补查与推送交叠时必须单调） */
    note(entno, tdno) {
      let changed = false
      const e = Number(entno)
      if (Number.isFinite(e) && e > mem.last_entno) {
        mem.last_entno = e
        changed = true
      }
      const t = Number(tdno)
      if (Number.isFinite(t) && t > mem.last_tdno) {
        mem.last_tdno = t
        changed = true
      }
      if (changed) persist()
    },
    reset() {
      mem.last_entno = 0
      mem.last_tdno = 0
      persist()
    }
  }
}

/**
 * 断线重连后的补查计划（rev4 §4）。
 *
 * @param cursors {lastEntno, lastTdno}
 * @param openEntnos 客户端已知的在途委托号（数组；终态的不必带）
 * @returns [{what, cursor?, entnos?}]，按"先快照后增量"的顺序
 */
export function buildRecoveryPlan(cursors, openEntnos = []) {
  const plan = []
  const entnos = (openEntnos || []).map((v) => String(v)).filter((v) => v && v !== '0').slice(0, 500)
  if (entnos.length) {
    plan.push({ what: 'orders', entnos })          // 已知在途：批量刷最新状态（含终态）
  }
  if (Number(cursors?.lastEntno) > 0) {
    plan.push({ what: 'orders', cursor: Number(cursors.lastEntno) })   // 离线期间新增
  }
  if (Number(cursors?.lastTdno) > 0) {
    plan.push({ what: 'trades', cursor: Number(cursors.lastTdno) })    // 离线期间成交
  }
  return plan
}

/** 看门狗阈值：2× 心跳周期（rev4 §2），下限 3s 防止配置异常导致抖动 */
export function watchdogMsFor(pingMs) {
  const ping = Number(pingMs)
  const base = Number.isFinite(ping) && ping > 0 ? ping : 10000
  return Math.max(3000, base * 2)
}
