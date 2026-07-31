/**
 * @file config.h
 * @brief 应用配置：加载 JSON 配置文件（程序目录 → 用户目录 → 系统目录）
 * @note 配置查找顺序（先到先用）：
 *       1. 命令行 --config 显式指定的路径
 *       2. 程序同目录 config.json
 *       3. 用户配置目录（Linux: $XDG_CONFIG_HOME 或 ~/.config；Windows: %APPDATA%）
 *       4. 系统目录（仅 Linux: /etc/ble-device-info/config.json）
 */
#pragma once

#include <string>

/**
 * @brief 应用配置结构（与 config.json 字段一一对应）
 */
struct AppConfig
{
    // ---- identity：设备身份信息（可覆盖系统采集值）----
    std::string title = "BLE Device";           ///< 设备标题
    std::string manufacturer;                   ///< 厂商名称（空则用系统采集值）
    std::string machineVersion;                 ///< 机器版本号（空则用系统采集值）
    std::string serial;                         ///< 机器序列号（空则用系统采集值）

    // ---- ble：BLE 外设参数 ----
    std::string broadcastName;                  ///< 广播名称（空则用 serial）
    bool advertiseServiceUuid = true;           ///< 是否广播服务 UUID
    std::string serviceUuid = "7e57d001-3f9a-4a6b-9f6c-000000000001";   ///< 服务 UUID
    std::string infoCharUuid = "7e57d002-3f9a-4a6b-9f6c-000000000002";  ///< 静态信息特征 UUID
    std::string statusCharUuid = "7e57d003-3f9a-4a6b-9f6c-000000000003"; ///< 实时状态特征 UUID

    // ---- sysinfo：系统信息采集 ----
    int cpuSampleMs = 1000;                     ///< CPU 采样间隔（毫秒）
    int statusIntervalMs = 2000;                ///< 客户端建议轮询间隔（毫秒，写入 JSON 供客户端参考）
};

/**
 * @brief 查找配置文件路径
 * @param explicitPath 命令行显式指定的路径（可为空）
 * @return 找到的配置文件路径；未找到返回空串
 */
std::string findConfigPath(const std::string& explicitPath);

/**
 * @brief 加载配置
 * @param explicitPath 命令行显式指定的路径（可为空）
 * @return 配置（缺省字段使用默认值）
 * @note 配置文件不存在或解析失败时输出警告并返回默认配置。
 */
AppConfig loadConfig(const std::string& explicitPath);
