/**
 * SMART-M 蓝牙监控小程序 — 历史数据页
 *
 * 数据来源：首页每收到一帧遥测即存入手机本地存储（key=bt_hist，最多300条），
 * 本页按时间倒序展示，可一键清空。
 */
Page({
  data: {
    records: [],
    count: 0,
    latest: ''
  },

  onShow() {
    this.load()
  },

  load() {
    const hist = wx.getStorageSync('bt_hist') || []
    const records = hist.slice().reverse() // 最新在前
    this.setData({
      records,
      count: hist.length,
      latest: hist.length ? hist[hist.length - 1].time : ''
    })
  },

  onClear() {
    wx.showModal({
      title: '确认清空',
      content: '将删除手机本地保存的全部历史记录',
      confirmColor: '#d73a49',
      success: (res) => {
        if (res.confirm) {
          wx.removeStorageSync('bt_hist')
          this.load()
          wx.showToast({ title: '已清空', icon: 'success' })
        }
      }
    })
  },

  onBack() {
    wx.navigateBack()
  }
})
