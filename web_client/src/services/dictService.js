/**
 * 数据字典服务
 * 从后端API加载dict.yml配置并提供字典映射功能
 */

import request from '@/utils/request'

class DictService {
  constructor() {
    this.dictData = null
    this.loading = false
    this.loadPromise = null
    // Dictionary name mapping for backward compatibility
    // Maps frontend names to dict.yml names
    this.nameMapping = {
      'EntrustStatus': 'OrderStatus',
      'BuySellSide': 'TradeSide'
    }
  }

  /**
   * 加载数据字典
   */
  async load() {
    // 如果正在加载，返回加载Promise
    if (this.loading && this.loadPromise) {
      return this.loadPromise
    }

    // 如果已经加载过，直接返回
    if (this.dictData) {
      return this.dictData
    }

    // 开始加载
    this.loading = true
    this.loadPromise = this._fetchDict()

    try {
      this.dictData = await this.loadPromise
      return this.dictData
    } finally {
      this.loading = false
      this.loadPromise = null
    }
  }

  /**
   * 从后端获取数据字典
   */
  async _fetchDict() {
    try {
      // 使用相对路径，通过Vite代理访问后端API
      console.log('加载数据字典: /api/dict')
      const response = await request.get('/api/dict', { timeout: 5000 })

      if (response.data.success) {
        console.log('成功获取数据字典')
        return response.data.data
      } else {
        throw new Error(response.data.message || '加载数据字典失败')
      }
    } catch (error) {
      console.error('加载数据字典失败:', error)
      // 降级使用本地字典数据
      console.warn('使用本地字典数据')
      return this._getLocalDict()
    }
  }

  /**
   * 获取本地字典数据（降级方案）
   */
  _getLocalDict() {
    return {}
  }

  /**
   * 获取字典项的显示名称
   * @param {string} dictType - 字典类型，如 'EntrustStatus', 'BuySellSide'
   * @param {string|number} value - 字典值
   * @returns {string} 显示名称，如果找不到则返回原值
   */
  getName(dictType, value) {
    // Apply name mapping if exists
    const mappedDictType = this.nameMapping[dictType] || dictType

    if (!this.dictData || !this.dictData[mappedDictType]) {
      return String(value)
    }

    const dict = this.dictData[mappedDictType]
    const valueStr = String(value)

    // 在fields数组中查找匹配的项
    if (dict.fields && Array.isArray(dict.fields)) {
      // 先精确匹配
      let field = dict.fields.find(f => f.val === valueStr)

      // 如果是 BuySellSide 或 TradeSide，尝试大小写不敏感匹配
      if (!field && (dictType === 'BuySellSide' || mappedDictType === 'TradeSide')) {
        const lowerValue = valueStr.toLowerCase()
        field = dict.fields.find(f => f.val.toLowerCase() === lowerValue)
      }

      if (field && field.name) {
        return field.name
      }
    }

    return valueStr
  }

  /**
   * 获取字典项的类型（用于Element Plus的tag类型）
   * @param {string} dictType - 字典类型
   * @param {string|number} value - 字典值
   * @returns {string} Element Plus tag类型: success/warning/info/danger
   */
  getType(dictType, value) {
    // Apply name mapping if exists
    const mappedDictType = this.nameMapping[dictType] || dictType
    const valueStr = String(value)

    // 针对委托状态的类型映射 (EntrustStatus 或 OrderStatus)
    if (dictType === 'EntrustStatus' || mappedDictType === 'OrderStatus') {
      const typeMap = {
        '0': 'info',      // 未报
        '1': 'warning',   // 正报
        '2': 'info',      // 已报
        '3': 'warning',   // 部成
        '4': 'success',   // 全部成交
        '5': 'warning',   // 已报待撤
        '6': 'info',      // 场内撤单
        '7': 'warning',   // 部成待撤
        '8': 'warning',   // 部成部撤
        '9': 'danger',    // 废单
        'I': 'info',      // 冻结
        'A': 'info'       // 预埋单
      }
      return typeMap[valueStr] || 'info'
    }

    // 针对买卖标记的类型映射 (BuySellSide 或 TradeSide)
    if (dictType === 'BuySellSide' || mappedDictType === 'TradeSide') {
      const upperValue = valueStr.toUpperCase()
      return (upperValue === 'B' || valueStr.toLowerCase() === 'b') ? 'success' : 'danger'
    }

    return 'info'
  }

  /**
   * 获取委托状态的自定义颜色（用于Element Plus tag的color属性，配合effect="dark"使用）
   * 颜色分组：
   *   黄/琥珀色系 - 中间态（未成交、处理中、待撤）
   *   绿色        - 全部成交
   *   蓝色系      - 已撤销
   *   红色        - 废单
   *   灰色        - 冻结、预埋单
   * @param {string|number} value - 委托状态值
   * @returns {string} CSS颜色值（十六进制）
   */
  getEntrustStatusColor(value) {
    const colorMap = {
      '0': '#9B7A14',  // 未报    - 深琥珀（尚未入市，最暗）
      '1': '#C89B0E',  // 正报    - 金黄（正在提交中）
      '2': '#DAA520',  // 已报    - 秋麒麟黄（已提交等待成交）
      '3': '#E8961C',  // 部成    - 琥珀橙（部分成交，仍活跃）
      '4': '#52C41A',  // 全部成交 - 绿色
      '5': '#B88018',  // 已报待撤 - 中琥珀（待撤单）
      '6': '#1890FF',  // 场内撤单 - 蓝色
      '7': '#CE9010',  // 部成待撤 - 深琥珀（部成后待撤）
      '8': '#40A9FF',  // 部成部撤 - 浅蓝色
      '9': '#FF4D4F',  // 废单    - 红色
      'I': '#6B7480',  // 冻结    - 灰色
      'A': '#8C939D',  // 预埋单  - 浅灰色
      // 字符串状态兼容（后端可能返回英文状态）
      'submitted':        '#DAA520',  // 同已报
      'partially_filled': '#E8961C',  // 同部成
      'filled':           '#52C41A',  // 同全部成交
      'canceled':         '#1890FF',  // 同场内撤单
      'rejected':         '#FF4D4F',  // 同废单
    }
    return colorMap[String(value)] || '#6B7480'
  }

  /**
   * 获取完整的字典配置
   * @param {string} dictType - 字典类型
   * @returns {object|null} 字典配置对象
   */
  getDict(dictType) {
    // Apply name mapping if exists
    const mappedDictType = this.nameMapping[dictType] || dictType

    if (!this.dictData) {
      return null
    }
    return this.dictData[mappedDictType] || null
  }

  /**
   * 获取字典的所有选项（用于下拉框等）
   * @param {string} dictType - 字典类型
   * @returns {Array} 选项数组 [{value, label}, ...]
   */
  getOptions(dictType) {
    // Apply name mapping if exists
    const mappedDictType = this.nameMapping[dictType] || dictType
    const dict = this.getDict(dictType)
    if (!dict || !dict.fields) {
      return []
    }

    return dict.fields.map(field => ({
      value: field.val,
      label: field.name
    }))
  }

  /**
   * 重新加载数据字典（用于字典更新后刷新）
   */
  async reload() {
    this.dictData = null
    return this.load()
  }
}

// 导出单例
export const dictService = new DictService()

// 导出类（用于需要创建新实例的情况）
export default DictService
