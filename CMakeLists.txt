# CMakeLists.txt
#
# 项目：ble-device-info
# 功能：跨平台 BLE 外设，向手机暴露设备状态信息（CPU/内存/硬件等）
# 平台：Linux（BlueZ D-Bus + sdbus-c++）/ Windows（C++/WinRT GattServiceProvider）
cmake_minimum_required(VERSION 3.16)
project(ble-device-info VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(ble-device-info
    src/main.cpp
    src/json.cpp
    src/config.cpp
    src/info_source.cpp
)

if(WIN32)
    # ---- Windows 平台：系统信息 + C++/WinRT BLE ----
    target_sources(ble-device-info PRIVATE
        src/sysinfo_windows.cpp
        src/ble_windows.cpp
    )
    target_compile_definitions(ble-device-info PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    # 查找 Windows SDK 的 C++/WinRT 头文件目录（cppwinrt）
    # 优先使用 VS 生成器提供的 SDK 版本，其次扫描常见安装位置
    set(_CPPWINRT_CANDIDATES "")
    if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        foreach(_KIT_ROOT
                "C:/Program Files (x86)/Windows Kits"
                "C:/Program Files/Windows Kits"
                "D:/Windows Kits")
            list(APPEND _CPPWINRT_CANDIDATES
                 "${_KIT_ROOT}/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt")
        endforeach()
    endif()
    file(GLOB CPPWINRT_INCLUDE_DIRS
        ${_CPPWINRT_CANDIDATES}
        "C:/Program Files (x86)/Windows Kits/10/Include/*/cppwinrt"
        "C:/Program Files/Windows Kits/10/Include/*/cppwinrt"
        "D:/Windows Kits/10/Include/*/cppwinrt"
        "E:/Windows Kits/10/Include/*/cppwinrt"
    )
    list(SORT CPPWINRT_INCLUDE_DIRS)
    if(CPPWINRT_INCLUDE_DIRS)
        list(GET CPPWINRT_INCLUDE_DIRS -1 CPPWINRT_INCLUDE_DIR)
        target_include_directories(ble-device-info PRIVATE "${CPPWINRT_INCLUDE_DIR}")
        message(STATUS "C++/WinRT 头文件: ${CPPWINRT_INCLUDE_DIR}")
    else()
        message(WARNING "未找到 C++/WinRT 头文件，请安装 Windows 10 SDK（含 cppwinrt）")
    endif()

    # windowsapp.lib 等由源码中 pragma comment 链接，无需在此列出
else()
    # ---- Linux 平台：系统信息 + BlueZ D-Bus BLE ----
    target_sources(ble-device-info PRIVATE
        src/sysinfo_linux.cpp
        src/ble_linux.cpp
    )
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDBUSCPP REQUIRED IMPORTED_TARGET sdbus-c++)
    target_link_libraries(ble-device-info PRIVATE PkgConfig::SDBUSCPP)
    target_compile_options(ble-device-info PRIVATE -Wall -Wextra)
endif()
