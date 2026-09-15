App({
  globalData: {
    // 与固件约定：蓝牙模块名关键字（BT24 出厂默认名，若用 AT+NAME 改过名请在此追加）
    nameKeywords: ['BT24', 'SMART'],
    // 服务/特征 UUID（DX-BT24 出厂默认，可用 AT+UUID / AT+CHAR 修改后同步此处）
    SERVICE_UUID: 'FFE0',
    NOTIFY_CHAR_UUID: 'FFE1',
    WRITE_CHAR_UUID: 'FFE2'
  }
})
