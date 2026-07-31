/**
 * @file sysinfo.h
 * @brief 系统信息采集抽象接口（跨平台）
 * @note 平台实现：Linux 读 /proc 与 DMI；Windows 读 Win32 API 与 SMBIOS。
 *       上层（GATT 特征组装）只依赖本接口，实现可替换。
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

/**
 * @brief 系统实时状态快照
 */
struct SystemStats
{
    double cpuPercent = 0.0;        ///< CPU 使用率（0~100）
    double memoryPercent = 0.0;     ///< 内存使用率（0~100）
    uint64_t memoryTotalKb = 0;     ///< 总内存（KB）
    uint64_t memoryUsedKb = 0;      ///< 已用内存（KB）
    uint64_t uptimeSeconds = 0;     ///< 系统运行时间（秒）
};

/**
 * @brief 系统信息提供者抽象接口
 * @note sample() 内部维护 CPU 差值状态，连续调用返回相邻两次采样间的平均值；
 *       非线程安全，调用方需保证串行访问（BLE 事件循环内调用）。
 */
class ISystemInfoProvider
{
public:
    virtual ~ISystemInfoProvider() = default;

    /**
     * @brief 采集实时状态（CPU/内存/运行时间）
     * @return 状态快照
     */
    virtual SystemStats sample() = 0;

    /** @brief 获取操作系统名称（如 "Ubuntu 22.04.2 LTS" / "Windows 11"） */
    virtual std::string osName() const = 0;

    /** @brief 获取硬件型号（SMBIOS/DMI 产品名，如 "ThinkPad X1 Carbon"） */
    virtual std::string hardwareModel() const = 0;

    /** @brief 获取机器版本号（SMBIOS/DMI 产品版本；可被配置覆盖） */
    virtual std::string machineVersion() const = 0;

    /** @brief 获取厂商名称（SMBIOS/DMI 厂商；可被配置覆盖） */
    virtual std::string manufacturer() const = 0;

    /** @brief 获取机器序列号（SMBIOS/DMI 序列号；可被配置覆盖） */
    virtual std::string serialNumber() const = 0;
};

/**
 * @brief 创建当前平台的系统信息提供者
 * @return 提供者实例
 */
std::unique_ptr<ISystemInfoProvider> createSystemInfoProvider();
