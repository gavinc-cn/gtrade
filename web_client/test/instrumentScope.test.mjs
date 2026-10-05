/**
 * 设置页「标的范围」纯逻辑单测（Node 内置 test runner，无需浏览器/依赖）。
 *
 * 运行：cd web_client && node --test test/
 *
 * 覆盖正确性关键的判定：选择键三元组形状、订阅状态三档筛选（含与类型/关键词叠加）、
 * 右栏「已订阅」行的顺序与「不在当前清单」补行（勾着但看不到 = 无法退订的死角）。
 */
import test from 'node:test'
import assert from 'node:assert/strict'

import {
  STATUS_ALL,
  STATUS_STRATEGY,
  STATUS_SUBSCRIBED,
  STATUS_UNSUBSCRIBED,
  buildStrategyCounts,
  buildSubscribedRows,
  filterInstruments,
  parseScopeKey,
  scopeKey
} from '../src/utils/instrumentScope.js'

/** 测试用标的清单：含同 instId 不同类型的场景（现货/杠杆共用 instId 的历史约束） */
const INSTRUMENTS = [
  { market: 'okx', inst_id: 'BTC-USDT', inst_type: 'SPOT' },
  { market: 'okx', inst_id: 'BTC-USDT', inst_type: 'MARGIN' },
  { market: 'okx', inst_id: 'BTC-USDT-SWAP', inst_type: 'SWAP' },
  { market: 'okx_dummy', inst_id: 'eth-usdt', inst_type: 'SPOT' }
]

test('选择键：三元组含 inst_type，空类型保留空段且可逆', () => {
  assert.equal(scopeKey({ market: 'okx', inst_id: 'BTC-USDT', inst_type: 'SPOT' }), 'okx|BTC-USDT|SPOT')
  assert.equal(scopeKey({ market: 'ctp', inst_id: 'rb2510' }), 'ctp|rb2510|')
  assert.deepEqual(parseScopeKey('ctp|rb2510|'), { market: 'ctp', inst_id: 'rb2510', inst_type: '' })
  // 同 instId 不同 inst_type 必须是不同的键（否则一个勾选框管两条、Vue :key 重复）
  assert.notEqual(
    scopeKey({ market: 'okx', inst_id: 'BTC-USDT', inst_type: 'SPOT' }),
    scopeKey({ market: 'okx', inst_id: 'BTC-USDT', inst_type: 'MARGIN' })
  )
})

test('策略持有数：按 owned[].strategies 长度统计，无策略记 0', () => {
  const counts = buildStrategyCounts([
    { market: 'okx', inst_id: 'BTC-USDT', inst_type: 'SPOT', strategies: ['s1', 's2'] },
    { market: 'okx', inst_id: 'BTC-USDT-SWAP', inst_type: 'SWAP', strategies: [] }
  ])
  assert.equal(counts['okx|BTC-USDT|SPOT'], 2)
  assert.equal(counts['okx|BTC-USDT-SWAP|SWAP'], 0)
  assert.deepEqual(buildStrategyCounts(undefined), {})
})

test('筛选：默认全部返回，关键词不区分大小写且只匹配 inst_id', () => {
  assert.equal(filterInstruments(INSTRUMENTS, []).length, 4)
  assert.deepEqual(
    filterInstruments(INSTRUMENTS, [], { keyword: 'btc-usdt' }).map(i => i.inst_id),
    ['BTC-USDT', 'BTC-USDT', 'BTC-USDT-SWAP']
  )
  assert.deepEqual(
    filterInstruments(INSTRUMENTS, [], { keyword: 'eth' }).map(i => i.market),
    ['okx_dummy']
  )
  // 类型筛选与关键词叠加（BTC-USDT 前缀命中 SWAP，但 SPOT 类型把它滤掉）
  assert.deepEqual(
    filterInstruments(INSTRUMENTS, [], { keyword: 'BTC-USDT', typeFilter: 'SPOT' }).map(scopeKey),
    ['okx|BTC-USDT|SPOT']
  )
})

test('筛选：已订阅 / 未订阅按当前勾选判定（同 instId 不同类型互不影响）', () => {
  const checked = ['okx|BTC-USDT|SPOT', 'okx|BTC-USDT-SWAP|SWAP']
  assert.deepEqual(
    filterInstruments(INSTRUMENTS, checked, { statusFilter: STATUS_SUBSCRIBED }).map(scopeKey),
    checked
  )
  assert.deepEqual(
    filterInstruments(INSTRUMENTS, checked, { statusFilter: STATUS_UNSUBSCRIBED }).map(scopeKey),
    ['okx|BTC-USDT|MARGIN', 'okx_dummy|eth-usdt|SPOT']
  )
  // 全部 = 未筛选（与已订阅/未订阅两档之和一致）
  assert.equal(filterInstruments(INSTRUMENTS, checked, { statusFilter: STATUS_ALL }).length, 4)
})

test('筛选：策略在用取 strategyCounts > 0 的标的', () => {
  const counts = buildStrategyCounts([
    { market: 'okx', inst_id: 'BTC-USDT-SWAP', inst_type: 'SWAP', strategies: ['s1'] }
  ])
  assert.deepEqual(
    filterInstruments(INSTRUMENTS, [], { statusFilter: STATUS_STRATEGY, strategyCounts: counts }).map(scopeKey),
    ['okx|BTC-USDT-SWAP|SWAP']
  )
})

test('右栏已订阅：按清单顺序展开勾选集合，不受筛选影响', () => {
  const checked = ['okx|BTC-USDT-SWAP|SWAP', 'okx|BTC-USDT|SPOT']
  const rows = buildSubscribedRows(INSTRUMENTS, checked, { 'okx|BTC-USDT|SPOT': 2 })
  assert.deepEqual(rows.map(r => r.key), ['okx|BTC-USDT|SPOT', 'okx|BTC-USDT-SWAP|SWAP'])
  assert.equal(rows[0].strategyCount, 2)
  assert.equal(rows[0].missing, false)
  assert.equal(rows[1].strategyCount, 0)
})

test('右栏已订阅：清单外的勾选条目补行并标 missing（否则勾着却无法移除）', () => {
  const rows = buildSubscribedRows(INSTRUMENTS, ['ctp|rb2510|', 'okx|BTC-USDT|SPOT'])
  assert.equal(rows.length, 2)
  const ghost = rows.find(r => r.key === 'ctp|rb2510|')
  assert.equal(ghost.missing, true)
  assert.deepEqual(
    { market: ghost.market, inst_id: ghost.inst_id, inst_type: ghost.inst_type },
    { market: 'ctp', inst_id: 'rb2510', inst_type: '' }
  )
})

test('右栏已订阅：空勾选返回空数组（渲染空态）', () => {
  assert.deepEqual(buildSubscribedRows(INSTRUMENTS, []), [])
})
