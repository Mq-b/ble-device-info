/**
 * Android 端 BLE 客户端：扫描并连接 ble-device-info 外设，读取设备信息。
 *
 * 技术栈：Expo SDK 55 + React Native 0.83 + TypeScript + react-native-ble-plx
 * 说明：BLE 为原生模块，需 development build（Expo Go 不支持），构建见 README.md。
 */
import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ActivityIndicator,
  Button,
  FlatList,
  Platform,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import BleManager, { Device } from 'react-native-ble-plx';

// 与 config.example.json 保持一致
const SERVICE_UUID = '7e57d001-3f9a-4a6b-9f6c-000000000001';
const INFO_CHAR_UUID = '7e57d002-3f9a-4a6b-9f6c-000000000002';
const STATUS_CHAR_UUID = '7e57d003-3f9a-4a6b-9f6c-000000000003';

const manager = new BleManager();

interface ScannedDevice {
  id: string;
  name: string;
}

interface InfoData {
  title?: string;
  manufacturer?: string;
  machineVersion?: string;
  serial?: string;
  os?: string;
  hardware?: string;
}

interface StatusData {
  cpu?: number;
  memory?: number;
  memoryTotal?: number;
  memoryUsed?: number;
  uptime?: number;
  intervalMs?: number;
  timestamp?: number;
}

/**
 * 连接外设并读取 info / status 特征（Android 系统自动处理长特征分段读取）。
 */
async function readAll(device: Device): Promise<{ info: InfoData; status: StatusData }> {
  const infoRaw = await device.readCharacteristic(INFO_CHAR_UUID);
  const statusRaw = await device.readCharacteristic(STATUS_CHAR_UUID);
  return {
    info: JSON.parse(Buffer.from(infoRaw.value ?? '', 'base64').toString('utf-8')),
    status: JSON.parse(Buffer.from(statusRaw.value ?? '', 'base64').toString('utf-8')),
  };
}

/**
 * 主界面：扫描 → 连接 → 展示设备信息与实时状态。
 */
export default function App() {
  const [devices, setDevices] = useState<ScannedDevice[]>([]);
  const [scanning, setScanning] = useState(false);
  const [connectedId, setConnectedId] = useState<string | null>(null);
  const [info, setInfo] = useState<InfoData | null>(null);
  const [status, setStatus] = useState<StatusData | null>(null);
  const [busy, setBusy] = useState(false);
  const [log, setLog] = useState<string>('');
  const deviceRef = useRef<Device | null>(null);

  const appendLog = useCallback((msg: string) => {
    setLog((prev) => `${new Date().toLocaleTimeString()} ${msg}\n${prev}`);
  }, []);

  /** 申请权限（Android 12+ 扫描/连接权限；Android 13+ 为"附近设备"权限组） */
  const ensurePermissions = useCallback(async (): Promise<boolean> => {
    if (Platform.OS !== 'android') {
      return true;
    }
    const granted = await manager.requestPermissions([
      'android.permission.BLUETOOTH_SCAN',
      'android.permission.BLUETOOTH_CONNECT',
    ]);
    const ok = granted === BleManager.State.PoweredOn || granted?.length > 0;
    if (!ok) {
      appendLog('未获得蓝牙权限，请在系统设置中授予');
      return false;
    }
    return true;
  }, [appendLog]);

  /** 扫描：按服务 UUID 过滤 */
  const startScan = useCallback(async () => {
    if (!(await ensurePermissions())) {
      return;
    }
    setDevices([]);
    setScanning(true);
    appendLog('开始扫描...');
    manager.startDeviceScan(
      null,
      { serviceUUIDs: [SERVICE_UUID], allowDuplicates: false },
      (error, device) => {
        if (error) {
          appendLog(`扫描错误: ${error.message}`);
          setScanning(false);
          return;
        }
        if (!device) {
          return;
        }
        const name = device.name ?? device.localName ?? '未知设备';
        setDevices((prev) => {
          if (prev.some((d) => d.id === device.id)) {
            return prev;
          }
          return [...prev, { id: device.id, name }];
        });
      },
    );
  }, [appendLog, ensurePermissions]);

  const stopScan = useCallback(() => {
    manager.stopDeviceScan();
    setScanning(false);
    appendLog('扫描停止');
  }, [appendLog]);

  /** 连接设备并读取信息 */
  const connectTo = useCallback(
    async (deviceId: string) => {
      stopScan();
      setBusy(true);
      appendLog(`正在连接 ${deviceId} ...`);
      try {
        await manager.connectToDevice(deviceId, { timeout: 10000 });
        const device = await manager.discoverAllServicesAndCharacteristicsForDevice(deviceId);
        deviceRef.current = device;
        setConnectedId(deviceId);
        appendLog('已连接，正在读取数据...');

        const data = await readAll(device);
        setInfo(data.info);
        setStatus(data.status);
        appendLog(`读取成功: ${data.info.title ?? ''} ${data.info.serial ?? ''}`);
      } catch (e) {
        appendLog(`连接失败: ${e instanceof Error ? e.message : String(e)}`);
      } finally {
        setBusy(false);
      }
    },
    [appendLog, stopScan],
  );

  /** 刷新实时状态 */
  const refreshStatus = useCallback(async () => {
    const device = deviceRef.current;
    if (!device) {
      return;
    }
    setBusy(true);
    try {
      const raw = await device.readCharacteristic(STATUS_CHAR_UUID);
      const data: StatusData = JSON.parse(Buffer.from(raw.value ?? '', 'base64').toString('utf-8'));
      setStatus(data);
      appendLog(`状态已刷新: cpu=${data.cpu?.toFixed(1)}% mem=${data.memory?.toFixed(1)}%`);
    } catch (e) {
      appendLog(`刷新失败: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(false);
    }
  }, [appendLog]);

  /** 断开连接 */
  const disconnect = useCallback(async () => {
    const device = deviceRef.current;
    if (device) {
      await manager.cancelDeviceConnection(device.id);
      deviceRef.current = null;
    }
    setConnectedId(null);
    setInfo(null);
    setStatus(null);
    appendLog('已断开');
  }, [appendLog]);

  useEffect(() => {
    return () => {
      manager.stopDeviceScan();
      manager.destroy();
    };
  }, []);

  return (
    <View style={styles.container}>
      <View style={styles.row}>
        <Button title={scanning ? '停止扫描' : '扫描设备'} onPress={scanning ? stopScan : startScan} />
        {connectedId && <Button title="断开" onPress={disconnect} color="#c0392b" />}
      </View>

      {connectedId === null ? (
        <FlatList
          data={devices}
          keyExtractor={(item) => item.id}
          style={styles.list}
          renderItem={({ item }) => (
            <View style={styles.deviceRow}>
              <Text style={styles.deviceName}>{item.name}</Text>
              <Text style={styles.deviceId}>{item.id}</Text>
              <Button title="连接" onPress={() => connectTo(item.id)} disabled={busy} />
            </View>
          )}
          ListEmptyComponent={
            <Text style={styles.hint}>{scanning ? '扫描中，等待发现设备...' : '点击"扫描设备"开始（外设需先运行）'}</Text>
          }
        />
      ) : (
        <ScrollView style={styles.detail}>
          {busy && <ActivityIndicator style={styles.loading} />}
          {info && (
            <View style={styles.card}>
              <Text style={styles.cardTitle}>设备信息</Text>
              <Text style={styles.line}>标题: {info.title}</Text>
              <Text style={styles.line}>厂商: {info.manufacturer}</Text>
              <Text style={styles.line}>版本号: {info.machineVersion}</Text>
              <Text style={styles.line}>序列号: {info.serial}</Text>
              <Text style={styles.line}>系统: {info.os}</Text>
              <Text style={styles.line}>硬件: {info.hardware}</Text>
            </View>
          )}
          {status && (
            <View style={styles.card}>
              <Text style={styles.cardTitle}>实时状态</Text>
              <Text style={styles.line}>CPU: {status.cpu?.toFixed(1)}%</Text>
              <Text style={styles.line}>内存: {status.memory?.toFixed(1)}%（{((status.memoryUsed ?? 0) / 1024 / 1024).toFixed(1)} / {((status.memoryTotal ?? 0) / 1024 / 1024).toFixed(1)} GB）</Text>
              <Text style={styles.line}>运行时间: {Math.floor((status.uptime ?? 0) / 3600)} 小时 {(status.uptime ?? 0) % 3600 / 60 | 0} 分钟</Text>
              <Button title="刷新状态" onPress={refreshStatus} disabled={busy} />
            </View>
          )}
        </ScrollView>
      )}

      <ScrollView style={styles.logBox}>
        <Text style={styles.logText}>{log || '暂无日志'}</Text>
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, paddingTop: 48, paddingHorizontal: 12, backgroundColor: '#f5f6fa' },
  row: { flexDirection: 'row', justifyContent: 'space-between', marginBottom: 8 },
  list: { flex: 1 },
  deviceRow: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', backgroundColor: '#fff', borderRadius: 8, padding: 12, marginVertical: 4 },
  deviceName: { fontSize: 16, fontWeight: '600', flex: 1 },
  deviceId: { fontSize: 12, color: '#888', marginRight: 8 },
  hint: { textAlign: 'center', color: '#888', marginTop: 32 },
  detail: { flex: 1 },
  loading: { marginVertical: 8 },
  card: { backgroundColor: '#fff', borderRadius: 8, padding: 12, marginVertical: 6 },
  cardTitle: { fontSize: 16, fontWeight: '700', marginBottom: 6, color: '#2c3e50' },
  line: { fontSize: 14, marginVertical: 2, color: '#333' },
  logBox: { backgroundColor: '#1e1e1e', borderRadius: 8, padding: 8, maxHeight: 120, marginTop: 8 },
  logText: { color: '#7bed9f', fontSize: 11, fontFamily: 'monospace' },
});
