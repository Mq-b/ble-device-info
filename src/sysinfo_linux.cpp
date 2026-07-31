/**
 * @file sysinfo_linux.cpp
 * @brief Linux 系统信息实现：/proc 与 DMI 数据
 */
#include "sysinfo.h"

#include <fstream>
#include <sstream>

namespace {

/** @brief 读取单行文件内容并去除首尾空白 */
std::string readTrimmed(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::string line;
    std::getline(file, line);
    // 去除尾部空白
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    return line;
}

/** @brief 读取 DMI 字段（/sys/class/dmi/id/xxx），失败返回空串 */
std::string readDmi(const std::string& field)
{
    std::string value = readTrimmed("/sys/class/dmi/id/" + field);
    if (value.empty() || value == "None" || value == "To be filled by O.E.M.") {
        // 部分主板用这些占位字符串，视为未知
        return {};
    }
    return value;
}

/**
 * @brief Linux 系统信息提供者
 */
class LinuxSystemInfo : public ISystemInfoProvider
{
public:
    LinuxSystemInfo() = default;

    SystemStats sample() override
    {
        SystemStats stats;
        stats.cpuPercent = sampleCpu();
        sampleMemory(stats);
        stats.uptimeSeconds = readUptime();
        return stats;
    }

    std::string osName() const override
    {
        // /etc/os-release 的 PRETTY_NAME（如 "Ubuntu 22.04.2 LTS"）
        std::ifstream file("/etc/os-release");
        if (!file.is_open()) {
            return "Linux";
        }
        std::string line;
        while (std::getline(file, line)) {
            if (line.rfind("PRETTY_NAME=", 0) == 0) {
                std::string value = line.substr(12);
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                return value.empty() ? "Linux" : value;
            }
        }
        return "Linux";
    }

    std::string hardwareModel() const override
    {
        std::string model = readDmi("product_name");
        if (model.empty()) {
            model = readDmi("board_name");
        }
        return model;
    }

    std::string machineVersion() const override
    {
        return readDmi("product_version");
    }

    std::string manufacturer() const override
    {
        std::string vendor = readDmi("sys_vendor");
        if (vendor.empty()) {
            vendor = readDmi("board_vendor");
        }
        return vendor;
    }

    std::string serialNumber() const override
    {
        return readDmi("product_serial");
    }

private:
    uint64_t lastTotal_ = 0;   ///< 上次 CPU 总时间
    uint64_t lastIdle_ = 0;    ///< 上次 CPU 空闲时间

    /**
     * @brief 从 /proc/stat 采样 CPU 使用率（相邻两次差值）
     * @return 使用率百分比（首次调用返回 0）
     */
    double sampleCpu()
    {
        std::ifstream file("/proc/stat");
        if (!file.is_open()) {
            return 0.0;
        }
        std::string label;
        uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        file >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        if (label != "cpu") {
            return 0.0;
        }

        const uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
        const uint64_t idleAll = idle + iowait;

        if (lastTotal_ == 0) {
            lastTotal_ = total;
            lastIdle_ = idleAll;
            return 0.0;
        }

        const uint64_t totalDelta = total - lastTotal_;
        const uint64_t idleDelta = idleAll - lastIdle_;
        lastTotal_ = total;
        lastIdle_ = idleAll;

        if (totalDelta == 0) {
            return 0.0;
        }
        const double busy = static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta) * 100.0;
        return busy < 0.0 ? 0.0 : (busy > 100.0 ? 100.0 : busy);
    }

    /** @brief 从 /proc/meminfo 采样内存（优先 MemAvailable） */
    void sampleMemory(SystemStats& stats)
    {
        std::ifstream file("/proc/meminfo");
        if (!file.is_open()) {
            return;
        }
        uint64_t total = 0, available = 0;
        std::string key;
        uint64_t value = 0;
        std::string unit;
        while (file >> key >> value >> unit) {
            if (key == "MemTotal:") {
                total = value;
            } else if (key == "MemAvailable:") {
                available = value;
            }
            if (total > 0 && available > 0) {
                break;
            }
        }
        stats.memoryTotalKb = total;
        if (total > 0) {
            const uint64_t used = total > available ? total - available : 0;
            stats.memoryUsedKb = used;
            stats.memoryPercent = static_cast<double>(used) / static_cast<double>(total) * 100.0;
        }
    }

    /** @brief 从 /proc/uptime 读取运行秒数 */
    uint64_t readUptime()
    {
        std::ifstream file("/proc/uptime");
        if (!file.is_open()) {
            return 0;
        }
        double seconds = 0.0;
        file >> seconds;
        return static_cast<uint64_t>(seconds);
    }
};

} // namespace

std::unique_ptr<ISystemInfoProvider> createSystemInfoProvider()
{
    return std::make_unique<LinuxSystemInfo>();
}
