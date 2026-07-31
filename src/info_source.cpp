/**
 * @file info_source.cpp
 * @brief 信息源实现：组装 GATT 特征 JSON
 */
#include "info_source.h"

#include <ctime>

#include "json.h"
#include "sysinfo.h"

namespace {

/**
 * @brief 获取当前 Unix 时间戳
 * @return 秒
 */
int64_t unixTimestamp()
{
    return static_cast<int64_t>(std::time(nullptr));
}

} // namespace

InfoSource::InfoSource(const AppConfig& config, std::unique_ptr<ISystemInfoProvider> sysinfo)
    : config_(config)
    , sysinfo_(std::move(sysinfo))
{
}

std::string InfoSource::infoJson() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    json::Value::Object obj;
    obj.emplace_back("title", json::Value(config_.title));
    // 配置优先，其次系统采集值
    obj.emplace_back("manufacturer", json::Value(config_.manufacturer.empty() ? sysinfo_->manufacturer() : config_.manufacturer));
    obj.emplace_back("machineVersion", json::Value(config_.machineVersion.empty() ? sysinfo_->machineVersion() : config_.machineVersion));
    obj.emplace_back("serial", json::Value(config_.serial.empty() ? sysinfo_->serialNumber() : config_.serial));
    obj.emplace_back("os", json::Value(sysinfo_->osName()));
    obj.emplace_back("hardware", json::Value(sysinfo_->hardwareModel()));
    return json::Value(std::move(obj)).stringify();
}

std::string InfoSource::statusJson()
{
    std::lock_guard<std::mutex> lock(mutex_);

    const SystemStats stats = sysinfo_->sample();

    json::Value::Object obj;
    obj.emplace_back("cpu", json::Value(stats.cpuPercent));
    obj.emplace_back("memory", json::Value(stats.memoryPercent));
    obj.emplace_back("memoryTotal", json::Value(stats.memoryTotalKb));
    obj.emplace_back("memoryUsed", json::Value(stats.memoryUsedKb));
    obj.emplace_back("uptime", json::Value(stats.uptimeSeconds));
    obj.emplace_back("intervalMs", json::Value(config_.statusIntervalMs));
    obj.emplace_back("timestamp", json::Value(unixTimestamp()));
    return json::Value(std::move(obj)).stringify();
}

std::string InfoSource::broadcastName() const
{
    // 优先级：配置广播名 → 配置序列号 → 系统序列号 → 兜底名
    if (!config_.broadcastName.empty()) {
        return config_.broadcastName;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.serial.empty()) {
        return config_.serial;
    }
    const std::string serial = sysinfo_->serialNumber();
    return serial.empty() ? std::string("ble-device-info") : serial;
}

std::string InfoSource::serviceUuid() const
{
    return config_.serviceUuid;
}

std::string InfoSource::infoCharUuid() const
{
    return config_.infoCharUuid;
}

std::string InfoSource::statusCharUuid() const
{
    return config_.statusCharUuid;
}

bool InfoSource::advertiseServiceUuid() const
{
    return config_.advertiseServiceUuid;
}
