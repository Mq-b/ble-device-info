/**
 * @file ble_windows.cpp
 * @brief Windows BLE 外设实现：C++/WinRT GattServiceProvider
 * @note 依赖 Windows 10 1809+ 与支持 LE 外设角色（peripheral role）的蓝牙适配器；
 *       特征读取支持按偏移分段（GATT Read Blob）。
 */
#include "ble.h"

#include <atomic>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <windows.h>
#include <combaseapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;

namespace {

/** @brief 输出信息日志 */
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
 * @brief 将 UTF-8 字符串转为 IBuffer（含偏移截取）
 * @param data 完整数据
 * @param offset 起始偏移
 * @return 缓冲区；偏移越界返回 nullptr
 */
IBuffer toBuffer(const std::string& data, uint32_t offset)
{
    if (offset >= data.size()) {
        return nullptr;
    }
    DataWriter writer;
    const std::string part = data.substr(offset);
    writer.WriteBytes(std::vector<uint8_t>(part.begin(), part.end()));
    return writer.DetachBuffer();
}

/**
 * @brief 将 UUID 字符串解析为 GUID（兼容大小写与连字符格式）
 * @param uuid 形如 "7e57d001-3f9a-4a6b-9f6c-000000000001"
 * @return GUID；解析失败返回空 GUID
 */
GUID parseGuid(const std::string& uuid)
{
    GUID result{};
    // CLSIDFromString 要求花括号包裹的 GUID 格式，这里自动补全
    std::wstring wstr;
    if (!uuid.empty() && uuid.front() == L'{') {
        wstr.assign(uuid.begin(), uuid.end());
    } else {
        wstr = L"{" + std::wstring(uuid.begin(), uuid.end()) + L"}";
    }
    if (CLSIDFromString(wstr.c_str(), &result) != S_OK) {
        logError("无效的 UUID: " + uuid);
    }
    return result;
}

/**
 * @brief Windows（WinRT）BLE 外设实现
 */
class WindowsBlePeripheral : public IBlePeripheral
{
public:
    /**
     * @brief 构造
     * @param source 数据源（外部持有）
     */
    explicit WindowsBlePeripheral(IInfoSource& source)
        : source_(source)
    {
    }

    ~WindowsBlePeripheral() override = default;

    int run() override
    {
        // 初始化 COM 与 WinRT 运行环境
        init_apartment(apartment_type::multi_threaded);

        try {
            const GUID serviceGuid = parseGuid(source_.serviceUuid());

            // 创建 GATT 服务提供者
            const auto providerResult = GattServiceProvider::CreateAsync(serviceGuid).get();
            if (providerResult.Error() != BluetoothError::Success) {
                logError("创建 GATT 服务失败（错误码: " + std::to_string(static_cast<int>(providerResult.Error())) + "）");
                return 1;
            }
            provider_ = providerResult.ServiceProvider();

            // 注册两个只读特征
            addReadCharacteristic(source_.infoCharUuid(), [this] { return source_.infoJson(); });
            addReadCharacteristic(source_.statusCharUuid(), [this] { return source_.statusJson(); });

            // 服务广播状态监控
            provider_.AdvertisementStatusChanged([this](GattServiceProvider const&, GattServiceProviderAdvertisementStatusChangedEventArgs const& args) {
                const auto status = args.Status();
                if (status == GattServiceProviderAdvertisementStatus::Aborted) {
                    logError("GATT 服务广播被中止（请确认蓝牙适配器支持 LE 外设角色）");
                } else if (status == GattServiceProviderAdvertisementStatus::Started) {
                    logInfo("GATT 服务广播已启动");
                }
            });
            logInfo("启动 GATT 服务广播...");
            provider_.StartAdvertising();

            // 独立广播：本地名称（序列号）
            // 注意：服务 UUID 由 GattServiceProvider 广播，publisher 只广播名字，
            // 避免 LocalName + 128 位 UUID 合计超过广播包 31 字节限制。
            // 部分 Intel 无线适配器不支持自定义广播（publisher），此时降级运行：
            // 手机仍可按服务 UUID 发现并连接，广播名使用系统蓝牙名称。
            std::string advName = source_.broadcastName();
            if (advName.size() > 24) { // 广播包限制保护（2 字节头 + 名字 ≤ 31 字节）
                logWarn("广播名超过 24 字符，已截断: " + advName);
                advName = advName.substr(0, 24);
            }
            publisher_ = BluetoothLEAdvertisementPublisher();
            publisher_.Advertisement().LocalName(winrt::to_hstring(advName));
            try {
                publisher_.Start();
                logInfo("自定义名称广播已启动: " + advName);
            } catch (const hresult_error&) {
                logWarn("自定义名称广播不可用（部分 Intel 适配器限制），将使用系统蓝牙名称；服务 UUID 广播不受影响");
            }

            logInfo("BLE 外设已启动");

            // 主循环：等待停止信号
            while (!stopRequested_.load()) {
                Sleep(200);
            }

            publisher_.Stop();
            provider_.StopAdvertising();
            logInfo("程序已退出");
            return 0;
        } catch (const hresult_error& e) {
            logError(std::string("初始化失败: 0x") + std::to_string(static_cast<uint32_t>(e.code())) + " - " + winrt::to_string(e.message()));
            return 1;
        }
    }

    void requestStop() override
    {
        stopRequested_.store(true);
    }

private:
    IInfoSource& source_;                   ///< 数据源引用
    GattServiceProvider provider_{nullptr}; ///< GATT 服务提供者
    BluetoothLEAdvertisementPublisher publisher_{nullptr}; ///< 广播发布器
    std::atomic<bool> stopRequested_{false}; ///< 停止标志

    /**
     * @brief 注册一个只读特征（支持分段读取）
     * @param uuidStr 特征 UUID 字符串
     * @param valueGetter 取值回调（事件回调线程中调用）
     */
    void addReadCharacteristic(const std::string& uuidStr, std::function<std::string()> valueGetter)
    {
        GattLocalCharacteristicParameters params;
        params.CharacteristicProperties(GattCharacteristicProperties::Read);
        params.UserDescription(winrt::to_hstring(uuidStr));

        const auto result = provider_.Service().CreateCharacteristicAsync(parseGuid(uuidStr), params).get();
        if (result.Error() != BluetoothError::Success) {
            throw hresult_error(E_FAIL, L"创建特征失败");
        }

        const GattLocalCharacteristic characteristic = result.Characteristic();
        characteristic.ReadRequested([this, valueGetter](GattLocalCharacteristic const&, GattReadRequestedEventArgs const& args) {
            // 读取请求为异步获取，完成后在回调中应答
            const auto deferral = args.GetDeferral();
            const auto requestAsync = args.GetRequestAsync();
            requestAsync.Completed([this, valueGetter, deferral](auto const& sender, winrt::Windows::Foundation::AsyncStatus status) {
                try {
                    if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
                        const auto request = sender.GetResults();
                        const IBuffer buffer = toBuffer(valueGetter(), request.Offset());
                        if (buffer == nullptr) {
                            request.RespondWithProtocolError(GattProtocolError::InvalidOffset());
                        } else {
                            request.RespondWithValue(buffer);
                        }
                    }
                } catch (const hresult_error& e) {
                    logWarn(std::string("处理读取请求失败: ") + winrt::to_string(e.message()));
                }
                deferral.Complete();
            });
        });
        logInfo("已注册特征: " + uuidStr);
    }
};

} // namespace

std::unique_ptr<IBlePeripheral> createBlePeripheral(IInfoSource& source)
{
    return std::make_unique<WindowsBlePeripheral>(source);
}
