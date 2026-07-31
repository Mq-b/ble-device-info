# ble-device-info

跨平台（Linux / Windows）BLE 外设程序：通过蓝牙低功耗向手机暴露设备状态信息（CPU、内存、硬件信息等）。

## 功能特性

- **跨平台**：Linux（BlueZ D-Bus + sdbus-c++）/ Windows（C++/WinRT GattServiceProvider），同一份 CMake 工程
- **信息内容**：CPU 使用率、内存使用率/总量/已用、运行时间、OS 名称、硬件型号、厂商、版本号、序列号、自定义 title
- **广播精简**：只广播名称（默认机器序列号）与服务 UUID，不广播多余数据
- **配置化**：JSON 配置文件（程序同目录 → 用户目录 → 系统目录），身份信息可覆盖系统采集值
- **解耦设计**：系统信息采集（`sysinfo`）与 BLE 通信（`ble`）通过抽象接口解耦，可独立替换
- **零第三方依赖**（Linux 依赖系统 sdbus-c++，Windows 依赖系统 WinRT）；JSON 解析为内置轻量实现
- **长特征读取**：支持 GATT Read Blob 分段读取（JSON 特征值超过单包 MTU 时自动分片）

## BLE 协议

### 服务

| 项目 | UUID |
|---|---|
| 服务（自定义） | `7e57d001-3f9a-4a6b-9f6c-000000000001` |
| 静态信息特征（只读） | `7e57d002-3f9a-4a6b-9f6c-000000000002` |
| 实时状态特征（只读） | `7e57d003-3f9a-4a6b-9f6c-000000000003` |

### 静态信息特征（info，读一次即可）

```json
{
  "title": "我的设备",
  "manufacturer": "LENOVO",
  "machineVersion": "ThinkPad X1 Carbon Gen 10",
  "serial": "PF3XXXXX",
  "os": "Windows 11 (10.0.26100.8655)",
  "hardware": "82YA"
}
```

### 实时状态特征（status，每次读取返回最新值，建议按 `intervalMs` 轮询）

```json
{
  "cpu": 12.5,
  "memory": 43.2,
  "memoryTotal": 33271936,
  "memoryUsed": 14375528,
  "uptime": 86400,
  "intervalMs": 2000,
  "timestamp": 1785574800
}
```

## 目录结构

```
src/
  main.cpp            入口（参数解析、信号处理、装配）
  config.h/.cpp       配置加载（JSON：程序目录 → 用户目录 → 系统目录）
  json.h/.cpp         轻量 JSON 解析/序列化（零依赖）
  sysinfo.h           系统信息抽象接口
  sysinfo_linux.cpp   Linux 实现（/proc、/sys DMI、os-release）
  sysinfo_windows.cpp Windows 实现（Win32 API、SMBIOS 固件表）
  ble.h               BLE 外设抽象接口 + 数据源接口
  ble_linux.cpp       Linux 实现（BlueZ D-Bus）
  ble_windows.cpp     Windows 实现（C++/WinRT GattServiceProvider）
  info_source.h/.cpp  信息源组装（配置 + 系统信息 → GATT JSON）
scripts/
  verify_ble.py       Python 验证脚本（bleak 客户端）
deploy/
  ble-device-info.service  systemd 服务示例（可选）
android/              Android 端（Expo React Native，另见 android/README.md）
```

## 构建

### Linux（Ubuntu 22.04 等）

```bash
sudo apt install -y libsdbus-c++-dev bluez libbluetooth-dev libsystemd-dev pkg-config cmake g++
cmake -S . -B build
cmake --build build -j4
```

### Windows（VS2022 + Windows 10 SDK）

```powershell
# 需要：Visual Studio 2022（含 C++ 桌面开发）、Windows 10 SDK（含 C++/WinRT）
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release
# 或使用 Ninja：
#   cmd /c ""<VS>\VC\Auxiliary\Build\vcvars64.bat" && cmake -S . -B build-win -G Ninja"
```

## 配置

配置文件 `config.json`，查找顺序（先到先用）：

1. 命令行 `--config <路径>`
2. 程序同目录 `config.json`
3. 用户目录（Linux `~/.config/ble-device-info/`，Windows `%APPDATA%\ble-device-info\`）
4. 系统目录（仅 Linux `/etc/ble-device-info/config.json`）

参考 `config.example.json`：

```json
{
  "identity": {
    "title": "我的设备",
    "manufacturer": "",
    "machineVersion": "",
    "serial": ""
  },
  "ble": {
    "broadcastName": "",
    "advertiseServiceUuid": true,
    "serviceUuid": "7e57d001-3f9a-4a6b-9f6c-000000000001",
    "infoCharUuid": "7e57d002-3f9a-4a6b-9f6c-000000000002",
    "statusCharUuid": "7e57d003-3f9a-4a6b-9f6c-000000000003"
  },
  "sysinfo": {
    "cpuSampleMs": 1000,
    "statusIntervalMs": 2000
  }
}
```

说明：
- `identity` 字段留空则使用系统采集值；`serial` 同时用作默认广播名
- `broadcastName` 留空则使用 `serial`
- 广播名超过 24 字符会被截断（BLE 广播包 31 字节限制）

## 运行

```bash
# Linux：直接运行（需可访问蓝牙适配器；可加 bluetooth 组或使用 root）
./build/ble-device-info --config /path/to/config.json

# Windows
.\build-win\ble-device-info.exe --config config.json
```

### systemd 服务（Linux，可选）

```bash
sudo cp deploy/ble-device-info.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now ble-device-info
```

程序本身不依赖系统服务，单可执行文件 + 配置文件即可运行；systemd 仅为开机自启提供。

## 验证

### Python 脚本（需另一台有蓝牙的设备运行，作为 BLE 客户端）

```bash
pip install bleak
python scripts/verify_ble.py --name <序列号或广播名>
# 或按服务 UUID 过滤：
python scripts/verify_ble.py --service-uuid 7e57d001-3f9a-4a6b-9f6c-000000000001
```

脚本会扫描、连接、读取两个特征、校验 JSON 字段并输出汇总。

> 注意：BLE 不支持同机回环——外设程序与验证脚本必须运行在两台不同设备上（如外设在 Linux 主机，脚本在 Windows/手机）。

### 手机

- **Android 端**：见 `android/` 目录（Expo React Native，需真机，模拟器不支持 BLE）
- 任意 BLE 工具（如 nRF Connect）也可手动连接读取

## 平台差异说明

| 项目 | Linux | Windows |
|---|---|---|
| 自定义广播名（序列号） | 支持 | 部分 Intel 适配器不支持自定义广播名，将使用系统蓝牙名称（服务 UUID 广播不受影响）|
| 外设角色要求 | 蓝牙适配器（BlueZ） | 适配器需支持 LE 外设角色（`IsPeripheralRoleSupported`）|
| 服务 UUID 广播 | 可配置开关 | 由 GattServiceProvider 广播（始终开启，为连接前提）|

## 开源协议

MIT License，详见 [LICENSE](LICENSE)。
