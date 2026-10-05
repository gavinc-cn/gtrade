/**
 * 设置页「标的范围」纯逻辑：与 Vue 组件解耦，可被 `node --test` 直接验证。
 *
 * 页面语义：勾选集合 = **待保存的订阅范围**（保存为全量替换：未勾选的会退订，
 * 策略仍在用的由引擎按 owner 保留）。因此「已订阅」以当前勾选为准，
 * 勾选/取消即时反映到状态筛选与右栏「已订阅」区域。
 */

/** 订阅状态筛选取值：全部 / 已订阅（在勾选集合内）/ 未订阅 / 策略在用（有策略 owner） */
export const STATUS_ALL = 'ALL'
export const STATUS_SUBSCRIBED = 'SUBSCRIBED'
export const STATUS_UNSUBSCRIBED = 'UNSUBSCRIBED'
export const STATUS_STRATEGY = 'STRATEGY'

/** 状态下拉选项（视图直接渲染，避免与筛选值两处手写） */
export const STATUS_OPTIONS = [
  { value: STATUS_ALL, label: '全部状态' },
  { value: STATUS_SUBSCRIBED, label: '已订阅' },
  { value: STATUS_UNSUBSCRIBED, label: '未订阅' },
  { value: STATUS_STRATEGY, label: '策略在用' }
]

/**
 * 标的/范围条目 → 选择键（三元组字符串）。
 * inst_type 是标的唯一性的一部分（OKX 币币杠杆复用现货的 instId 与 instIdCode，只有 instType 能区分），
 * 二元键会让同 instId 的两条共用一个勾选框、Vue 的 :key 也会重复；'|' 不会出现在三段取值里。
 * @param {{market:string, inst_id:string, inst_type?:string}} item 标的或范围条目
 * @returns {string} "market|inst_id|inst_type"
 */
export function scopeKey(item) {
  return `${item.market}|${item.inst_id}|${item.inst_type || ''}`
}

/**
 * 选择键 → 三段字段（scopeKey 的逆运算，供清单外条目回显）。
 * @param {string} key 选择键
 * @returns {{market:string, inst_id:string, inst_type:string}}
 */
export function parseScopeKey(key) {
  const [market, inst_id, inst_type] = String(key).split('|')
  return { market, inst_id, inst_type }
}

/**
 * 引擎返回的 owned[] → { 选择键: 策略持有数 }。
 * owned 里 strategies 为空数组表示该行只有范围 owner（无策略在用）。
 * @param {Array<{market:string, inst_id:string, inst_type?:string, strategies?:Array}>} owned
 * @returns {Object<string, number>}
 */
export function buildStrategyCounts(owned) {
  const counts = {}
  for (const o of (owned || [])) {
    counts[scopeKey(o)] = (o.strategies || []).length
  }
  return counts
}

/**
 * 左栏列表筛选：关键词（不区分大小写，只匹配 inst_id）∧ 标的类型 ∧ 订阅状态。
 * @param {Array<{market:string, inst_id:string, inst_type?:string}>} instruments 标的清单
 * @param {Array<string>} checkedKeys 当前勾选（待保存订阅范围）的选择键
 * @param {{keyword?:string, typeFilter?:string, statusFilter?:string, strategyCounts?:Object}} [options]
 * @returns {Array} 命中的标的（保持入参顺序）
 */
export function filterInstruments(instruments, checkedKeys, options = {}) {
  const { keyword = '', typeFilter = STATUS_ALL, statusFilter = STATUS_ALL, strategyCounts = {} } = options
  const checked = new Set(checkedKeys || [])
  const kw = String(keyword).trim().toUpperCase()
  return (instruments || []).filter(i => {
    if (typeFilter !== STATUS_ALL && i.inst_type !== typeFilter) return false
    const key = scopeKey(i)
    // 状态判定先于关键词，任一不匹配即淘汰
    if (statusFilter === STATUS_SUBSCRIBED && !checked.has(key)) return false
    if (statusFilter === STATUS_UNSUBSCRIBED && checked.has(key)) return false
    if (statusFilter === STATUS_STRATEGY && !strategyCounts[key]) return false
    return !kw || String(i.inst_id).toUpperCase().includes(kw)
  })
}

/**
 * 右栏「已订阅」行：以勾选集合为准，**不受左栏筛选影响**（否则筛选一变右侧就跟着少几条）。
 * 先按标的清单顺序展开；勾选里若存在不在清单中的条目（引擎从 DB 恢复的范围条目，
 * 其市场信息未加载时会不在 instruments 里），补一行并置 missing=true，
 * 保证任何已勾选条目都能在右栏被移除，不出现「勾着但看不到」的死角。
 * @param {Array<{market:string, inst_id:string, inst_type?:string}>} instruments 标的清单
 * @param {Array<string>} checkedKeys 当前勾选的选择键
 * @param {Object<string, number>} [strategyCounts] 选择键 → 策略持有数
 * @returns {Array<{key:string, market:string, inst_id:string, inst_type:string, strategyCount:number, missing:boolean}>}
 */
export function buildSubscribedRows(instruments, checkedKeys, strategyCounts = {}) {
  const checked = new Set(checkedKeys || [])
  const rows = []
  const seen = new Set()
  for (const i of (instruments || [])) {
    const key = scopeKey(i)
    if (!checked.has(key)) continue
    seen.add(key)
    rows.push({
      key,
      market: i.market,
      inst_id: i.inst_id,
      inst_type: i.inst_type || '',
      strategyCount: strategyCounts[key] || 0,
      missing: false
    })
  }
  for (const key of (checkedKeys || [])) {
    if (seen.has(key)) continue
    const { market, inst_id, inst_type } = parseScopeKey(key)
    rows.push({ key, market, inst_id, inst_type, strategyCount: strategyCounts[key] || 0, missing: true })
  }
  return rows
}
