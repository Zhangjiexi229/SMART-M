/**
 * SMART-M 蓝牙监控小程序 — 全局逻辑
 *
 * 全局 BLE 单例管理器：
 *   - 整个小程序只维护一条 BLE 连接，页面跳转不断开
 *   - 通知事件全局只注册一次，分发给各页面 listener
 *   - 连接成功后自动保存历史设备，下次打开自动重连
 *   - 提供 connect / disconnect / sendJson 全局接口
 */
App({
  globalData: {
    // 与固件约定：蓝牙模块名关键字
    nameKeywords: ['BT24', 'SMART'],
    // 服务/特征 UUID（DX-BT24 出厂默认）
    SERVICE_UUID: 'FFE0',
    NOTIFY_CHAR_UUID: 'FFE1',
    WRITE_CHAR_UUID: 'FFE2',

    // ===== 全局 BLE 连接状态 =====
    adapterReady: false,
    connected: false,
    deviceId: '',
    deviceName: '',
    rssi: 0,
    _serviceId: '',
    _notifyCharId: '',
    _writeCharId: '',
    _rxBuffer: '',
    _writeQueue: Promise.resolve(),

    // 事件监听器列表（每个页面注册自己的 onLine 回调）
    _lineListeners: [],
    // 连接状态变化监听器
    _connListeners: [],

    // 全局 handler 引用（只注册一次）
    _notifyHandler: null,
    _connStateHandler: null,

    // 历史设备记录
    HISTORY_KEY: 'bt_history_device'
  },

  onLaunch() {
    // 小程序启动时不自动打开蓝牙，由首页 onLoad 触发
  },

  /* ==================== 事件订阅 ==================== */

  // 注册行数据监听器（页面跳转时新页面调用，旧页面 onUnload 注销）
  onLine(listener) {
    if (typeof listener === 'function' && this.globalData._lineListeners.indexOf(listener) < 0) {
      this.globalData._lineListeners.push(listener)
    }
  },

  offLine(listener) {
    const arr = this.globalData._lineListeners
    const idx = arr.indexOf(listener)
    if (idx >= 0) arr.splice(idx, 1)
  },

  // 连接状态变化监听器
  onConnChange(listener) {
    if (typeof listener === 'function' && this.globalData._connListeners.indexOf(listener) < 0) {
      this.globalData._connListeners.push(listener)
    }
  },

  offConnChange(listener) {
    const arr = this.globalData._connListeners
    const idx = arr.indexOf(listener)
    if (idx >= 0) arr.splice(idx, 1)
  },

  _notifyConnChange(connected, deviceName) {
    this.globalData._connListeners.forEach((fn) => {
      try { fn(connected, deviceName) } catch (e) { console.error(e) }
    })
  },

  /* ==================== 历史设备 ==================== */

  saveHistoryDevice(device) {
    try {
      wx.setStorageSync(this.globalData.HISTORY_KEY, {
        deviceId: device.deviceId,
        name: device.name || '',
        saveTime: Date.now()
      })
    } catch (e) { /* ignore */ }
  },

  loadHistoryDevice() {
    try {
      return wx.getStorageSync(this.globalData.HISTORY_KEY) || null
    } catch (e) {
      return null
    }
  },

  clearHistoryDevice() {
    try { wx.removeStorageSync(this.globalData.HISTORY_KEY) } catch (e) { /* ignore */ }
  },

  /* ==================== 蓝牙适配器 ==================== */

  openAdapter() {
    return new Promise((resolve, reject) => {
      wx.openBluetoothAdapter({
        success: () => {
          this.globalData.adapterReady = true
          // 全局只注册一次连接状态监听
          this._watchGlobalConnState()
          resolve()
        },
        fail: (err) => reject(err)
      })
    })
  },

  closeAdapter() {
    return new Promise((resolve) => {
      wx.closeBluetoothAdapter({ complete: () => {
        this.globalData.adapterReady = false
        resolve()
      }})
    })
  },

  /* ==================== 扫描 ==================== */

  startDiscovery({ allowDuplicatesKey = false, onFound } = {}) {
    return new Promise((resolve, reject) => {
      // 先注销旧的设备发现回调
      if (this._deviceFoundHandler) {
        wx.offBluetoothDeviceFound(this._deviceFoundHandler)
      }
      this._deviceFoundHandler = (res) => {
        if (typeof onFound === 'function') onFound(res)
      }
      wx.onBluetoothDeviceFound(this._deviceFoundHandler)

      wx.startBluetoothDevicesDiscovery({
        allowDuplicatesKey,
        powerLevel: 'high',
        success: resolve,
        fail: reject
      })
    })
  },

  stopDiscovery() {
    return new Promise((resolve) => {
      wx.stopBluetoothDevicesDiscovery({ complete: resolve })
    })
  },

  /* ==================== 连接 ==================== */

  connect(deviceId, { timeout = 10000, retry = 2 } = {}) {
    const g = this.globalData
    return new Promise((resolve, reject) => {
      const tryConnect = (attempt) => {
        wx.createBLEConnection({
          deviceId,
          timeout,
          success: () => {
            g.connected = true
            g.deviceId = deviceId
            this._notifyConnChange(true, g.deviceName)
            // 连接成功后自动发现服务和特征
            this.discoverServices(deviceId)
              .then(() => this._enableNotify(deviceId))
              .then(() => resolve(deviceId))
              .catch((err) => reject(err))
          },
          fail: (err) => {
            const ec = err && err.errCode
            if (attempt < retry) {
              // 先关闭残留连接，再重试
              wx.closeBLEConnection({
                deviceId,
                complete: () => {
                  setTimeout(() => {
                    if (g.connected) { resolve(deviceId); return }
                    tryConnect(attempt + 1)
                  }, 800)
                }
              })
            } else {
              g.connected = false
              this._notifyConnChange(false, '')
              reject(err)
            }
          }
        })
      }
      tryConnect(0)
    })
  },

  disconnect() {
    return new Promise((resolve) => {
      const g = this.globalData
      const did = g.deviceId
      if (did) {
        wx.closeBLEConnection({
          deviceId: did,
          complete: () => {
            this._resetState()
            this._notifyConnChange(false, '')
            resolve()
          }
        })
      } else {
        this._resetState()
        this._notifyConnChange(false, '')
        resolve()
      }
    })
  },

  // 检查当前连接是否仍然有效（不主动扫描，只查系统连接状态）
  checkConnection() {
    return new Promise((resolve) => {
      const g = this.globalData
      if (!g.deviceId || !g.connected) { resolve(false); return }
      wx.getBLEDeviceServices({
        deviceId: g.deviceId,
        success: () => resolve(true),
        fail: () => resolve(false)
      })
    })
  },

  /* ==================== 内部：服务/特征/订阅 ==================== */

  discoverServices(deviceId) {
    const g = this.globalData
    return new Promise((resolve, reject) => {
      wx.getBLEDeviceServices({
        deviceId,
        success: (res) => {
          const services = res.services || []
          const svc = services.find((s) =>
            (s.uuid || '').toUpperCase().indexOf(g.SERVICE_UUID) >= 0)
          if (!svc) { reject(new Error('未找到 FFE0 服务')); return }
          g._serviceId = svc.uuid
          this._getCharacteristics(deviceId, svc.uuid)
            .then(resolve)
            .catch(reject)
        },
        fail: (err) => reject(err)
      })
    })
  },

  _getCharacteristics(deviceId, serviceId) {
    const g = this.globalData
    return new Promise((resolve, reject) => {
      wx.getBLEDeviceCharacteristics({
        deviceId,
        serviceId,
        success: (res) => {
          const chars = res.characteristics || []
          const notifyChar = chars.find((c) => {
            const u = (c.uuid || '').toUpperCase()
            return u.indexOf(g.NOTIFY_CHAR_UUID) >= 0 && c.properties && c.properties.notify
          })
          const writeChar = chars.find((c) => {
            const u = (c.uuid || '').toUpperCase()
            const props = c.properties || {}
            return (u.indexOf(g.NOTIFY_CHAR_UUID) >= 0 || u.indexOf(g.WRITE_CHAR_UUID) >= 0) && props.write
          })
          if (!notifyChar) { reject(new Error('未找到通知特征 FFE1')); return }
          g._notifyCharId = notifyChar.uuid
          g._writeCharId = writeChar ? writeChar.uuid : notifyChar.uuid
          resolve()
        },
        fail: (err) => reject(err)
      })
    })
  },

  _enableNotify(deviceId) {
    const g = this.globalData
    return new Promise((resolve, reject) => {
      wx.notifyBLECharacteristicValueChange({
        deviceId,
        serviceId: g._serviceId,
        characteristicId: g._notifyCharId,
        state: true,
        success: () => {
          // 全局只注册一次通知回调
          this._watchGlobalNotify()
          resolve()
        },
        fail: (err) => reject(err)
      })
    })
  },

  _watchGlobalNotify() {
    const g = this.globalData
    if (g._notifyHandler) return  // 已注册过
    g._notifyHandler = (res) => {
      let str = ''
      const buf = new Uint8Array(res.value)
      for (let i = 0; i < buf.length; i++) str += String.fromCharCode(buf[i])
      g._rxBuffer += str
      let nl
      while ((nl = g._rxBuffer.indexOf('\n')) >= 0) {
        const line = g._rxBuffer.slice(0, nl).replace(/\r/g, '').trim()
        g._rxBuffer = g._rxBuffer.slice(nl + 1)
        if (line && line[0] === '{') {
          // 分发给所有注册的页面 listener
          g._lineListeners.forEach((fn) => {
            try { fn(line) } catch (e) { console.error('line listener error:', e) }
          })
        }
      }
    }
    wx.onBLECharacteristicValueChange(g._notifyHandler)
  },

  _watchGlobalConnState() {
    const g = this.globalData
    if (g._connStateHandler) return  // 已注册过
    g._connStateHandler = (res) => {
      if (res.deviceId !== g.deviceId) return
      if (!res.connected) {
        g.connected = false
        g.deviceId = ''
        g.deviceName = ''
        g._serviceId = ''
        g._notifyCharId = ''
        g._writeCharId = ''
        g._rxBuffer = ''
        this._notifyConnChange(false, '')
      }
    }
    wx.onBLEConnectionStateChange(g._connStateHandler)
  },

  _resetState() {
    const g = this.globalData
    g.connected = false
    g.deviceId = ''
    g.deviceName = ''
    g.rssi = 0
    g._serviceId = ''
    g._notifyCharId = ''
    g._writeCharId = ''
    g._rxBuffer = ''
    g._writeQueue = Promise.resolve()
  },

  /* ==================== 发送命令 ==================== */

  sendJson(obj) {
    const g = this.globalData
    const payload = JSON.stringify(obj) + '\r\n'
    if (!g.deviceId || !g._writeCharId) {
      return Promise.reject(new Error('未连接或特征未就绪'))
    }
    const chunks = []
    for (let i = 0; i < payload.length; i += 20) {
      chunks.push(payload.slice(i, i + 20))
    }
    g._writeQueue = g._writeQueue.then(() => {
      return chunks.reduce((p, chunk) => {
        return p.then(() => this._writeChunk(g.deviceId, chunk))
          .then(() => new Promise((r) => setTimeout(r, 40)))
      }, Promise.resolve())
    })
    return g._writeQueue
  },

  _writeChunk(deviceId, chunk) {
    const g = this.globalData
    return new Promise((resolve) => {
      const buf = new ArrayBuffer(chunk.length)
      const view = new Uint8Array(buf)
      for (let i = 0; i < chunk.length; i++) view[i] = chunk.charCodeAt(i)
      wx.writeBLECharacteristicValue({
        deviceId,
        serviceId: g._serviceId,
        characteristicId: g._writeCharId,
        value: buf,
        success: () => resolve(),
        fail: (err) => { console.error('写入失败:', err); resolve() }
      })
    })
  }
})
