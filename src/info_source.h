/**
 * @file info_source.h
 * @brief 信息源实现：配置 + 系统信息 → GATT 特征 JSON
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "ble.h"
#include "config.h"

class ISystemInfoProvider;

/**
 * @brief 组装 BLE 外设所需的信息（静态 info / 动态 status JSON）
 * @note statusJson 内部加锁，可被多线程（Windows 事件回调）安全调用。
 */
class InfoSource : public IInfoSource
{
public:
    /**
     * @brief 构造信息源
     * @param config 应用配置
     * @param sysinfo 系统信息提供者（所有权转移）
     */
    InfoSource(const AppConfig& config, std::unique_ptr<ISystemInfoProvider> sysinfo);

    std::string infoJson() const override;
    std::string statusJson() override;
    std::string broadcastName() const override;
    std::string serviceUuid() const override;
    std::string infoCharUuid() const override;
    std::string statusCharUuid() const override;
    bool advertiseServiceUuid() const override;

private:
    AppConfig config_;                             ///< 应用配置
    std::unique_ptr<ISystemInfoProvider> sysinfo_; ///< 系统信息提供者
    mutable std::mutex mutex_;                     ///< 状态采样互斥锁

    /** @brief 组装静态 info JSON（每次调用实时拼装，量小可接受） */
    std::string buildInfoJson() const;
};
