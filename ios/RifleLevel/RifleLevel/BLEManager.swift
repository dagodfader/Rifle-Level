import Foundation
import CoreBluetooth
import Combine

struct BLEDevice: Identifiable {
    let id: UUID
    let name: String
    let rssi: Int
    let peripheral: CBPeripheral
}

struct RifleLevelData {
    var version: Int = 1
    var cant: Double = 0.0
    var offset: Double = 0.0
    var pitch: Double = 0.0
    var cosine: Double = 1.000
    var stability: Int = 0
    var battery: Double = 0.0
}

final class BLEManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var bluetoothStatus: String = "Starting Bluetooth..."
    @Published var devices: [BLEDevice] = []
    @Published var connectedDeviceName: String = "Not connected"

    @Published var rawData: String = "No data yet"
    @Published var levelData = RifleLevelData()

    @Published var lastPacketDate: Date?
    @Published var packetsReceived: Int = 0

    let targetDeviceName = "RifleLevel"

    private var centralManager: CBCentralManager!
    private var connectedPeripheral: CBPeripheral?
    private var isConnectingToTarget = false

    private var receiveBuffer: String = ""
    private var rawPacketLines: [String] = []

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: .main)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            bluetoothStatus = "Bluetooth on. Looking for \(targetDeviceName)..."
            startScan()

        case .poweredOff:
            bluetoothStatus = "Bluetooth is off"

        case .unauthorized:
            bluetoothStatus = "Bluetooth permission denied"

        case .unsupported:
            bluetoothStatus = "Bluetooth not supported"

        case .resetting:
            bluetoothStatus = "Bluetooth resetting..."

        case .unknown:
            bluetoothStatus = "Bluetooth state unknown"

        @unknown default:
            bluetoothStatus = "Unknown Bluetooth error"
        }
    }

    func startScan() {
        guard centralManager.state == .poweredOn else {
            bluetoothStatus = "Bluetooth not ready"
            return
        }

        devices.removeAll()

        if connectedDeviceName == "Not connected" {
            bluetoothStatus = "Looking for \(targetDeviceName)..."
        } else {
            bluetoothStatus = "Scanning..."
        }

        centralManager.scanForPeripherals(
            withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
    }

    func stopScan() {
        centralManager.stopScan()
        bluetoothStatus = "Scan stopped"
    }

    func connect(to device: BLEDevice) {
        centralManager.stopScan()

        bluetoothStatus = "Connecting to \(device.name)..."
        isConnectingToTarget = true

        connectedPeripheral = device.peripheral
        connectedPeripheral?.delegate = self

        centralManager.connect(device.peripheral, options: nil)
    }

    func disconnect() {
        if let peripheral = connectedPeripheral {
            centralManager.cancelPeripheralConnection(peripheral)
        } else {
            connectedDeviceName = "Not connected"
            isConnectingToTarget = false
            startScan()
        }
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let advertisedName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let deviceName = peripheral.name ?? advertisedName ?? "Unnamed BLE Device"

        let newDevice = BLEDevice(
            id: peripheral.identifier,
            name: deviceName,
            rssi: RSSI.intValue,
            peripheral: peripheral
        )

        if let index = devices.firstIndex(where: { $0.id == newDevice.id }) {
            devices[index] = newDevice
        } else {
            devices.append(newDevice)
        }

        devices.sort { $0.rssi > $1.rssi }

        if deviceName == targetDeviceName &&
            connectedPeripheral == nil &&
            connectedDeviceName == "Not connected" &&
            !isConnectingToTarget {

            connect(to: newDevice)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        isConnectingToTarget = false
        connectedDeviceName = peripheral.name ?? targetDeviceName
        bluetoothStatus = "Connected. Looking for services..."

        receiveBuffer = ""
        rawPacketLines.removeAll()
        rawData = "No data yet"
        lastPacketDate = nil
        packetsReceived = 0

        peripheral.delegate = self
        peripheral.discoverServices(nil)
    }

    func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: Error?
    ) {
        connectedPeripheral = nil
        connectedDeviceName = "Not connected"
        isConnectingToTarget = false
        bluetoothStatus = "Failed to connect. Scanning again..."

        receiveBuffer = ""
        rawPacketLines.removeAll()
        lastPacketDate = nil

        startScan()
    }

    func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        error: Error?
    ) {
        connectedPeripheral = nil
        connectedDeviceName = "Not connected"
        isConnectingToTarget = false
        bluetoothStatus = "Disconnected. Looking for \(targetDeviceName)..."

        rawData = "No data yet"
        receiveBuffer = ""
        rawPacketLines.removeAll()
        lastPacketDate = nil

        startScan()
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            bluetoothStatus = "Service scan error: \(error.localizedDescription)"
            return
        }

        guard let services = peripheral.services else {
            bluetoothStatus = "No services found"
            return
        }

        bluetoothStatus = "Found \(services.count) service(s). Looking for data..."

        for service in services {
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        if let error = error {
            bluetoothStatus = "Characteristic scan error: \(error.localizedDescription)"
            return
        }

        guard let characteristics = service.characteristics else {
            return
        }

        for characteristic in characteristics {
            if characteristic.properties.contains(.notify) {
                peripheral.setNotifyValue(true, for: characteristic)
                bluetoothStatus = "Subscribed to live data"
            }

            if characteristic.properties.contains(.read) {
                peripheral.readValue(for: characteristic)
            }
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error = error {
            bluetoothStatus = "Data read error: \(error.localizedDescription)"
            return
        }

        guard let data = characteristic.value else {
            return
        }

        guard let text = String(data: data, encoding: .utf8) else {
            rawData = "Received non-text BLE data"
            return
        }

        receiveBuffer += text

        while let newlineRange = receiveBuffer.range(of: "\n") {
            let packet = String(receiveBuffer[..<newlineRange.lowerBound])
                .replacingOccurrences(of: "\r", with: "")
                .trimmingCharacters(in: .whitespacesAndNewlines)

            receiveBuffer.removeSubrange(receiveBuffer.startIndex...newlineRange.lowerBound)

            if !packet.isEmpty {
                handleCompletePacketLine(packet)
            }
        }

        if rawData == "No data yet" && !receiveBuffer.isEmpty {
            rawData = "Partial: \(receiveBuffer)"
        }

        if receiveBuffer.count > 300 {
            receiveBuffer = ""
            rawData = "Buffer cleared. Waiting for newline packet..."
        }
    }

    private func handleCompletePacketLine(_ packet: String) {
        let lowerPacket = packet.lowercased()

        if lowerPacket.hasPrefix("v:") {
            rawPacketLines.removeAll()
        }

        rawPacketLines.append(packet)

        if rawPacketLines.count > 3 {
            rawPacketLines.removeFirst(rawPacketLines.count - 3)
        }

        rawData = rawPacketLines.joined(separator: "\n")

        parseRifleLevelData(packet)

        lastPacketDate = Date()
        packetsReceived += 1
    }

    private func parseRifleLevelData(_ text: String) {
        let cleaned = text
            .replacingOccurrences(of: "\n", with: "")
            .replacingOccurrences(of: "\r", with: "")
            .trimmingCharacters(in: .whitespacesAndNewlines)

        let parts = cleaned.split(separator: ",")

        var updatedData = levelData

        for part in parts {
            let keyValue = part.split(separator: ":", maxSplits: 1)

            guard keyValue.count == 2 else {
                continue
            }

            let key = keyValue[0]
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .lowercased()

            let value = keyValue[1]
                .trimmingCharacters(in: .whitespacesAndNewlines)

            switch key {
            case "v", "version":
                updatedData.version = Int(value) ?? updatedData.version

            case "c", "cant":
                updatedData.cant = Double(value) ?? updatedData.cant

            case "o", "offset":
                updatedData.offset = Double(value) ?? updatedData.offset

            case "p", "pitch":
                updatedData.pitch = Double(value) ?? updatedData.pitch

            case "x", "cos", "cosine":
                updatedData.cosine = Double(value) ?? updatedData.cosine

            case "s", "stable", "stability":
                if let intValue = Int(value) {
                    updatedData.stability = intValue
                } else if let doubleValue = Double(value) {
                    updatedData.stability = Int(doubleValue)
                }

            case "b", "batt", "battery":
                updatedData.battery = Double(value) ?? updatedData.battery

            default:
                break
            }
        }

        levelData = updatedData
    }
}
