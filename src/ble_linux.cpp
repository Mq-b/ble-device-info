/**
 * @file ble_linux.cpp
 * @brief Linux BLE 外设实现：BlueZ D-Bus（sdbus-c++）
 * @note 通过 GattManager1.RegisterApplication 注册自定义 GATT 服务，
 *       LEAdvertisingManager1.RegisterAdvertisement 启动广播；
 *       特征读取支持按偏移分段（GATT Read Blob）。
 */
#include "ble.h"

#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

namespace {

// ---- BlueZ D-Bus 接口名 ----
constexpr const char* BLUEZ_NAME = "org.bluez";
constexpr const char* IFACE_OBJECT_MANAGER = "org.freedesktop.DBus.ObjectManager";
constexpr const char* IFACE_PROPERTIES = "org.freedesktop.DBus.Properties";
constexpr const char* IFACE_ADAPTER = "org.bluez.Adapter1";
constexpr const char* IFACE_DEVICE = "org.bluez.Device1";
constexpr const char* IFACE_GATT_MANAGER = "org.bluez.GattManager1";
constexpr const char* IFACE_LE_ADV_MANAGER = "org.bluez.LEAdvertisingManager1";
constexpr const char* IFACE_LE_ADV = "org.bluez.LEAdvertisement1";
constexpr const char* IFACE_GATT_SERVICE = "org.bluez.GattService1";
constexpr const char* IFACE_GATT_CHAR = "org.bluez.GattCharacteristic1";

// ---- 对象路径 ----
constexpr const char* APP_ROOT = "/com/bleinfo/app";
constexpr const char* ADV_PATH = "/com/bleinfo/adv0";

/** @brief 输出信息日志（带时间戳） */
void logInfo(const std::string& msg)
{
    std::cout << "[INFO] " << msg << std::endl;
}

/** @brief 输出警告日志 */
void logWarn(const std::string& msg)
{
    std::cout << "[WARN] " << msg << std::endl;
}

/** @brief 输出错误日志 */
void logError(const std::string& msg)
{
    std::cerr << "[ERROR] " << msg << std::endl;
}

/**
 * @brief 将 UTF-8 字符串转为字节数组
 * @param s 输入字符串
 * @return 字节数组
 */
std::vector<uint8_t> toBytes(const std::string& s)
{
    return std::vector<uint8_t>(s.begin(), s.end());
}

/**
 * @brief 按偏移截取数据（GATT Read Blob 分段读取）
 * @param data 完整数据
 * @param offset 请求偏移
 * @return 从偏移开始的数据；偏移越界返回空
 */
std::vector<uint8_t> sliceAt(const std::vector<uint8_t>& data, uint16_t offset)
{
    if (offset >= data.size()) {
        return {};
    }
    return std::vector<uint8_t>(data.begin() + offset, data.end());
}

/**
 * @brief 从 ReadValue options 中提取 offset
 * @param options BlueZ 传入的参数
 * @return 偏移值（缺省 0）
 */
uint16_t offsetFromOptions(const std::map<std::string, sdbus::Variant>& options)
{
    const auto it = options.find("offset");
    if (it == options.end()) {
        return 0;
    }
    return it->second.get<uint16_t>();
}

/**
 * @brief Linux（BlueZ）BLE 外设实现
 */
class LinuxBlePeripheral : public IBlePeripheral
{
public:
    /**
     * @brief 构造
     * @param source 数据源（外部持有）
     */
    explicit LinuxBlePeripheral(IInfoSource& source)
        : source_(source)
        , bus_(sdbus::createSystemBusConnection())
        , rootProxy_(sdbus::createProxy(*bus_, BLUEZ_NAME, "/"))
    {
    }

    ~LinuxBlePeripheral() override = default;

    int run() override
    {
        try {
            if (!prepare()) {
                cleanup();
                return 1;
            }
        } catch (const sdbus::Error& e) {
            logError(std::string("初始化失败: ") + e.getName() + " - " + e.getMessage());
            cleanup();
            return 1;
        } catch (const std::exception& e) {
            logError(std::string("初始化失败: ") + e.what());
            cleanup();
            return 1;
        }

        logInfo("BLE 外设已启动，广播名称: " + source_.broadcastName());
        lastConnCheck_ = std::chrono::steady_clock::now();

        while (!stopRequested_.load()) {
            const auto pollData = bus_->getEventLoopPollData();
            struct pollfd pfd {};
            pfd.fd = pollData.fd;
            pfd.events = pollData.events;

            int timeoutMs = pollData.getPollTimeout();
            if (timeoutMs < 0 || timeoutMs > 1000) {
                timeoutMs = 1000; // 保证断线检测按时执行
            }

            const int rc = ::poll(&pfd, 1, timeoutMs);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                logError(std::string("poll 失败: ") + std::strerror(errno));
                break;
            }
            if (rc > 0) {
                try {
                    bus_->processPendingRequest();
                } catch (const sdbus::Error& e) {
                    logWarn(std::string("处理 D-Bus 消息失败: ") + e.getMessage());
                }
            }
            checkConnection();
        }

        cleanup();
        logInfo("程序已退出");
        return 0;
    }

    void requestStop() override
    {
        stopRequested_.store(true);
    }

private:
    IInfoSource& source_;                       ///< 数据源引用
    std::unique_ptr<sdbus::IConnection> bus_;   ///< 系统总线
    std::unique_ptr<sdbus::IProxy> rootProxy_;  ///< org.bluez 根代理
    std::unique_ptr<sdbus::IProxy> adapterProxy_;
    std::unique_ptr<sdbus::IProxy> gattManagerProxy_;
    std::unique_ptr<sdbus::IProxy> advManagerProxy_;
    std::vector<std::unique_ptr<sdbus::IObject>> objects_; ///< 注册的对象
    std::string adapterPath_;                   ///< 适配器路径
    std::atomic<bool> stopRequested_{false};    ///< 停止标志
    bool hadConnection_ = false;                ///< 是否曾连接（断线重广播状态机）
    std::chrono::steady_clock::time_point lastConnCheck_;

    /** @brief 通过 ObjectManager 查找第一个蓝牙适配器 */
    std::string findAdapter()
    {
        std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> objects;
        rootProxy_->callMethod("GetManagedObjects")
            .onInterface(IFACE_OBJECT_MANAGER)
            .storeResultsTo(objects);
        for (const auto& [path, interfaces] : objects) {
            if (interfaces.find(IFACE_ADAPTER) != interfaces.end()) {
                return path;
            }
        }
        return {};
    }

    /** @brief 设置适配器电源与别名 */
    void configureAdapter()
    {
        adapterProxy_->callMethod("Set")
            .onInterface(IFACE_PROPERTIES)
            .withArguments(IFACE_ADAPTER, "Powered", sdbus::Variant{true});
        adapterProxy_->callMethod("Set")
            .onInterface(IFACE_PROPERTIES)
            .withArguments(IFACE_ADAPTER, "Alias", sdbus::Variant{source_.broadcastName()});
    }

    /** @brief 注册自定义服务与两个特征 */
    void createService()
    {
        const std::string servicePath = std::string(APP_ROOT) + "/service0";
        const std::string infoPath = servicePath + "/char0";
        const std::string statusPath = servicePath + "/char1";

        // 服务对象
        auto service = sdbus::createObject(*bus_, servicePath);
        service->registerProperty("UUID").onInterface(IFACE_GATT_SERVICE)
            .withGetter([this] { return source_.serviceUuid(); });
        service->registerProperty("Primary").onInterface(IFACE_GATT_SERVICE)
            .withGetter([] { return true; });
        service->finishRegistration();
        objects_.push_back(std::move(service));

        // 静态信息特征（info）
        addCharacteristic(infoPath, servicePath, source_.infoCharUuid(),
                          [this] { return toBytes(source_.infoJson()); });

        // 实时状态特征（status）
        addCharacteristic(statusPath, servicePath, source_.statusCharUuid(),
                          [this] { return toBytes(source_.statusJson()); });
    }

    /**
     * @brief 注册一个只读特征（支持分段读取）
     * @param charPath 特征对象路径
     * @param servicePath 父服务路径
     * @param uuid 特征 UUID
     * @param valueGetter 取值回调
     */
    void addCharacteristic(const std::string& charPath,
                           const std::string& servicePath,
                           const std::string& uuid,
                           std::function<std::vector<uint8_t>()> valueGetter)
    {
        auto obj = sdbus::createObject(*bus_, charPath);
        obj->registerProperty("UUID").onInterface(IFACE_GATT_CHAR)
            .withGetter([uuid] { return uuid; });
        obj->registerProperty("Service").onInterface(IFACE_GATT_CHAR)
            .withGetter([servicePath] { return sdbus::ObjectPath(servicePath); });
        obj->registerProperty("Value").onInterface(IFACE_GATT_CHAR)
            .withGetter([valueGetter] { return valueGetter(); });
        obj->registerProperty("Notifying").onInterface(IFACE_GATT_CHAR)
            .withGetter([] { return false; });
        obj->registerProperty("Flags").onInterface(IFACE_GATT_CHAR)
            .withGetter([] { return std::vector<std::string>{"read"}; });
        obj->registerMethod("ReadValue").onInterface(IFACE_GATT_CHAR)
            .implementedAs([valueGetter](const std::map<std::string, sdbus::Variant>& options) {
                return sliceAt(valueGetter(), offsetFromOptions(options));
            });
        obj->registerMethod("WriteValue").onInterface(IFACE_GATT_CHAR)
            .implementedAs([](const std::vector<uint8_t>&, const std::map<std::string, sdbus::Variant>&) {
                // 只读特征，写入直接忽略
            });
        obj->finishRegistration();
        objects_.push_back(std::move(obj));
    }

    /** @brief 创建 LE 广播对象（名称=序列号，服务 UUID 可配置） */
    void createAdvertisement()
    {
        auto adv = sdbus::createObject(*bus_, ADV_PATH);
        adv->registerProperty("Type").onInterface(IFACE_LE_ADV)
            .withGetter([] { return std::string("peripheral"); });
        adv->registerProperty("LocalName").onInterface(IFACE_LE_ADV)
            .withGetter([this] { return source_.broadcastName(); });
        adv->registerProperty("Discoverable").onInterface(IFACE_LE_ADV)
            .withGetter([] { return true; });
        if (source_.advertiseServiceUuid()) {
            adv->registerProperty("ServiceUUIDs").onInterface(IFACE_LE_ADV)
                .withGetter([this] { return std::vector<std::string>{source_.serviceUuid()}; });
        }
        adv->finishRegistration();
        objects_.push_back(std::move(adv));
    }

    /** @brief 初始化流程 */
    bool prepare()
    {
        adapterPath_ = findAdapter();
        if (adapterPath_.empty()) {
            logError("未找到蓝牙适配器（请确认 bluetoothd 已运行且有蓝牙硬件）");
            return false;
        }
        logInfo("使用蓝牙适配器 " + adapterPath_);

        adapterProxy_ = sdbus::createProxy(*bus_, BLUEZ_NAME, adapterPath_);
        gattManagerProxy_ = sdbus::createProxy(*bus_, BLUEZ_NAME, adapterPath_);
        advManagerProxy_ = sdbus::createProxy(*bus_, BLUEZ_NAME, adapterPath_);

        configureAdapter();
        createService();
        createAdvertisement();

        gattManagerProxy_->callMethod("RegisterApplication")
            .onInterface(IFACE_GATT_MANAGER)
            .withArguments(sdbus::ObjectPath(APP_ROOT), std::map<std::string, sdbus::Variant>{});
        logInfo("GATT 服务已注册");

        advManagerProxy_->callMethod("RegisterAdvertisement")
            .onInterface(IFACE_LE_ADV_MANAGER)
            .withArguments(sdbus::ObjectPath(ADV_PATH), std::map<std::string, sdbus::Variant>{});
        logInfo("BLE 广播已启动");
        return true;
    }

    /** @brief 查询是否有手机连接 */
    bool hasConnections()
    {
        try {
            std::map<sdbus::ObjectPath, std::map<std::string, std::map<std::string, sdbus::Variant>>> objects;
            rootProxy_->callMethod("GetManagedObjects")
                .onInterface(IFACE_OBJECT_MANAGER)
                .storeResultsTo(objects);
            const std::string prefix = adapterPath_ + "/";
            for (const auto& [path, interfaces] : objects) {
                if (path.rfind(prefix, 0) == 0 && interfaces.find(IFACE_DEVICE) != interfaces.end()) {
                    return true;
                }
            }
        } catch (const sdbus::Error& e) {
            logWarn(std::string("查询连接状态失败: ") + e.getMessage());
        }
        return false;
    }

    /** @brief 断开连接后重新注册广播（BlueZ 不会自动恢复） */
    void rebroadcast()
    {
        try {
            advManagerProxy_->callMethod("UnregisterAdvertisement")
                .onInterface(IFACE_LE_ADV_MANAGER)
                .withArguments(sdbus::ObjectPath(ADV_PATH));
        } catch (const sdbus::Error&) {
            // 广播可能已被清理，忽略
        }
        try {
            advManagerProxy_->callMethod("RegisterAdvertisement")
                .onInterface(IFACE_LE_ADV_MANAGER)
                .withArguments(sdbus::ObjectPath(ADV_PATH), std::map<std::string, sdbus::Variant>{});
            logInfo("广播已重新注册");
        } catch (const sdbus::Error& e) {
            logWarn(std::string("重新注册广播失败: ") + e.getName() + " - " + e.getMessage());
        }
    }

    /** @brief 周期任务：检测断开并重广播 */
    void checkConnection()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastConnCheck_ < std::chrono::seconds(2)) {
            return;
        }
        lastConnCheck_ = now;

        const bool connected = hasConnections();
        if (hadConnection_ && !connected) {
            logInfo("客户端已断开，重新注册广播");
            rebroadcast();
            hadConnection_ = false;
        } else if (!hadConnection_ && connected) {
            logInfo("客户端已连接");
            hadConnection_ = true;
        }
    }

    /** @brief 退出清理 */
    void cleanup()
    {
        try {
            if (advManagerProxy_) {
                advManagerProxy_->callMethod("UnregisterAdvertisement")
                    .onInterface(IFACE_LE_ADV_MANAGER)
                    .withArguments(sdbus::ObjectPath(ADV_PATH));
            }
        } catch (...) {
        }
        try {
            if (gattManagerProxy_) {
                gattManagerProxy_->callMethod("UnregisterApplication")
                    .onInterface(IFACE_GATT_MANAGER)
                    .withArguments(sdbus::ObjectPath(APP_ROOT));
            }
        } catch (...) {
        }
        objects_.clear();
    }
};

} // namespace

std::unique_ptr<IBlePeripheral> createBlePeripheral(IInfoSource& source)
{
    return std::make_unique<LinuxBlePeripheral>(source);
}
