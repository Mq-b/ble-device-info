/**
 * @file main.cpp
 * @brief 程序入口：加载配置、组装数据源、启动平台 BLE 外设
 * @note 用法：ble-device-info [--config <路径>] [--help]
 */
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "ble.h"
#include "config.h"
#include "info_source.h"
#include "sysinfo.h"

#if defined(_WIN32)
#include <windows.h>

namespace {
/** @brief 全局外设指针（供控制台事件回调访问） */
std::atomic<IBlePeripheral*> g_peripheral{nullptr};

/**
 * @brief Windows 控制台事件处理（Ctrl+C / 关闭窗口）
 * @param type 事件类型
 * @return 始终返回 TRUE 表示已处理
 */
BOOL WINAPI consoleHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (IBlePeripheral* p = g_peripheral.load()) {
            p->requestStop();
        }
        return TRUE;
    }
    return FALSE;
}

/** @brief 安装控制台事件处理 */
bool installSignalHandlers()
{
    return SetConsoleCtrlHandler(consoleHandler, TRUE) != 0;
}
} // namespace

#else
#include <csignal>

namespace {
/** @brief 全局外设指针（供信号处理回调访问） */
std::atomic<IBlePeripheral*> g_peripheral{nullptr};

/**
 * @brief POSIX 信号处理（SIGINT/SIGTERM）
 * @param sig 信号编号
 */
void signalHandler(int sig)
{
    (void)sig;
    if (IBlePeripheral* p = g_peripheral.load()) {
        p->requestStop();
    }
}

/** @brief 安装退出信号处理 */
bool installSignalHandlers()
{
    struct sigaction sa {};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    return sigaction(SIGINT, &sa, nullptr) == 0 && sigaction(SIGTERM, &sa, nullptr) == 0;
}
} // namespace
#endif

namespace {

/**
 * @brief 打印用法说明
 * @param prog 程序名
 */
void printUsage(const char* prog)
{
    std::cout << "用法: " << prog << " [--config <配置文件路径>] [--help]" << std::endl;
    std::cout << "配置查找顺序: 命令行 --config → 程序同目录 config.json → 用户配置目录 → 系统目录(/etc/ble-device-info)" << std::endl;
}

} // namespace

/**
 * @brief 程序入口
 * @param argc 参数个数
 * @param argv 参数列表
 * @return 0 正常退出；非 0 失败
 */
int main(int argc, char* argv[])
{
    std::string configPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            configPath = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (!installSignalHandlers()) {
        std::cerr << "[ERROR] 安装退出信号处理失败" << std::endl;
        return 1;
    }

    try {
        // 装配：配置 → 系统信息 → 数据源 → BLE 外设
        const AppConfig config = loadConfig(configPath);
        auto sysinfo = createSystemInfoProvider();
        auto source = std::make_unique<InfoSource>(config, std::move(sysinfo));
        auto peripheral = createBlePeripheral(*source);

        g_peripheral.store(peripheral.get());
        const int rc = peripheral->run();
        g_peripheral.store(nullptr);
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] 启动失败: " << e.what() << std::endl;
        return 1;
    }
}
