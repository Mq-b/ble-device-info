# Android 端（Expo React Native）

BLE 客户端：扫描 ble-device-info 外设（按服务 UUID 过滤），连接后读取设备信息与实时状态。

技术栈：Expo SDK 55 + React Native 0.83 + TypeScript + react-native-ble-plx

## 构建与运行（需要真机）

BLE 是原生模块，**Expo Go 不支持**，需 development build：

```bash
cd android
npm install
npx expo run:android        # 需要已连接 Android 真机（USB 调试）或已配置的构建环境
```

> 注意：Android 模拟器（AVD）不支持 BLE，必须使用真机。

## 使用

1. 先在 Linux / Windows 主机上运行 ble-device-info 外设程序
2. App 打开后点击「扫描设备」
3. 列表出现设备（显示序列号/广播名）后点击「连接」
4. 自动读取并展示：设备信息（title/厂商/版本号/序列号/OS/硬件）+ 实时状态（CPU/内存/运行时间）
5. 「刷新状态」按钮重新读取实时数据（也可在系统中按 intervalMs 自动轮询）

## 权限

- Android 12+：`BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT` 运行时权限（App 内自动申请）
- Android 13+：归入「附近设备」权限组
- 清单权限已在 `app.json` 中声明
