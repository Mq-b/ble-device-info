/**
 * @file config.cpp
 * @brief 应用配置实现：路径查找与 JSON 解析
 */
#include "config.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "json.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

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

/**
 * @brief 读取文件全部内容
 * @param path 文件路径
 * @param out 输出内容
 * @return 成功返回 true
 */
bool readFile(const std::string& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    out = oss.str();
    return true;
}

/**
 * @brief 获取可执行文件所在目录
 * @return 目录路径（末尾带路径分隔符）；失败返回空串
 */
std::string executableDir()
{
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    std::wstring path(buf, len);
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return {};
    }
    path.resize(pos + 1);
    // 宽字符转 UTF-8
    const int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
#else
    char buf[PATH_MAX] = {0};
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return {};
    }
    buf[len] = '\0';
    std::string path(buf);
    const size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return {};
    }
    path.resize(pos + 1);
    return path;
#endif
}

/**
 * @brief 获取用户配置目录
 * @return 目录路径（末尾带路径分隔符）；不可用时返回空串
 */
std::string userConfigDir()
{
#if defined(_WIN32)
    // 环境变量 APPDATA（如 C:\Users\xxx\AppData\Roaming）
    const char* appdata = std::getenv("APPDATA");
    if (appdata == nullptr || *appdata == '\0') {
        return {};
    }
    return std::string(appdata) + "\\ble-device-info\\";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/ble-device-info/";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::string(home) + "/.config/ble-device-info/";
    }
    return {};
#endif
}

/**
 * @brief 判断文件是否存在
 * @param path 文件路径
 * @return 存在返回 true
 */
bool fileExists(const std::string& path)
{
    std::ifstream file(path);
    return file.is_open();
}

/**
 * @brief 从 JSON 对象读取字符串字段（缺省返回空串）
 */
std::string getString(const json::Value& obj, const std::string& key)
{
    const json::Value* v = obj.find(key);
    return (v != nullptr && v->isString()) ? v->asString() : std::string();
}

/**
 * @brief 从 JSON 对象读取布尔字段（缺省返回默认值）
 */
bool getBool(const json::Value& obj, const std::string& key, bool def)
{
    const json::Value* v = obj.find(key);
    return (v != nullptr && v->isBool()) ? v->asBool() : def;
}

/**
 * @brief 从 JSON 对象读取整数字段（缺省返回默认值）
 */
int getInt(const json::Value& obj, const std::string& key, int def)
{
    const json::Value* v = obj.find(key);
    return (v != nullptr && v->isNumber()) ? static_cast<int>(v->asInt64(def)) : def;
}

} // namespace

std::string findConfigPath(const std::string& explicitPath)
{
    if (!explicitPath.empty()) {
        return fileExists(explicitPath) ? explicitPath : std::string();
    }

    // 1. 程序同目录
    const std::string exeDir = executableDir();
    if (!exeDir.empty()) {
        const std::string local = exeDir + "config.json";
        if (fileExists(local)) {
            return local;
        }
    }

    // 2. 用户配置目录
    const std::string userDir = userConfigDir();
    if (!userDir.empty()) {
        const std::string user = userDir + "config.json";
        if (fileExists(user)) {
            return user;
        }
    }

    // 3. 系统目录（仅 Linux）
#if !defined(_WIN32)
    const std::string system = "/etc/ble-device-info/config.json";
    if (fileExists(system)) {
        return system;
    }
#endif

    return {};
}

AppConfig loadConfig(const std::string& explicitPath)
{
    AppConfig config;
    const std::string path = findConfigPath(explicitPath);
    if (path.empty()) {
        logInfo("未找到配置文件，使用默认配置（可放置 config.json 于程序同目录）");
        return config;
    }

    std::string text;
    if (!readFile(path, text)) {
        logWarn("读取配置文件失败: " + path);
        return config;
    }

    std::string error;
    const json::Value root = json::parse(text, &error);
    if (!root.isObject()) {
        logWarn("配置文件解析失败: " + path + " - " + error);
        return config;
    }

    const json::Value* identity = root.find("identity");
    if (identity != nullptr && identity->isObject()) {
        config.title = getString(*identity, "title");
        config.manufacturer = getString(*identity, "manufacturer");
        config.machineVersion = getString(*identity, "machineVersion");
        config.serial = getString(*identity, "serial");
    }

    const json::Value* ble = root.find("ble");
    if (ble != nullptr && ble->isObject()) {
        config.broadcastName = getString(*ble, "broadcastName");
        config.advertiseServiceUuid = getBool(*ble, "advertiseServiceUuid", true);
        const std::string svc = getString(*ble, "serviceUuid");
        const std::string info = getString(*ble, "infoCharUuid");
        const std::string status = getString(*ble, "statusCharUuid");
        if (!svc.empty()) {
            config.serviceUuid = svc;
        }
        if (!info.empty()) {
            config.infoCharUuid = info;
        }
        if (!status.empty()) {
            config.statusCharUuid = status;
        }
    }

    const json::Value* sysinfo = root.find("sysinfo");
    if (sysinfo != nullptr && sysinfo->isObject()) {
        config.cpuSampleMs = getInt(*sysinfo, "cpuSampleMs", config.cpuSampleMs);
        config.statusIntervalMs = getInt(*sysinfo, "statusIntervalMs", config.statusIntervalMs);
    }

    logInfo("已加载配置文件: " + path);
    return config;
}
