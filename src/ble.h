/**
 * @file ble.h
 * @brief BLE 外设抽象接口（跨平台）
 * @note Linux 实现基于 BlueZ D-Bus（sdbus-c++），Windows 实现基于
 *       C++/WinRT GattServiceProvider；上层只依赖本接口与 IInfoSource。
 */
#pragma once

#include <memory>
#include <string>

/**
 * @brief BLE 外设数据源抽象接口
 * @note 由 main 组装（配置 + 系统信息），BLE 层不感知系统细节，实现解耦。
 */
class IInfoSource
{
public:
    virtual ~IInfoSource() = default;

    /** @brief 获取静态设备信息 JSON（title/厂商/版本/序列号/OS/硬件） */
    virtual std::string infoJson() const = 0;

    /** @brief 获取实时状态 JSON（CPU/内存/运行时间，每次调用重新采样） */
    virtual std::string statusJson() = 0;

    /** @brief 获取广播名称 */
    virtual std::string broadcastName() const = 0;

    /** @brief 获取服务 UUID */
    virtual std::string serviceUuid() const = 0;

    /** @brief 获取静态信息特征 UUID */
    virtual std::string infoCharUuid() const = 0;

    /** @brief 获取实时状态特征 UUID */
    virtual std::string statusCharUuid() const = 0;

    /** @brief 是否广播服务 UUID */
    virtual bool advertiseServiceUuid() const = 0;
};

/**
 * @brief BLE 外设抽象接口
 */
class IBlePeripheral
{
public:
    virtual ~IBlePeripheral() = default;

    /**
     * @brief 启动外设并进入事件循环（阻塞）
     * @return 0 正常退出；非 0 初始化失败
     */
    virtual int run() = 0;

    /**
     * @brief 请求停止（可在线程安全地从信号处理函数调用）
     */
    virtual void requestStop() = 0;
};

/**
 * @brief 创建当前平台的 BLE 外设
 * @param source 数据源（生命周期由调用方管理，须长于外设）
 * @return 外设实例
 */
std::unique_ptr<IBlePeripheral> createBlePeripheral(IInfoSource& source);
