/**
 * SMART-M 蓝牙监控小程序 — 首页逻辑
 *
 * 与固件协议（一行一帧 JSON，\r\n 结尾）：
 *   上行遥测: {"t":25.3,"h":58.0,"v":1.02,"i":0.452,"u":12.100,"p":5.4,
 *              "relay":0,"alarm":0,"diag":0,"mask":0}
 *   下行命令: {"cmd":"QUERY"} / {"cmd":"RELAY","val":1} /
 *             {"cmd":"LED","mask":5} / {"cmd":"LED","index":1,"state":1} /
 *             {"cmd":"PING"} / {"cmd":"BEEP","ms":200}
 *   应答:     {"ok":1,"cmd":"RELAY","val":1} / {"ok":0,"err":"..."}
 */
const app = getApp()

const SERVICE_UUID = app.globalData.SERVICE_UUID
const NOTIFY_CHAR_UUID = app.globalData.NOTIFY_CHAR_UUID
const WRITE_CHAR_UUID = app.globalData.WRITE_CHAR_UUID

Page({
  data: {
    adapterReady: false,
    discovering: false,
    connected: false,
    deviceName: '',
    deviceId: '',
    rssi: 0,
    devices: [],
    showAllDevices: false,
    // 遥测数据
    t: '--', h: '--', v: '--', i: '--', u: '--', p: '--',
    relay: 0, alarm: 0, diag: 0, mask: 0,
    lastUpdate: '--',
    logs: []
  },

  onLoad() {
    this._notifyHandler = null
    this._connStateHandler = null
    this._deviceFoundHandler = null
    this._rxBuffer = ''
    this._writeQueue = Promise.resolve()
  },

  onUnload() {
    this.cleanup()
  },

  onHide() {
    // 保留后台连接（小程序切入后台 BLE 连接仍有效），仅停止扫描
    if (this.data.discovering) {
      wx.stopBluetoothDevicesDiscovery({ complete: () => {} })
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

  /* ---------------- 蓝牙初始化与扫描 ---------------- */

  onOpenAdapter() {
    wx.openBluetoothAdapter({
      success: () => {
        this.setData({ adapterReady: true })
        this.log('蓝牙适配器已打开')
        this.onStartScan()
      },
      fail: (err) => {
        this.handleAdapterError(err, '打开蓝牙失败')
      }
    })
  },

  handleAdapterError(err, prefix) {
    const ec = err && err.errCode
    let tip = `${prefix}`
    if (ec === 10001) tip = '当前设备不支持蓝牙 BLE'
    else if (ec === 10002) tip = '未获得蓝牙权限，请在设置中允许'
    else if (ec === 10003) tip = 'Android 需开启系统定位服务才能扫描 BLE'
    else if (ec === 10004) tip = '蓝牙适配器已打开'
    this.showToast(tip)
    this.log(`${prefix}: ${JSON.stringify(err)}`)
    if (ec === 10000) {
      // 未初始化：重新打开适配器
      wx.openBluetoothAdapter({
        success: () => this.setData({ adapterReady: true }),
        fail: (e2) => this.log('再次打开适配器失败: ' + JSON.stringify(e2))
      })
    }
  },

  onStartScan() {
    if (this.data.discovering) return
    this.setData({ devices: [], discovering: true })

    // 订阅设备发现
    this._deviceFoundHandler = (res) => {
      const found = (res.devices || []).map((d) => ({
        deviceId: d.deviceId,
        name: d.name || d.localName || '未知设备',
        rssi: d.RSSI,
        advertisServiceUUIDs: d.advertisServiceUUIDs || []
      })).filter((d) => {
        // 已连接设备直接保留；其余按服务 UUID 过滤（适配器已按 FFE0 过滤，这里再兜底一次）
        if (d.deviceId === this.data.deviceId) return true
        const uuids = (d.advertisServiceUUIDs || []).map((u) => u.toUpperCase())
        return uuids.some((u) => u.indexOf(SERVICE_UUID) >= 0)
      })

      if (found.length === 0) return
      const devices = this.data.devices
      const map = {}
      devices.forEach((d) => { map[d.deviceId] = d })
      found.forEach((d) => { map[d.deviceId] = d })
      const merged = Object.keys(map).map((k) => map[k])
      this.setData({ devices: merged })
      this.log(`发现 ${found.length} 个设备（共 ${merged.length}）`)
    }
    wx.onBluetoothDeviceFound(this._deviceFoundHandler)

    // 用 FFE0 服务过滤扫描，只返回 BT24 一类透传模块
    wx.startBluetoothDevicesDiscovery({
      services: [SERVICE_UUID],
      allowDuplicatesKey: false,
      success: () => {
        this.log('正在扫描 BT24 设备...')
        // 8 秒后停止扫描
        setTimeout(() => { this.stopScan() }, 8000)
      },
      fail: (err) => {
        this.log('启动扫描失败: ' + JSON.stringify(err))
        this.setData({ discovering: false })
        this.showToast('启动扫描失败，请重试')
      }
    })
  },

  stopScan() {
    wx.stopBluetoothDevicesDiscovery({
      complete: () => {
        this.setData({ discovering: false })
        this.log('扫描停止')
      }
    })
  },

  /* ---------------- 连接 ---------------- */

  onConnect(e) {
    const deviceId = e.currentTarget.dataset.id
    const device = this.data.devices.find((d) => d.deviceId === deviceId)
    if (!device) return
    wx.showLoading({ title: '连接中...' })
    this.connectDevice(device)
  },

  connectDevice(device) {
    const deviceId = device.deviceId
    this.setData({ deviceId, deviceName: device.name || '未知设备', rssi: device.rssi || 0 })

    wx.createBLEConnection({
      deviceId,
      timeout: 10000,
      success: () => {
        wx.hideLoading()
        this.log('已连接: ' + device.name)
        this.setData({ connected: true })
        this.discoverServices(deviceId)
        this.watchConnectionState(deviceId)
      },
      fail: (err) => {
        wx.hideLoading()
        this.showToast('连接失败: ' + (err.errMsg || ''))
        this.log('连接失败: ' + JSON.stringify(err))
        this.setData({ connected: false })
      }
    })
  },

  onDisconnect() {
    if (!this.data.deviceId) return
    wx.closeBLEConnection({
      deviceId: this.data.deviceId,
      success: () => this.log('已断开连接'),
      fail: (err) => this.log('断开失败: ' + JSON.stringify(err))
    })
    this.resetConnectedState()
  },

  watchConnectionState(deviceId) {
    if (this._connStateHandler) {
      wx.offBLEConnectionStateChange(this._connStateHandler)
    }
    this._connStateHandler = (res) => {
      if (res.deviceId !== deviceId) return
      this.log(res.connected ? '设备重新连接' : '设备已断开')
      if (!res.connected) {
        this.resetConnectedState()
        this.showToast('蓝牙连接已断开')
      }
    }
    wx.onBLEConnectionStateChange(this._connStateHandler)
  },

  resetConnectedState() {
    this.setData({
      connected: false,
      deviceId: '',
      deviceName: '',
      lastUpdate: '--'
    })
  },

  discoverServices(deviceId) {
    wx.getBLEDeviceServices({
      deviceId,
      success: (res) => {
        const services = res.services || []
        const svc = services.find((s) => (s.uuid || '').toUpperCase().indexOf(SERVICE_UUID) >= 0)
        if (!svc) {
          this.showToast('未找到 FFE0 服务')
          this.log('services: ' + JSON.stringify(services.map((s) => s.uuid)))
          return
        }
        this.log('找到服务 ' + svc.uuid.toUpperCase())
        this.getCharacteristics(deviceId, svc.uuid)
      },
      fail: (err) => {
        this.showToast('获取服务失败')
        this.log('获取服务失败: ' + JSON.stringify(err))
      }
    })
  },

  getCharacteristics(deviceId, serviceId) {
    this._serviceId = serviceId
    wx.getBLEDeviceCharacteristics({
      deviceId,
      serviceId,
      success: (res) => {
        const chars = res.characteristics || []
        const notifyChar = chars.find((c) => {
          const u = (c.uuid || '').toUpperCase()
          const props = c.properties || {}
          return u.indexOf(NOTIFY_CHAR_UUID) >= 0 && props.notify
        })
        const writeChar = chars.find((c) => {
          const u = (c.uuid || '').toUpperCase()
          const props = c.properties || {}
          return (u.indexOf(NOTIFY_CHAR_UUID) >= 0 || u.indexOf(WRITE_CHAR_UUID) >= 0) && props.write
        })
        if (!notifyChar) {
          this.showToast('未找到通知特征 FFE1')
          this.log('chars: ' + JSON.stringify(chars.map((c) => ({ u: c.uuid, p: c.properties }))))
          return
        }
        this._notifyCharId = notifyChar.uuid
        this._writeCharId = writeChar ? writeChar.uuid : notifyChar.uuid
        this.log(`通知特征=${this._notifyCharId} 写特征=${this._writeCharId}`)
        this.enableNotify(deviceId, serviceId, notifyChar.uuid)
      },
      fail: (err) => {
        this.showToast('获取特征失败')
        this.log('获取特征失败: ' + JSON.stringify(err))
      }
    })
  },

  enableNotify(deviceId, serviceId, charId) {
    wx.notifyBLECharacteristicValueChange({
      deviceId,
      serviceId,
      characteristicId: charId,
      state: true,
      success: () => {
        this.log('已订阅通知')
        this.watchNotify()
        // 连接成功立即请求一帧实时数据
        this.sendJson({ cmd: 'QUERY' })
        this.sendJson({ cmd: 'PING' })
      },
      fail: (err) => {
        this.showToast('订阅通知失败')
        this.log('订阅通知失败: ' + JSON.stringify(err))
      }
    })
  },

  watchNotify() {
    if (this._notifyHandler) {
      wx.offBLECharacteristicValueChange(this._notifyHandler)
    }
    this._notifyHandler = (res) => {
      // 逐字节解码（上行遥测为 ASCII JSON）
      let str = ''
      const buf = new Uint8Array(res.value)
      for (let i = 0; i < buf.length; i++) {
        str += String.fromCharCode(buf[i])
      }
      this._rxBuffer += str
      // 按行拆帧
      let nl
      while ((nl = this._rxBuffer.indexOf('\n')) >= 0) {
        const line = this._rxBuffer.slice(0, nl).replace(/\r/g, '').trim()
        this._rxBuffer = this._rxBuffer.slice(nl + 1)
        if (line) this.onLine(line)
      }
    }
    wx.onBLECharacteristicValueChange(this._notifyHandler)
  },

  onLine(line) {
    if (line[0] !== '{') return
    try {
      const obj = JSON.parse(line)
      if (typeof obj.t === 'number') {
        // 遥测帧
        this.setData({
          t: obj.t.toFixed(1),
          h: obj.h.toFixed(1),
          v: obj.v.toFixed(2),
          i: obj.i.toFixed(3),
          u: obj.u.toFixed(3),
          p: obj.p.toFixed(3),
          relay: obj.relay,
          alarm: obj.alarm,
          diag: obj.diag,
          mask: obj.mask,
          lastUpdate: new Date().toLocaleTimeString()
        })
      } else if (obj.ok !== undefined) {
        this.log('应答: ' + line)
      }
    } catch (err) {
      this.log('解析失败: ' + line)
    }
  },

  /* ---------------- 发送命令 ---------------- */

  sendJson(obj) {
    const payload = JSON.stringify(obj) + '\r\n'
    const deviceId = this.data.deviceId
    if (!deviceId || !this._writeCharId) {
      this.showToast('未连接或特征未就绪')
      return
    }
    // 分包写入（单次 ≤20 字节），串行执行
    const chunks = []
    for (let i = 0; i < payload.length; i += 20) {
      chunks.push(payload.slice(i, i + 20))
    }
    this._writeQueue = this._writeQueue.then(() => {
      return chunks.reduce((p, chunk) => {
        return p.then(() => this.writeChunk(deviceId, chunk))
          .then(() => new Promise((r) => setTimeout(r, 40)))
      }, Promise.resolve())
    })
    this.log('发送: ' + JSON.stringify(obj))
  },

  writeChunk(deviceId, chunk) {
    return new Promise((resolve) => {
      const buf = new ArrayBuffer(chunk.length)
      const view = new Uint8Array(buf)
      for (let i = 0; i < chunk.length; i++) {
        view[i] = chunk.charCodeAt(i)
      }
      wx.writeBLECharacteristicValue({
        deviceId,
        serviceId: this._serviceId,
        characteristicId: this._writeCharId,
        value: buf,
        success: () => resolve(),
        fail: (err) => {
          this.log('写入失败: ' + JSON.stringify(err))
          resolve()
        }
      })
    })
  },

  /* ---------------- 控制按钮 ---------------- */

  onQuery() {
    this.sendJson({ cmd: 'QUERY' })
  },

  onRelayToggle() {
    this.sendJson({ cmd: 'RELAY', val: this.data.relay ? 0 : 1 })
  },

  onLedToggle(e) {
    const index = Number(e.currentTarget.dataset.index)
    // 简单起见：点一下翻转该灯（发送单灯命令，state 与当前遥测无直接对应，
    // 固件按 index+state 执行；这里按 1 处理，便于演示）
    this.sendJson({ cmd: 'LED', index, state: 1 })
  },

  onLedMask(e) {
    const mask = Number(e.currentTarget.dataset.mask)
    this.sendJson({ cmd: 'LED', mask })
  },

  onBeep() {
    this.sendJson({ cmd: 'BEEP', ms: 200 })
  },

  onClearLog() {
    this.setData({ logs: [] })
  },

  /* ---------------- 清理 ---------------- */

  cleanup() {
    if (this._notifyHandler) {
      wx.offBLECharacteristicValueChange(this._notifyHandler)
      this._notifyHandler = null
    }
    if (this._connStateHandler) {
      wx.offBLEConnectionStateChange(this._connStateHandler)
      this._connStateHandler = null
    }
    if (this._deviceFoundHandler) {
      wx.offBluetoothDeviceFound(this._deviceFoundHandler)
      this._deviceFoundHandler = null
    }
    wx.stopBluetoothDevicesDiscovery({ complete: () => {} })
    if (this.data.deviceId) {
      wx.closeBLEConnection({ deviceId: this.data.deviceId, complete: () => {} })
    }
    wx.closeBluetoothAdapter({ complete: () => {} })
  }
})
