/**
 * SMART-M 蓝牙监控小程序 — 首页逻辑
 *
 * 基于全局 BLE 单例（app.globalData）：
 *   - 页面跳转不断开连接
 *   - 连接成功自动保存历史设备，下次打开自动重连
 *   - 通过 app.onLine() 注册数据回调
 */
const app = getApp()

const SERVICE_UUID = app.globalData.SERVICE_UUID

// 固件诊断描述（英文）→ 中文
const DMSG_CN = {
  'Overheat! Check cooling': '过热！请检查散热',
  'Temp rising, monitor': '温度上升，请关注',
  'Overload! Check load': '过载！请检查负载',
  'Current high, check': '电流偏高，请检查',
  'Bearing fault! Kurtosis high': '轴承故障！峭度值过高',
  'Bearing wear early sign': '轴承磨损早期迹象',
  'Imbalance/misalignment!': '转子不平衡/不对中！',
  'Vibration rising, check balance': '振动上升，请检查动平衡',
  'Multiple faults! Immediate check': '多重故障！请立即检查'
}

Page({
  data: {
    adapterReady: false,
    discovering: false,
    connected: false,
    deviceName: '',
    deviceId: '',
    rssi: 0,
    devices: [],
    showAllDevices: true,
    autoConnecting: false,   // 正在自动重连历史设备
    // 遥测数据
    t: '--', h: '--', v: '--', i: '--', u: '--', p: '--',
    relay: 0, alarm: 0,
    // 告警详情
    alarmLevelText: '', alarmSrcs: [],
    alarmChecks: [
      { name: '温度超限', on: false },
      { name: '振动超限', on: false },
      { name: '电流超限', on: false }
    ],
    // 诊断详情
    diagStatusText: '', diagFaults: [], conf: 0, dmsg: '',
    diagChecks: [
      { name: '过热', on: false },
      { name: '过载', on: false },
      { name: '振动异常', on: false },
      { name: '不平衡/不对中', on: false },
      { name: '轴承故障', on: false }
    ],
    lastUpdate: '--',
    // 阈值设置
    thInputs: { temp: '60', vib: '3.0', curr: '5.0' },
    // LED1/LED2 本地开关状态
    led1On: false, led2On: false,
    logs: []
  },

  onLoad() {
    this._lineListener = null
    this._connListener = null
    this._deviceFoundHandler = null
    this._rxBuffer = ''

    // 检查全局是否已有连接（从配置页返回时）
    if (app.globalData.connected) {
      this.setData({
        adapterReady: true,
        connected: true,
        deviceId: app.globalData.deviceId,
        deviceName: app.globalData.deviceName,
        rssi: app.globalData.rssi
      })
      this._registerListeners()
      this.log('复用已有蓝牙连接: ' + app.globalData.deviceName)
      // 主动查询一次数据
      app.sendJson({ cmd: 'QUERY' })
      app.sendJson({ cmd: 'GETTH' })
    } else {
      // 没有连接：打开适配器 + 尝试历史设备自动重连
      this._initAdapterAndAutoConnect()
    }
  },

  onShow() {
    // 每次显示页面时，同步全局连接状态（可能在配置页断开了）
    const g = app.globalData
    if (!g.connected && this.data.connected) {
      this.setData({
        connected: false, deviceId: '', deviceName: '',
        lastUpdate: '--', alarmLevelText: '', alarmSrcs: [],
        diagStatusText: '', diagFaults: [], conf: 0, dmsg: ''
      })
    } else if (g.connected && !this.data.connected) {
      this.setData({
        connected: true,
        deviceId: g.deviceId,
        deviceName: g.deviceName,
        rssi: g.rssi
      })
    }
  },

  onUnload() {
    this._unregisterListeners()
    if (this.data.discovering) {
      app.stopDiscovery()
    }
  },

  onHide() {
    if (this.data.discovering) {
      app.stopDiscovery()
    }
  },

  /* ---------------- 监听注册 ---------------- */

  _registerListeners() {
    // 行数据监听
    this._lineListener = (line) => this.onLine(line)
    app.onLine(this._lineListener)

    // 连接状态监听
    this._connListener = (connected, deviceName) => {
      this.log(connected ? '设备已连接' : '设备已断开')
      if (connected) {
        this.setData({
          connected: true,
          deviceId: app.globalData.deviceId,
          deviceName: app.globalData.deviceName,
          rssi: app.globalData.rssi
        })
      } else {
        this.setData({
          connected: false, deviceId: '', deviceName: '',
          lastUpdate: '--', alarmLevelText: '', alarmSrcs: [],
          diagStatusText: '', diagFaults: [], conf: 0, dmsg: ''
        })
        this.showToast('蓝牙连接已断开')
      }
    }
    app.onConnChange(this._connListener)
  },

  _unregisterListeners() {
    if (this._lineListener) {
      app.offLine(this._lineListener)
      this._lineListener = null
    }
    if (this._connListener) {
      app.offConnChange(this._connListener)
      this._connListener = null
    }
  },

  /* ---------------- 工具 ---------------- */

  log(msg) {
    const logs = this.data.logs
    logs.push(`[${new Date().toLocaleTimeString()}] ${msg}`)
    if (logs.length > 60) logs.shift()
    this.setData({ logs })
  },

  showToast(title, icon = 'none') {
    wx.showToast({ title, icon, duration: 2000 })
  },

  /* ---------------- 初始化 + 历史设备自动重连 ---------------- */

  async _initAdapterAndAutoConnect() {
    try {
      await app.openAdapter()
      this.setData({ adapterReady: true })
      this.log('蓝牙适配器已打开')

      // 尝试历史设备自动重连
      const hist = app.loadHistoryDevice()
      if (hist && hist.deviceId) {
        this.log(`发现历史设备: ${hist.name}，尝试自动重连...`)
        this.setData({ autoConnecting: true })
        try {
          await app.connect(hist.deviceId, { timeout: 8000, retry: 1 })
          // 连接成功，更新设备名
          app.globalData.deviceName = hist.name || '未知设备'
          this.setData({
            connected: true,
            deviceId: hist.deviceId,
            deviceName: hist.name || '未知设备',
            autoConnecting: false
          })
          this._registerListeners()
          this.log('自动重连成功: ' + hist.name)
          app.sendJson({ cmd: 'QUERY' })
          app.sendJson({ cmd: 'PING' })
          app.sendJson({ cmd: 'GETTH' })
          return
        } catch (err) {
          this.log('自动重连失败: ' + (err.errMsg || err.message || err))
          this.setData({ autoConnecting: false })
          // 失败后清除历史，重新扫描
          app.clearHistoryDevice()
        }
      }

      // 没有历史设备或重连失败，开始扫描
      this.onStartScan()
    } catch (err) {
      const ec = err && err.errCode
      let tip = '打开蓝牙失败'
      if (ec === 10001) tip = '蓝牙未开启或无权限，请到系统设置打开并授权'
      else if (ec === 10002) tip = '未获得蓝牙权限，请在设置中允许'
      else if (ec === 10003) tip = 'Android 需开启系统定位服务才能扫描 BLE'
      this.showToast(tip)
      this.log('打开蓝牙失败: ' + JSON.stringify(err))
    }
  },

  /* ---------------- 扫描 ---------------- */

  onStartScan() {
    if (this.data.discovering) return
    this.setData({ devices: [], discovering: true })

    app.startDiscovery({
      onFound: (res) => {
        const found = (res.devices || []).map((d) => {
          const name = d.name || d.localName || ''
          const uuids = (d.advertisServiceUUIDs || []).map((u) => u.toUpperCase())
          return {
            deviceId: d.deviceId,
            name: name || '未知设备',
            rssi: d.RSSI,
            isBt24: name.toLowerCase().indexOf('bt24') >= 0 ||
                    uuids.some((u) => u.indexOf(SERVICE_UUID) >= 0)
          }
        }).filter((d) => {
          if (d.deviceId === this.data.deviceId) return true
          return this.data.showAllDevices || d.isBt24
        })

        if (found.length === 0) return
        const devices = this.data.devices
        const map = {}
        devices.forEach((d) => { map[d.deviceId] = d })
        found.forEach((d) => { map[d.deviceId] = d })
        const merged = Object.keys(map).map((k) => map[k])
          .sort((a, b) => (b.rssi || -200) - (a.rssi || -200))
        this.setData({ devices: merged })
      }
    }).then(() => {
      this.log('正在扫描附近蓝牙设备...')
      setTimeout(() => this.stopScan(), 10000)
    }).catch((err) => {
      this.log('启动扫描失败: ' + JSON.stringify(err))
      this.setData({ discovering: false })
      this.showToast('启动扫描失败，请重试')
    })
  },

  onToggleAll(e) {
    this.setData({ showAllDevices: e.detail.value })
    this.log(e.detail.value ? '显示全部设备' : '只显示 BT24 设备')
    if (this.data.adapterReady && !this.data.connected) {
      this.onStartScan()
    }
  },

  stopScan() {
    app.stopDiscovery().then(() => {
      this.setData({ discovering: false })
      this.log('扫描停止')
    })
  },

  /* ---------------- 连接 ---------------- */

  async onConnect(e) {
    const deviceId = e.currentTarget.dataset.id
    const device = this.data.devices.find((d) => d.deviceId === deviceId)
    if (!device) return
    wx.showLoading({ title: '连接中...' })
    try {
      await app.connect(deviceId, { timeout: 10000, retry: 2 })
      wx.hideLoading()
      app.globalData.deviceName = device.name || '未知设备'
      app.globalData.rssi = device.rssi || 0
      this.setData({
        connected: true,
        deviceId,
        deviceName: device.name || '未知设备',
        rssi: device.rssi || 0
      })
      // 保存历史设备
      app.saveHistoryDevice(device)
      this._registerListeners()
      this.log('已连接: ' + device.name)
      // 连接成功立即请求数据
      app.sendJson({ cmd: 'QUERY' })
      app.sendJson({ cmd: 'PING' })
      app.sendJson({ cmd: 'GETTH' })
    } catch (err) {
      wx.hideLoading()
      const ec = err && err.errCode
      this.showToast('连接失败，请重试或重新上电设备')
      this.log('连接失败: ' + JSON.stringify(err))
    }
  },

  async onDisconnect() {
    await app.disconnect()
    this.log('已断开连接')
    // 断开后自动扫描
    setTimeout(() => {
      if (this.data.adapterReady && !this.data.connected) {
        this.onStartScan()
      }
    }, 300)
  },

  onResetAdapter() {
    this._unregisterListeners()
    app.disconnect().then(() => {
      return app.closeAdapter()
    }).then(() => {
      this.setData({ adapterReady: false, devices: [], connected: false })
      this.log('蓝牙适配器已关闭，正在重新打开...')
      this._initAdapterAndAutoConnect()
    })
  },

  /* ---------------- 数据处理 ---------------- */

  onLine(line) {
    try {
      const obj = JSON.parse(line)
      if (typeof obj.t === 'number') {
        // ===== 遥测帧 =====
        const fmt = (v, d) => (v === null || v === undefined) ? '--' : v.toFixed(d)

        const as = obj.as || 0
        const srcs = []
        if (as & 1) srcs.push('温度超限')
        if (as & 2) srcs.push('振动超限')
        if (as & 4) srcs.push('电流超限')
        const alText = obj.al === 2 ? '异常（超阈值）' : (obj.al === 1 ? '注意（超阈值的80%）' : '')

        const fm = obj.mask || 0
        const faults = []
        if (fm & 1) faults.push('过热')
        if (fm & 2) faults.push('过载')
        if (fm & 4) faults.push('振动异常')
        if (fm & 8) faults.push('不平衡/不对中')
        if (fm & 16) faults.push('轴承故障')
        const dText = obj.diag === 0 ? '' : (obj.diag === 1 ? '注意' : '异常')
        const conf = (obj.conf === undefined || obj.conf === null) ? 0 : obj.conf

        const lmRaw = obj.l
        const ledUpd = (lmRaw === undefined || lmRaw === null)
          ? {}
          : { led1On: !!(lmRaw & 1), led2On: !!(lmRaw & 2) }

        this.setData({
          t: fmt(obj.t, 1),
          h: fmt(obj.h, 1),
          v: fmt(obj.v, 2),
          i: fmt(obj.i, 3),
          u: fmt(obj.u, 3),
          p: fmt(obj.p, 3),
          relay: obj.relay,
          alarm: obj.alarm,
          alarmLevelText: alText,
          alarmSrcs: srcs,
          alarmChecks: [
            { name: '温度超限', on: !!(as & 1) },
            { name: '振动超限', on: !!(as & 2) },
            { name: '电流超限', on: !!(as & 4) }
          ],
          diagStatusText: dText,
          diagFaults: faults,
          diagChecks: [
            { name: '过热', on: !!(fm & 1) },
            { name: '过载', on: !!(fm & 2) },
            { name: '振动异常', on: !!(fm & 4) },
            { name: '不平衡/不对中', on: !!(fm & 8) },
            { name: '轴承故障', on: !!(fm & 16) }
          ],
          conf: conf,
          dmsg: DMSG_CN[obj.dmsg] || obj.dmsg || '',
          lastUpdate: new Date().toLocaleTimeString(),
          ...ledUpd
        })
        this.recordHistory(obj)
      } else if (obj.ok !== undefined) {
        // ===== 命令应答 =====
        this.log('应答: ' + line)
        if (obj.cmd === 'GETTH' || obj.cmd === 'SETTH') {
          if (obj.ok === 1 && typeof obj.temp === 'number') {
            this.setData({
              thInputs: {
                temp: String(obj.temp),
                vib: String(obj.vib),
                curr: String(obj.curr)
              }
            })
            this.log(`阈值已同步: 温度${obj.temp}℃ 振动${obj.vib}g 电流${obj.curr}A`)
          }
        }
      }
    } catch (err) {
      this.log('解析失败: ' + line)
    }
  },

  /* 记录历史（手机本地存储，最多 300 条） */
  recordHistory(obj) {
    const rec = {
      time: new Date().toLocaleTimeString(),
      ts: Date.now(),
      temp: obj.t, hum: obj.h, vib: obj.v,
      cur: obj.i, vol: obj.u, pwr: obj.p,
      relay: obj.relay, alarm: obj.alarm, al: obj.al, as: obj.as,
      diag: obj.diag, mask: obj.mask
    }
    let hist = wx.getStorageSync('bt_hist') || []
    hist.push(rec)
    if (hist.length > 300) hist = hist.slice(hist.length - 300)
    wx.setStorageSync('bt_hist', hist)
  },

  /* ---------------- 控制按钮 ---------------- */

  onQuery() {
    app.sendJson({ cmd: 'QUERY' })
  },

  onRelayToggle() {
    app.sendJson({ cmd: 'RELAY', val: this.data.relay ? 0 : 1 })
  },

  onLed1On() {
    app.sendJson({ cmd: 'LED', index: 1, state: 1 })
    this.setData({ led1On: true })
  },

  onLed1Off() {
    app.sendJson({ cmd: 'LED', index: 1, state: 0 })
    this.setData({ led1On: false })
  },

  onLed2On() {
    app.sendJson({ cmd: 'LED', index: 2, state: 1 })
    this.setData({ led2On: true })
  },

  onLed2Off() {
    app.sendJson({ cmd: 'LED', index: 2, state: 0 })
    this.setData({ led2On: false })
  },

  onBeep() {
    app.sendJson({ cmd: 'BEEP', ms: 200 })
  },

  /* ---------------- 阈值设置 ---------------- */

  onGetTh() {
    app.sendJson({ cmd: 'GETTH' })
  },

  onThInput(e) {
    const field = e.currentTarget.dataset.field
    const thInputs = this.data.thInputs
    thInputs[field] = e.detail.value
    this.setData({ thInputs })
  },

  onSaveTh() {
    const temp = parseFloat(this.data.thInputs.temp)
    const vib = parseFloat(this.data.thInputs.vib)
    const curr = parseFloat(this.data.thInputs.curr)
    if (isNaN(temp) || isNaN(vib) || isNaN(curr)) {
      this.showToast('请输入有效的阈值数值')
      return
    }
    app.sendJson({ cmd: 'SETTH', temp, vib, curr })
    this.showToast('已下发，等待确认', 'none')
  },

  /* ---------------- 页面跳转 ---------------- */

  onOpenHistory() {
    wx.navigateTo({ url: '/pages/history/history' })
  },

  onOpenConfig() {
    wx.navigateTo({ url: '/pages/config/config' })
  },

  onClearLog() {
    this.setData({ logs: [] })
  }
})
