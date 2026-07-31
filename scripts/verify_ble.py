#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
验证脚本：作为 BLE 客户端连接 ble-device-info 外设，读取并校验特征数据。

用法:
    python verify_ble.py [--name <广播名>] [--service-uuid <服务UUID>] [--timeout 15]

依赖:
    pip install bleak

说明:
    - 支持按广播名或服务 UUID 过滤扫描结果
    - 自动读取 info / status 两个特征（含长特征分段读取）
    - 输出校验结果汇总，全部通过退出码为 0
"""
import argparse
import asyncio
import json
import sys

import bleak

# 默认 UUID（与 config.example.json 一致）
DEFAULT_SERVICE_UUID = "7e57d001-3f9a-4a6b-9f6c-000000000001"
DEFAULT_INFO_UUID = "7e57d002-3f9a-4a6b-9f6c-000000000002"
DEFAULT_STATUS_UUID = "7e57d003-3f9a-4a6b-9f6c-000000000003"

# 校验结果记录
checks = []


def check(name: str, ok: bool, detail: str = "") -> None:
    """记录一条校验结果"""
    checks.append((name, ok, detail))
    status = "通过" if ok else "失败"
    print(f"  [{status}] {name}" + (f" - {detail}" if detail else ""))


def summarize() -> int:
    """输出汇总并返回退出码"""
    passed = sum(1 for _, ok, _ in checks if ok)
    total = len(checks)
    print(f"\n校验汇总: {passed}/{total} 通过")
    for name, ok, detail in checks:
        if not ok:
            print(f"  ✗ {name}: {detail}")
    return 0 if passed == total else 1


async def read_char(client: bleak.BleakClient, uuid: str) -> bytes:
    """读取特征（bleak 自动处理长特征分段）"""
    return await client.read_gatt_char(uuid)


async def run(args: argparse.Namespace) -> int:
    print("正在扫描 BLE 设备...")

    devices = await bleak.BleakScanner.discover(timeout=args.timeout, return_adv=True)

    target = None
    target_name = ""
    for device, adv in devices.values():
        name = (device.name or "").strip()
        uuids = [str(u).lower() for u in adv.service_uuids or []]

        if args.name and name == args.name:
            target, target_name = device, name
            break
        if args.service_uuid and args.service_uuid.lower() in uuids:
            target, target_name = device, name
            break
        # 兜底：无过滤条件时选第一个可连接设备
        if not args.name and not args.service_uuid and name:
            target, target_name = device, name
            break

    if target is None:
        print("未找到目标设备，请确认外设正在广播")
        check("扫描发现设备", False, "无匹配设备")
        return summarize()

    print(f"发现设备: {target_name} ({target.address})")
    check("扫描发现设备", True, f"{target_name} {target.address}")

    try:
        async with bleak.BleakClient(target.address, timeout=args.timeout) as client:
            print(f"已连接: {target.address}")
            check("建立连接", True)

            # 读取静态信息
            raw_info = await read_char(client, args.info_uuid)
            info = json.loads(raw_info.decode("utf-8"))
            print(f"设备信息: {json.dumps(info, ensure_ascii=False, indent=2)}")

            for key in ("title", "manufacturer", "machineVersion", "serial", "os", "hardware"):
                check(f"info.{key}", key in info and isinstance(info[key], str), str(info.get(key, "")))

            # 读取实时状态
            raw_status = await read_char(client, args.status_uuid)
            status = json.loads(raw_status.decode("utf-8"))
            print(f"实时状态: {json.dumps(status, ensure_ascii=False, indent=2)}")

            check("status.cpu", "cpu" in status and isinstance(status["cpu"], (int, float)), f"cpu={status.get('cpu')}")
            check("status.memory", "memory" in status and isinstance(status["memory"], (int, float)), f"memory={status.get('memory')}")
            check("status.memoryTotal", "memoryTotal" in status and status["memoryTotal"] > 0, f"total={status.get('memoryTotal')}KB")
            check("status.uptime", "uptime" in status and status["uptime"] >= 0, f"uptime={status.get('uptime')}s")
            check("status.timestamp", "timestamp" in status and status["timestamp"] > 0, f"ts={status.get('timestamp')}")

    except asyncio.TimeoutError:
        check("建立连接", False, "连接超时")
    except Exception as exc:  # noqa: BLE001
        check("读取特征", False, str(exc))

    return summarize()


def main() -> int:
    parser = argparse.ArgumentParser(description="ble-device-info 验证脚本")
    parser.add_argument("--name", default="", help="按广播名过滤")
    parser.add_argument("--service-uuid", default=DEFAULT_SERVICE_UUID, help="按服务 UUID 过滤")
    parser.add_argument("--info-uuid", default=DEFAULT_INFO_UUID, help="info 特征 UUID")
    parser.add_argument("--status-uuid", default=DEFAULT_STATUS_UUID, help="status 特征 UUID")
    parser.add_argument("--timeout", type=int, default=15, help="扫描/连接超时秒数")
    args = parser.parse_args()

    return asyncio.run(run(args))


if __name__ == "__main__":
    sys.exit(main())
