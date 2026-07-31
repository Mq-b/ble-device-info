/**
 * @file sysinfo_windows.cpp
 * @brief Windows 系统信息实现：Win32 API 与 SMBIOS 固件表
 */
#include "sysinfo.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>   // RtlGetVersion

#include <cstring>
#include <vector>

namespace {

/** @brief 将宽字符串转换为 UTF-8 */
std::string wideToUtf8(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

/**
 * @brief 从 SMBIOS 固件表中提取字符串字段
 * @param type 结构类型（如 1 = System Information）
 * @param fieldOffset 字符串字段在结构数据中的偏移（如 Type1 的厂商名偏移 4）
 * @return 字段值；失败返回空串
 */
std::string readSmbiosString(BYTE type, BYTE fieldOffset)
{
    // 先查询缓冲区大小
    const DWORD size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (size < sizeof(DWORD) * 2) {
        return {};
    }
    std::vector<BYTE> buffer(size);
    if (GetSystemFirmwareTable('RSMB', 0, buffer.data(), size) != size) {
        return {};
    }

    // 头 8 字节为 RawSMBIOSData（4 字节 Used20CallingMethod + 版本 + 4 字节 Length）
    const BYTE* data = buffer.data() + 8;
    const DWORD length = *reinterpret_cast<const DWORD*>(buffer.data() + 4);
    const BYTE* end = data + length;

    const BYTE* p = data;
    while (p + 4 <= end) {
        const BYTE typeNow = p[0];
        const BYTE len = p[1];
        if (len < 4 || p + len + 2 > end) {
            break; // 结构异常
        }
        if (typeNow == 127) {
            break; // 结束结构
        }
        if (typeNow == type && fieldOffset < len) {
            // 提取字符串编号并到字符串表中查找
            const BYTE index = p[fieldOffset];
            if (index > 0) {
                const BYTE* str = p + len;
                BYTE current = 1;
                while (str < end) {
                    const char* s = reinterpret_cast<const char*>(str);
                    if (current == index) {
                        return std::string(s);
                    }
                    str += std::strlen(s) + 1;
                    ++current;
                }
            }
            return {};
        }
        // 跳到下一个结构（数据 + 字符串表 + 结尾双零）
        const BYTE* str = p + len;
        while (str < end) {
            if (*str == 0 && str + 1 < end && *(str + 1) == 0) {
                str += 2;
                break;
            }
            ++str;
        }
        p = str;
    }
    return {};
}

/**
 * @brief Windows 系统信息提供者
 */
class WindowsSystemInfo : public ISystemInfoProvider
{
public:
    WindowsSystemInfo() = default;

    SystemStats sample() override
    {
        SystemStats stats;
        stats.cpuPercent = sampleCpu();
        sampleMemory(stats);
        stats.uptimeSeconds = GetTickCount64() / 1000ULL;
        return stats;
    }

    std::string osName() const override
    {
        // RtlGetVersion 不受应用程序清单版本限制
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        const auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        if (fn == nullptr) {
            return "Windows";
        }
        RTL_OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        if (fn(&info) != 0) {
            return "Windows";
        }
        const std::string version = std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);
        if (info.dwMajorVersion >= 10 && info.dwBuildNumber >= 22000) {
            return "Windows 11 (" + version + ")";
        }
        return "Windows " + version;
    }

    std::string hardwareModel() const override
    {
        std::string model = readSmbiosString(1, 5); // Type1 产品名
        if (model.empty()) {
            // 回退：注册表中的计算机型号
            const std::string reg = readRegistryModel();
            if (!reg.empty()) {
                return reg;
            }
        }
        return model;
    }

    std::string machineVersion() const override
    {
        return readSmbiosString(1, 6); // Type1 产品版本
    }

    std::string manufacturer() const override
    {
        std::string vendor = readSmbiosString(1, 4); // Type1 厂商
        if (vendor.empty()) {
            vendor = readSmbiosString(0, 4); // Type0 BIOS 厂商
        }
        return vendor;
    }

    std::string serialNumber() const override
    {
        return readSmbiosString(1, 7); // Type1 序列号
    }

private:
    ULONGLONG lastIdle_ = 0;   ///< 上次空闲时间（100ns）
    ULONGLONG lastKernel_ = 0; ///< 上次内核时间（100ns）
    ULONGLONG lastUser_ = 0;   ///< 上次用户时间（100ns）

    /** @brief 通过 GetSystemTimes 差值计算 CPU 使用率 */
    double sampleCpu()
    {
        FILETIME idle{}, kernel{}, user{};
        if (!GetSystemTimes(&idle, &kernel, &user)) {
            return 0.0;
        }
        const auto toU64 = [](const FILETIME& ft) {
            return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
        const ULONGLONG idleNow = toU64(idle);
        const ULONGLONG kernelNow = toU64(kernel);
        const ULONGLONG userNow = toU64(user);

        if (lastKernel_ == 0 && lastUser_ == 0) {
            lastIdle_ = idleNow;
            lastKernel_ = kernelNow;
            lastUser_ = userNow;
            return 0.0;
        }

        const ULONGLONG idleDelta = idleNow - lastIdle_;
        // 内核时间包含空闲时间，忙时间 = (内核 + 用户 - 空闲) 的增量
        const ULONGLONG totalDelta = (kernelNow - lastKernel_) + (userNow - lastUser_);
        lastIdle_ = idleNow;
        lastKernel_ = kernelNow;
        lastUser_ = userNow;

        if (totalDelta == 0) {
            return 0.0;
        }
        const double busy = static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta) * 100.0;
        return busy < 0.0 ? 0.0 : (busy > 100.0 ? 100.0 : busy);
    }

    /** @brief 通过 GlobalMemoryStatusEx 采样内存 */
    void sampleMemory(SystemStats& stats)
    {
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        if (!GlobalMemoryStatusEx(&mem)) {
            return;
        }
        stats.memoryTotalKb = mem.ullTotalPhys / 1024ULL;
        stats.memoryUsedKb = (mem.ullTotalPhys - mem.ullAvailPhys) / 1024ULL;
        if (mem.ullTotalPhys > 0) {
            stats.memoryPercent = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys) / static_cast<double>(mem.ullTotalPhys) * 100.0;
        }
    }

    /** @brief 从注册表读取计算机型号（SMBIOS 缺失时的回退） */
    std::string readRegistryModel() const
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &key) != ERROR_SUCCESS) {
            return {};
        }
        wchar_t value[256] = {0};
        DWORD size = sizeof(value);
        const LSTATUS rc = RegQueryValueExW(key, L"SystemProductName", nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size);
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS) {
            return {};
        }
        return wideToUtf8(value);
    }
};

} // namespace

std::unique_ptr<ISystemInfoProvider> createSystemInfoProvider()
{
    return std::make_unique<WindowsSystemInfo>();
}
