/**
 * 设置页 · 标的范围（走 Flask，JWT 由 request 实例统一携带）。
 *
 * 信封说明：Flask 失败体为 {success:false, message}；引擎（C++ HttpGateway）失败体为
 * {success:false, error}，Flask 保存接口原样透传，故调用方错误提示需 error 优先、message 兜底。
 */
import request from '@/utils/request'

/**
 * 查询标的范围。
 * @returns {Promise<{success:boolean, data?:{instruments:Array, scope:Array, owned:Array}, message?:string|undefined}>}
 *   instruments: [{market, inst_id, inst_type}]
 *   scope:       [{market, inst_id}]                当前订阅范围
 *   owned:       [{market, inst_id, scope, strategies}]  有订阅 owner 的行（含仅策略持有的行）
 */
export async function getInstrumentScope() {
  const response = await request.get('/api/settings/instrument_scope', { timeout: 10000 })
  return response.data
}

/**
 * 保存标的范围（全量覆盖：未勾选的会退订，策略在用的保留）。
 * @param {Array<{market:string, inst_id:string}>} scope 目标订阅范围
 * @returns {Promise<{success:boolean, applied_cnt?:number, removed_cnt?:number, kept_cnt?:number, unknown_cnt?:number, error?:string}>}
 */
export async function setInstrumentScope(scope) {
  const response = await request.post('/api/settings/instrument_scope', { scope }, { timeout: 10000 })
  return response.data
}
