/*
  Rifle Level iOS App
  File: BLEManager.swift
  Version: iOS BLE v0.4.3

  Changes in v0.4.3:
  - Format/version field is no longer required from BLE.
  - Fast live packet can now be:
    c:-0.42,p:1.80,x:0.999

  - Slow status packet:
    o:0.10,s:82,b:3.92,cc:1,pc:1

  - Keeps optional v/version parsing if firmware sends it again later.
  - Keeps calibration flags:
    cc = cant calibration saved
    pc = pitch calibration saved

  - Keeps BLE UART RX command writing.
  - Keeps ack reply handling.

  Nordic UART UUIDs:
  - Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
  - RX:      6E400002-B5A3-F393-E0A9-E50E24DCCA9E
             App writes here. Device receives here.
  - TX:      6E400003-B5A3-F393-E0A9-E50E24DCCA9E
             Device notifies here. App receives here.

  Commands sent:
  - ZERO_CANT\n
  - RESET_CANT\n
  - ZERO_PITCH\n
  - RESET_PITCH\n

  Expected firmware replies:
  - ack:ZERO_CANT\n
  - ack:RESET_CANT\n
  - ack:ZERO_PITCH\n
  - ack:RESET_PITCH\n
*/

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
    var cantCalibrated: Bool = false
    var pitchCalibrated: Bool = false
}

final class BLEManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var bluetoothStatus: String = "Starting Bluetooth..."
    @Published var devices: [BLEDevice] = []
    @Published var connectedDeviceName: String = "Not connected"

    @Published var rawData: String = "No data yet"
    @Published var levelData = RifleLevelData()

    @Published var lastPacketDate: Date?
    @Published var packetsReceived: Int = 0

    @Published var lastStatusPacketDate: Date?
    @Published var statusPacketsReceived: Int = 0

    @Published var lastCommandSent: String = "None"
    @Published var lastCommandReply: String = "No reply yet"

    let targetDeviceName = "RifleLevel"

    private let uartServiceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    private let uartRXUUID = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    private let uartTXUUID = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

    private var centralManager: CBCentralManager!
    private var connectedPeripheral: CBPeripheral?

    private var rxCharacteristic: CBCharacteristic?
    private var txCharacteristic: CBCharacteristic?

    private var isConnectingToTarget = false

    private var receiveBuffer: String = ""
    private var lastLivePacketLine: String?
    private var lastStatusPacketLine: String?

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: - Bluetooth State

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

    // MARK: - Scanning / Connection

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
        bluetoothStatus = "Connected. Looking for UART service..."

        receiveBuffer = ""
        lastLivePacketLine = nil
        lastStatusPacketLine = nil
        rawData = "No data yet"

        lastPacketDate = nil
        packetsReceived = 0

        lastStatusPacketDate = nil
        statusPacketsReceived = 0

        rxCharacteristic = nil
        txCharacteristic = nil

        lastCommandSent = "None"
        lastCommandReply = "No reply yet"

        peripheral.delegate = self
        peripheral.discoverServices([uartServiceUUID])
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
        lastLivePacketLine = nil
        lastStatusPacketLine = nil
        rawData = "No data yet"

        lastPacketDate = nil
        packetsReceived = 0

        lastStatusPacketDate = nil
        statusPacketsReceived = 0

        rxCharacteristic = nil
        txCharacteristic = nil

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
        lastLivePacketLine = nil
        lastStatusPacketLine = nil

        lastPacketDate = nil
        packetsReceived = 0

        lastStatusPacketDate = nil
        statusPacketsReceived = 0

        rxCharacteristic = nil
        txCharacteristic = nil

        startScan()
    }

    // MARK: - Services / Characteristics

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            bluetoothStatus = "Service scan error: \(error.localizedDescription)"
            return
        }

        guard let services = peripheral.services, !services.isEmpty else {
            bluetoothStatus = "UART service not found"
            return
        }

        for service in services {
            if service.uuid == uartServiceUUID {
                bluetoothStatus = "UART service found. Looking for RX/TX..."
                peripheral.discoverCharacteristics([uartRXUUID, uartTXUUID], for: service)
            }
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
            bluetoothStatus = "No UART characteristics found"
            return
        }

        for characteristic in characteristics {
            if characteristic.uuid == uartRXUUID {
                rxCharacteristic = characteristic
            }

            if characteristic.uuid == uartTXUUID {
                txCharacteristic = characteristic

                if characteristic.properties.contains(.notify) {
                    peripheral.setNotifyValue(true, for: characteristic)
                }
            }
        }

        if rxCharacteristic != nil && txCharacteristic != nil {
            bluetoothStatus = "Live data and commands ready"
        } else if txCharacteristic != nil {
            bluetoothStatus = "Live data ready. Commands not ready."
        } else if rxCharacteristic != nil {
            bluetoothStatus = "Commands ready. Live data not ready."
        } else {
            bluetoothStatus = "UART RX/TX not found"
        }
    }

    // MARK: - Receiving BLE Data

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error = error {
            bluetoothStatus = "Data read error: \(error.localizedDescription)"
            return
        }

        guard characteristic.uuid == uartTXUUID else {
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

        if lowerPacket.hasPrefix("ack:") {
            lastCommandReply = packet
            return
        }

        let keys = packetKeys(packet)

        let isLivePacket =
            keys.contains("v") ||
            keys.contains("version") ||
            keys.contains("c") ||
            keys.contains("cant") ||
            keys.contains("p") ||
            keys.contains("pitch") ||
            keys.contains("x") ||
            keys.contains("cos") ||
            keys.contains("cosine")

        let isStatusPacket =
            keys.contains("o") ||
            keys.contains("offset") ||
            keys.contains("s") ||
            keys.contains("stable") ||
            keys.contains("stability") ||
            keys.contains("b") ||
            keys.contains("batt") ||
            keys.contains("battery") ||
            keys.contains("cc") ||
            keys.contains("pc")

        parseRifleLevelData(packet)

        if isLivePacket {
            lastLivePacketLine = packet
            lastPacketDate = Date()
            packetsReceived += 1
        }

        if isStatusPacket {
            lastStatusPacketLine = packet
            lastStatusPacketDate = Date()
            statusPacketsReceived += 1
        }

        updateRawDataDisplay()
    }

    private func packetKeys(_ text: String) -> Set<String> {
        let cleaned = text
            .replacingOccurrences(of: "\n", with: "")
            .replacingOccurrences(of: "\r", with: "")
            .trimmingCharacters(in: .whitespacesAndNewlines)

        let parts = cleaned.split(separator: ",")

        var keys = Set<String>()

        for part in parts {
            let keyValue = part.split(separator: ":", maxSplits: 1)

            guard keyValue.count == 2 else {
                continue
            }

            let key = keyValue[0]
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .lowercased()

            keys.insert(key)
        }

        return keys
    }

    private func updateRawDataDisplay() {
        var lines: [String] = []

        if let lastLivePacketLine {
            lines.append(lastLivePacketLine)
        }

        if let lastStatusPacketLine {
            lines.append(lastStatusPacketLine)
        }

        rawData = lines.isEmpty ? "No data yet" : lines.joined(separator: "\n")
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

            case "cc", "cantcal", "cantcalibrated":
                updatedData.cantCalibrated = value == "1" || value.lowercased() == "true"

            case "pc", "pitchcal", "pitchcalibrated":
                updatedData.pitchCalibrated = value == "1" || value.lowercased() == "true"

            default:
                break
            }
        }

        levelData = updatedData
    }

    // MARK: - Sending BLE Commands

    func sendCommand(_ command: String) {
        guard let peripheral = connectedPeripheral else {
            bluetoothStatus = "Command failed: not connected"
            return
        }

        guard let rxCharacteristic = rxCharacteristic else {
            bluetoothStatus = "Command failed: UART RX not found"
            lastCommandSent = command.trimmingCharacters(in: .whitespacesAndNewlines)
            lastCommandReply = "No UART RX characteristic"
            return
        }

        guard let data = command.data(using: .utf8) else {
            bluetoothStatus = "Command failed: invalid text"
            return
        }

        let trimmedCommand = command.trimmingCharacters(in: .whitespacesAndNewlines)

        if rxCharacteristic.properties.contains(.write) {
            peripheral.writeValue(data, for: rxCharacteristic, type: .withResponse)
        } else if rxCharacteristic.properties.contains(.writeWithoutResponse) {
            peripheral.writeValue(data, for: rxCharacteristic, type: .withoutResponse)
        } else {
            bluetoothStatus = "Command failed: RX is not writable"
            lastCommandSent = trimmedCommand
            lastCommandReply = "RX is not writable"
            return
        }

        lastCommandSent = trimmedCommand
        lastCommandReply = "Waiting for reply..."
        bluetoothStatus = "Sent: \(trimmedCommand)"
    }
}
