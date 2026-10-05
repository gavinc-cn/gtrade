/**
 * 手动下单相关接口封装。
 *
 * 统一通道：全部走 request（相对路径经 Vite 代理到 web_server Flask :46011，带 token），
 * 由 Flask 按需转发到 C++ HttpGateway（:46012）：
 * - 下单/撤单/盘口由 Flask 转发到网关；place_order / cancel_order 仅在引擎以
 *   GTRADE_ENABLE_HTTP_TRADE 编译时存在，未开启时网关返回 404（Flask 原样透传）。
 * - 委托列表/资金列表由 Flask 直接查库。
 *
 * 注意：网关接口的响应信封是 {success, order_id|error}，与 Flask 查询接口的
 * {success, data, message} 不同，调用方需按各自信封处理；引擎不可达时 Flask
 * 统一回 502 {success, message}。
 * 所有请求均带 silent 标记，错误提示由页面自行展示（避免全局 toast 刷屏/语义不符）。
 */
import request from '@/utils/request'

/**
 * 下单。
 * @param {Object} payload 字段全部为字符串（避免浮点精度问题）：
 *   account_id, inst_id, td_mode(cash|cross|isolated), side(buy|sell),
 *   ord_type(limit|market|post_only|fok|ioc), sz, [px](市价可省略), [market](默认 okx)
 * @returns {Promise<{success:boolean, order_id?:string, error?:string}>}
 */
export async function placeOrder(payload) {
  const { data } = await request.post('/api/trade/place_order', payload, { silent: true })
  return data
}

/**
 * 撤单。orderId 为本地委托号（order 表 entno，列表接口返回字符串）。
 *
 * 注意：entno 是 19 位大整数（≈1.79e18，超过 JS 安全整数上限 2^53≈9.0e15），
 * 必须原样按字符串传递——用 Number() 转换会被 IEEE-754 double 静默舍入
 * （…000005 → …000000），引擎按错误的号查不到委托，回 order not found。
 * 字符串由 web_server 转发层转回精确整数后再发给引擎。
 * @returns {Promise<{success:boolean, error?:string}>}
 */
export async function cancelOrder(accountId, orderId) {
  const { data } = await request.post('/api/trade/cancel_order', {
    account_id: accountId,
    order_id: String(orderId)
  }, { silent: true })
  return data
}

/**
 * 最新盘口快照（经 Flask 转发，只读）。
 * @returns {Promise<{success:boolean, timestamp:number, asks:number[][], bids:number[][], error?:string}>}
 *   asks/bids 为 [price, size] 数组的数组
 */
export async function getDepth(instId, market = 'okx') {
  const { data } = await request.get('/api/trade/depth', {
    params: { inst_id: instId, market },
    silent: true
  })
  return data
}

/**
 * 委托列表（Flask，分页）。参数：page, page_size, policy_no, inst_id, status
 * @returns {Promise<{success:boolean, data?:{orders:Array, total:number}, error?:string}>}
 */
export async function getOrders(params) {
  const { data } = await request.get('/api/orders', { params, silent: true })
  return data
}

/**
 * 资金列表（Flask，分页）。参数：page, page_size, account_id, currency
 * @returns {Promise<{success:boolean, data?:{balances:Array, total:number}, error?:string}>}
 */
export async function getBalances(params) {
  const { data } = await request.get('/api/balances', { params, silent: true })
  return data
}

/**
 * 组合持仓列表（Flask，分页）。参数：page, page_size, account_id, portfolio, instrument
 * 用于推导组合（portfolio）下拉选项。
 * @returns {Promise<{success:boolean, data?:{positions:Array, total:number}, error?:string}>}
 */
export async function getPortfolioPositions(params) {
  const { data } = await request.get('/api/portfolio_positions', { params, silent: true })
  return data
}
