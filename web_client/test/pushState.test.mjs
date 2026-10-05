/**
 * 推送通道纯逻辑单测（Node 内置 test runner，无需浏览器/依赖）。
 *
 * 运行：cd web_client && npm test（即 node --test test/*.test.mjs；Node 24 下 node --test test/ 会把目录当模块解析）
 *
 * 覆盖的是"正确性关键"的判定：实体守卫（漏一条成交 / 用旧状态覆盖新状态）、
 * 游标单调与持久化、补查计划、看门狗阈值、帧滞后。
 */
import test from 'node:test'
import assert from 'node:assert/strict'

import {
  CHANNEL_QUOTE,
  CHANNEL_TRADE,
  acceptOrder,
  acceptTrade,
  buildRecoveryPlan,
  channelOfTopic,
  createCursorStore,
  frameLagMs,
  isTerminalOrder,
  watchdogMsFor
} from '../src/services/pushState.js'

/** 内存版 localStorage（Node 无 localStorage） */
function fakeStorage() {
  const map = new Map()
  return {
    getItem: (k) => (map.has(k) ? map.get(k) : null),
    setItem: (k, v) => map.set(k, String(v)),
    _dump: () => Object.fromEntries(map)
  }
}

test('通道归属：depth 走 quote，其余走 trade', () => {
  assert.equal(channelOfTopic('depth:okx:BTC-USDT'), CHANNEL_QUOTE)
  assert.equal(channelOfTopic('strategies'), CHANNEL_TRADE)
  assert.equal(channelOfTopic('order'), CHANNEL_TRADE)
  assert.equal(channelOfTopic('trade'), CHANNEL_TRADE)
  assert.equal(channelOfTopic(undefined), CHANNEL_TRADE)
})

test('守卫：委托只接受更新的 status_id（防补查与推送交叠时倒挂）', () => {
  const prev = { status_id: 3, status: '2' }
  assert.equal(acceptOrder(prev, { status_id: 4, status: '3' }), true, '更新应采纳')
  assert.equal(acceptOrder(prev, { status_id: 3, status: '2' }), true, '同版本可幂等重放')
  assert.equal(acceptOrder(prev, { status_id: 2, status: '1' }), false, '旧版本必须拒绝')
  assert.equal(acceptOrder(undefined, { status_id: 1 }), true, '新委托应采纳')
  assert.equal(acceptOrder(prev, null), false, '空行拒绝')
})

test('守卫：status_id 缺失时退回保守规则（状态变了才采纳）', () => {
  const prev = { status: '2' }
  assert.equal(acceptOrder(prev, { status: '3' }), true)
  assert.equal(acceptOrder(prev, { status: '2' }), false)
})

test('终态判定与补查的终态语义一致', () => {
  for (const s of ['4', '6', '8', '9']) assert.equal(isTerminalOrder({ status: s }), true, s)
  for (const s of ['1', '2', '3']) assert.equal(isTerminalOrder({ status: s }), false, s)
})

test('守卫：成交按 tdno 去重，且集合有上限', () => {
  const seen = new Set()
  assert.equal(acceptTrade(seen, '1001'), true)
  assert.equal(acceptTrade(seen, '1001'), false, '重复 tdno 必须拒绝')
  assert.equal(acceptTrade(seen, '1002'), true)
  assert.equal(acceptTrade(seen, 0), false, 'tdno=0 无效')
  assert.equal(acceptTrade(seen, undefined), false, '缺 tdno 无效')

  const small = new Set()
  for (let i = 1; i <= 10; i++) acceptTrade(small, String(i), 5)
  assert.equal(small.size, 5, '超过上限应裁剪')
  assert.equal(small.has('10'), true, '保留最新的')
  assert.equal(small.has('1'), false, '裁掉最早的')
})

test('游标：单调前进、可持久化、可恢复', () => {
  const storage = fakeStorage()
  const store = createCursorStore(storage)
  store.note('1000', '2000')
  assert.equal(store.lastEntno, 1000)
  assert.equal(store.lastTdno, 2000)

  store.note('900', '1900')          // 回退的游标必须被忽略
  assert.equal(store.lastEntno, 1000)
  assert.equal(store.lastTdno, 2000)

  store.note(undefined, '2001')      // 只推进一个维度
  assert.equal(store.lastEntno, 1000)
  assert.equal(store.lastTdno, 2001)

  const restored = createCursorStore(storage)
  assert.equal(restored.lastEntno, 1000, '应从存储恢复')
  assert.equal(restored.lastTdno, 2001)
})

test('游标：存储不可用时退化为内存（隐私模式/Node）', () => {
  const store = createCursorStore(null)
  store.note('5', '6')
  assert.equal(store.lastEntno, 5)
  assert.equal(store.lastTdno, 6)
})

test('补查计划：先批量刷在途、再补增量，且成交单独一路', () => {
  const plan = buildRecoveryPlan({ lastEntno: 1000, lastTdno: 2000 }, ['1000', '1005'])
  assert.deepEqual(plan, [
    { what: 'orders', entnos: ['1000', '1005'] },
    { what: 'orders', cursor: 1000 },
    { what: 'trades', cursor: 2000 }
  ])
})

test('补查计划：没有游标（首次连接）时不发无意义的增量查询', () => {
  const plan = buildRecoveryPlan({ lastEntno: 0, lastTdno: 0 }, [])
  assert.deepEqual(plan, [])
})

test('补查计划：在途委托上限 500（与引擎 by_entnos 上限一致）', () => {
  const many = Array.from({ length: 600 }, (_, i) => String(1000 + i))
  const plan = buildRecoveryPlan({ lastEntno: 0, lastTdno: 0 }, many)
  assert.equal(plan[0].entnos.length, 500)
})

test('看门狗 = 2× 心跳，且有下限', () => {
  assert.equal(watchdogMsFor(10000), 20000, 'quote：10s -> 20s')
  assert.equal(watchdogMsFor(5000), 10000, 'trade：5s -> 10s')
  assert.equal(watchdogMsFor(0), 20000, '非法值退回默认 10s 心跳')
  assert.equal(watchdogMsFor(500), 3000, '极小心跳仍保底 3s，避免抖动')
})

test('帧滞后：无 ts 视为无法判定（null），而不是 0', () => {
  assert.equal(frameLagMs(1000, 1500), 500)
  assert.equal(frameLagMs(2000, 1500), 0, '未来时间戳钳到 0')
  assert.equal(frameLagMs(undefined, 1500), null)
  assert.equal(frameLagMs(0, 1500), null)
  assert.equal(frameLagMs('abc', 1500), null)
})
