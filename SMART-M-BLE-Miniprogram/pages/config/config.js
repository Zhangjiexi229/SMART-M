/**
 * SMART-M 蓝牙监控小程序 — 设备配置页
 *
 * 基于全局 BLE 单例（app.globalData）：
 *   - 从首页跳转过来时，如果已连接直接复用，无需二次连接
 *   - 如果未连接，才走扫描+连接流程
 *   - 通过 app.onLine() 注册数据回调处理配置应答
 */
const app = getApp()

const SERVICE_UUID = app.globalData.SERVICE_UUID

Page({
  data: {
    adapterReady: false,
    discovering: false,
    connected: false,
    deviceId: '',
    deviceName: '',
    devices: [],
    showAllDevices: true,
    loadingCfg: false,
    saving: false,
    // 网络配置表单
    wifi_ssid: '',
    wifi_password: '',
    broker_host: '',
    broker_ip: '',
    broker_port: '1883',
    client_id: '',
    username: '',
    password: '',
    device_id: '',
    logs: [],
    showWifiPwd: false,
    showMqttPwd: false
  },
  toggleWifiPwd() {
    this.setData({ showWifiPwd: !this.data.showWifiPwd })
  },
  toggleMqttPwd() {
    this.setData({ showMqttPwd: !this.data.showMqttPwd })
  },
  onLoad() {
    this._lineListener = null
    this._connListener = null

    // 检查全局是否已有连接（从首页跳转过来时）
    if (app.globalData.connected) {
      this.setData({
        adapterReady: true,
        connected: true,
        deviceId: app.globalData.deviceId,
        deviceName: app.globalData.deviceName
      })
      this._registerListeners()
      this.log('复用已有蓝牙连接: ' + app.globalData.deviceName)
      // 直接读取配置，不需要二次连接
      this.onGetCfg()
    } else {
      // 未连接：打开适配器 + 扫描
      this._initAdapter()
    }
  },

  onShow() {
    // 同步全局连接状态
    const g = app.globalData
    if (g.connected && !this.data.connected) {
      this.setData({
        connected: true,
        deviceId: g.deviceId,
        deviceName: g.deviceName
      })
      this._registerListeners()
      this.onGetCfg()
    } else if (!g.connected && this.data.connected) {
      this.setData({
        connected: false, deviceId: '', deviceName: '',
        loadingCfg: false, saving: false
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
    this._lineListener = (line) => this.onLine(line)
    app.onLine(this._lineListener)

    this._connListener = (connected, deviceName) => {
      this.log(connected ? '设备已连接' : '设备已断开')
      if (connected) {
        this.setData({
          connected: true,
          deviceId: app.globalData.deviceId,
          deviceName: app.globalData.deviceName
        })
        this.onGetCfg()
      } else {
        this.setData({
          connected: false, deviceId: '', deviceName: '',
          loadingCfg: false, saving: false
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
    wx.showToast({ title, icon, duration: 2200 })
  },

  onClearLog() {
    this.setData({ logs: [] })
  },

  /* ---------------- 蓝牙初始化 ---------------- */

  async _initAdapter() {
    try {
      await app.openAdapter()
      this.setData({ adapterReady: true })
      this.log('蓝牙适配器已打开')

      // 尝试历史设备自动重连
      const hist = app.loadHistoryDevice()
      if (hist && hist.deviceId) {
        this.log(`发现历史设备: ${hist.name}，尝试自动重连...`)
        try {
          await app.connect(hist.deviceId, { timeout: 8000, retry: 1 })
          app.globalData.deviceName = hist.name || '未知设备'
          this.setData({
            connected: true,
            deviceId: hist.deviceId,
            deviceName: hist.name || '未知设备'
          })
          this._registerListeners()
          this.log('自动重连成功: ' + hist.name)
          this.onGetCfg()
          return
        } catch (err) {
          this.log('自动重连失败: ' + (err.errMsg || err.message || err))
          app.clearHistoryDevice()
        }
      }

      this.onStartScan()
    } catch (err) {
      const ec = err && err.errCode
      let tip = '打开蓝牙失败'
      if (ec === 10001) tip = '蓝牙未开启或无权限，请到系统设置打开并授权'
      else if (ec === 10002) tip = '未获得蓝牙权限，请在设置中允许'
      else if (ec === 10003) tip = 'Android 需开启系统定位服务才能扫描'
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
        }).filter((d) => this.data.showAllDevices || d.isBt24)

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
      this.setData({
        connected: true,
        deviceId,
        deviceName: device.name || '未知设备'
      })
      app.saveHistoryDevice(device)
      this._registerListeners()
      this.log('已连接: ' + device.name)
      this.onGetCfg()
    } catch (err) {
      wx.hideLoading()
      this.showToast('连接失败，请重试或重新上电设备')
      this.log('连接失败: ' + JSON.stringify(err))
    }
  },

  async onDisconnect() {
    await app.disconnect()
    this.log('已断开连接')
    setTimeout(() => {
      if (this.data.adapterReady && !this.data.connected) {
        this.onStartScan()
      }
    }, 300)
  },

  onResetAdapter() {
    this._unregisterListeners()
    app.disconnect().then(() => app.closeAdapter()).then(() => {
      this.setData({ adapterReady: false, devices: [], connected: false })
      this.log('蓝牙适配器已关闭，正在重新打开...')
      this._initAdapter()
    })
  },

  /* ---------------- 协议处理 ---------------- */

  onLine(line) {
    try {
      const obj = JSON.parse(line)
      if (obj.ok !== undefined) {
        this.log('应答: ' + line)
        if (obj.cmd === 'GETCFG' && obj.ok === 1) {
          this.applyCfg(obj)
        } else if (obj.cmd === 'SETCFG') {
          this.setData({ saving: false })
          if (obj.ok === 1 && obj.saved === 1) {
            this.showToast('已保存，设备自动重连', 'success')
            this.log('配置已保存到设备 Flash，MQTT 将自动重连')
          } else {
            this.showToast('保存失败，请检查输入')
          }
        }
      }
    } catch (err) {
      this.log('解析失败: ' + line)
    }
  },

  applyCfg(obj) {
    if (this._cfgTimeout) { clearTimeout(this._cfgTimeout); this._cfgTimeout = null }
    this.setData({
      wifi_ssid: obj.wifi_ssid || '',
      wifi_password: obj.wifi_password || '',
      broker_host: obj.broker_host || '',
      broker_ip: obj.broker_ip || '',
      broker_port: String(obj.broker_port == null ? '' : obj.broker_port),
      client_id: obj.client_id || '',
      username: obj.username || '',
      password: obj.password || '',
      device_id: obj.device_id || '',
      loadingCfg: false
    })
    this.log('已读取设备当前配置')
  },

  /* ---------------- 表单输入 ---------------- */

  onInput(e) {
    const field = e.currentTarget.dataset.field
    this.setData({ [field]: e.detail.value })
  },

  /* ---------------- 读取 / 保存 ---------------- */

  onGetCfg() {
    if (!app.globalData.connected) {
      this.showToast('未连接设备')
      return
    }
    this.setData({ loadingCfg: true })
    // 12秒超时保护：SETCFG写Flash约2s + WiFi重连阻塞，留足余量
    if (this._cfgTimeout) clearTimeout(this._cfgTimeout)
    this._cfgTimeout = setTimeout(() => {
      if (this.data.loadingCfg) {
        this.setData({ loadingCfg: false })
        this.showToast('读取超时，设备可能正在重连WiFi，请重试')
        this.log('GETCFG 超时：未收到设备应答（可能因WiFi重连阻塞）')
      }
    }, 12000)
    app.sendJson({ cmd: 'GETCFG' }).catch((err) => {
      this.setData({ loadingCfg: false })
      this.showToast('发送失败：' + (err.message || ''))
    })
  },

  onSaveCfg() {
    const d = this.data
    if (!app.globalData.connected) {
      this.showToast('未连接设备')
      return
    }
    // ---- 校验 ----
    const invalidChar = (v) => /["\\]/.test(v || '')
    const fields = [
      ['WiFi 名称', d.wifi_ssid],
      ['WiFi 密码', d.wifi_password],
      ['云平台地址', d.broker_host],
      ['云平台 IP', d.broker_ip],
      ['ClientId', d.client_id],
      ['用户名', d.username],
      ['云密码', d.password],
      ['设备ID', d.device_id]
    ]
    for (const [name, val] of fields) {
      if (invalidChar(val)) {
        this.showToast(`${name}不能包含 " 或 \\`)
        return
      }
    }
    if (!d.wifi_ssid.trim()) {
      this.showToast('WiFi 名称不能为空')
      return
    }
    if (!d.broker_host.trim() && !d.broker_ip.trim()) {
      this.showToast('云平台地址与IP至少填一个')
      return
    }
    const port = parseInt(d.broker_port, 10)
    if (isNaN(port) || port < 1 || port > 65535) {
      this.showToast('端口需为 1~65535')
      return
    }
    // ---- 确认 ----
    wx.showModal({
      title: '确认保存配置？',
      content: `WiFi: ${d.wifi_ssid}\n云平台: ${d.broker_host || d.broker_ip}:${port}\n保存后设备将自动按新配置重连。`,
      confirmText: '保存',
      cancelText: '取消',
      success: (res) => {
        if (!res.confirm) return
        this.setData({ saving: true })
        const cfgData = {
          cmd: 'SETCFG',
          wifi_ssid: d.wifi_ssid.trim(),
          wifi_password: d.wifi_password,
          broker_host: d.broker_host.trim(),
          broker_ip: d.broker_ip.trim(),
          broker_port: port,
          client_id: d.client_id.trim(),
          username: d.username.trim(),
          password: d.password,
          device_id: d.device_id.trim()
        }
        this.log('发送 SETCFG (' + JSON.stringify(cfgData).length + ' 字节): ' + JSON.stringify(cfgData))
        app.sendJson(cfgData).catch((err) => {
          this.setData({ saving: false })
          this.showToast('发送失败：' + (err.message || ''))
        })
        this.showToast('已下发，等待设备确认', 'none')
      }
    })
  },

  onBack() {
    wx.navigateBack()
  }
})
