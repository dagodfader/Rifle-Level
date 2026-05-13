/*
  Rifle Level iOS App
  File: ContentView.swift
  Version: v0.3.0

  Changes in v0.3.0:
  - Added pitch movement to the reticle marker.
  - Marker X position = cant.
  - Marker Y position = pitch.
  - Reticle angle text now shows cant and pitch.
  - Reticle angle, L / 0 / R, and LEVEL / LEFT / RIGHT text use matching font size.
  - Reticle labels moved closer to the outer circle.
  - Debug toggle, diagnostics, battery status, and compact header retained.

  Notes:
  - BLEManager.swift does not need to change for this update.
  - If pitch moves the wrong direction, change displayPitchSign from 1.0 to -1.0.
*/

import SwiftUI
import Combine

struct ContentView: View {
    @StateObject private var bleManager = BLEManager()

    @State private var showAllDevices = false
    @State private var showDebug = false
    @State private var now = Date()

    private let displayCantSign: Double = 1.0
    private let displayPitchSign: Double = 1.0

    private let levelTolerance: Double = 0.6
    private let maxDisplayCant: Double = 10.0
    private let maxDisplayPitch: Double = 20.0
    private let reticleLabelSize: CGFloat = 18

    private let batteryGood: Double = 3.75
    private let batteryOk: Double = 3.50
    private let batteryLow: Double = 3.30

    private let refreshTimer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            VStack(spacing: 8) {
                compactHeader

                if bleManager.connectedDeviceName == "Not connected" {
                    scannerSection
                } else {
                    liveLevelSection
                }

                Spacer(minLength: 0)
            }
            .padding(.horizontal)
            .padding(.top, 8)
        }
        .preferredColorScheme(.dark)
        .onReceive(refreshTimer) { value in
            now = value
        }
    }

    // MARK: - Compact Header

    private var compactHeader: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(connectionColor)
                .frame(width: 9, height: 9)

            VStack(alignment: .leading, spacing: 1) {
                Text("Rifle Level")
                    .font(.headline)
                    .fontWeight(.bold)
                    .foregroundStyle(.white)

                Text(compactStatusText)
                    .font(.caption2)
                    .foregroundStyle(.gray)
                    .lineLimit(1)
            }

            Spacer()

            HStack(spacing: 6) {
                Button("Scan") {
                    bleManager.startScan()
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.mini)

                Button("Disc") {
                    bleManager.disconnect()
                }
                .buttonStyle(.bordered)
                .controlSize(.mini)
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 7)
        .background(Color.white.opacity(0.06))
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private var compactStatusText: String {
        if bleManager.connectedDeviceName == "Not connected" {
            return bleManager.bluetoothStatus
        }

        return dataUpdatingText
    }

    private var connectionColor: Color {
        if bleManager.connectedDeviceName == "Not connected" {
            return .red
        }

        return isDataUpdating ? .green : .yellow
    }

    // MARK: - Scanner

    private var scannerSection: some View {
        VStack(spacing: 10) {
            VStack(spacing: 4) {
                Text("Looking for RifleLevel")
                    .font(.title3)
                    .fontWeight(.bold)
                    .foregroundStyle(.white)

                Text("The app will connect automatically when it sees your level.")
                    .font(.caption)
                    .foregroundStyle(.gray)
                    .multilineTextAlignment(.center)

                Button(showAllDevices ? "Hide Other Devices" : "Show All Devices") {
                    showAllDevices.toggle()
                }
                .font(.caption)
                .buttonStyle(.bordered)
                .controlSize(.small)
                .padding(.top, 4)
            }
            .padding(.top, 8)

            if scannerDevices.isEmpty {
                VStack(spacing: 8) {
                    ProgressView()

                    Text(showAllDevices ? "No BLE devices found yet" : "RifleLevel not found yet")
                        .font(.caption)
                        .foregroundStyle(.gray)
                }
                .padding(.top, 30)
            } else {
                List(scannerDevices) { device in
                    Button {
                        bleManager.connect(to: device)
                    } label: {
                        HStack {
                            VStack(alignment: .leading, spacing: 3) {
                                Text(device.name)
                                    .font(.headline)

                                Text(device.id.uuidString)
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }

                            Spacer()

                            Text("\(device.rssi) dBm")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        .padding(.vertical, 3)
                    }
                }
                .scrollContentBackground(.hidden)
                .background(Color.black)
                .listStyle(.plain)
            }
        }
    }

    private var scannerDevices: [BLEDevice] {
        if showAllDevices {
            return bleManager.devices
        } else {
            return bleManager.devices.filter { $0.name == bleManager.targetDeviceName }
        }
    }

    // MARK: - Live Level Screen

    private var liveLevelSection: some View {
        ScrollView {
            VStack(spacing: 14) {
                reticleCantView
                    .frame(width: 285, height: 285)

                mainStatusCard

                if showDebug {
                    diagnosticsCard
                    rawDataSection
                }
            }
        }
    }

    // MARK: - Reticle Graphic

    private var reticleCantView: some View {
        GeometryReader { geometry in
            let size = min(geometry.size.width, geometry.size.height)
            let center = CGPoint(x: geometry.size.width / 2, y: geometry.size.height / 2)

            let outerRadius = size * 0.48
            let travelRadius = size * 0.36

            let clampedCant = min(max(displayCant, -maxDisplayCant), maxDisplayCant)
            let normalizedCant = clampedCant / maxDisplayCant
            let markerX = center.x + CGFloat(normalizedCant) * travelRadius

            let clampedPitch = min(max(displayPitch, -maxDisplayPitch), maxDisplayPitch)
            let normalizedPitch = clampedPitch / maxDisplayPitch

            // Positive pitch moves marker upward.
            let markerY = center.y - CGFloat(normalizedPitch) * travelRadius

            let labelFont = Font.system(size: reticleLabelSize, weight: .bold)

            ZStack {
                Circle()
                    .stroke(Color.gray.opacity(0.35), lineWidth: 3)

                Circle()
                    .stroke(Color.gray.opacity(0.18), lineWidth: 1)
                    .frame(width: size * 0.68, height: size * 0.68)

                // Horizontal cant reference line
                Rectangle()
                    .fill(Color.gray.opacity(0.45))
                    .frame(width: size * 0.78, height: 2)
                    .position(center)

                // Vertical pitch reference line
                Rectangle()
                    .fill(Color.gray.opacity(0.35))
                    .frame(width: 2, height: size * 0.78)
                    .position(center)

                // Center level window
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color.green.opacity(0.18))
                    .frame(width: size * 0.12, height: 18)
                    .position(center)

                // Center zero mark
                Circle()
                    .stroke(Color.green.opacity(0.75), lineWidth: 2)
                    .frame(width: 18, height: 18)
                    .position(center)

                // Moving marker:
                // X = cant
                // Y = pitch
                Circle()
                    .fill(levelStatusColor)
                    .frame(width: 34, height: 34)
                    .shadow(color: levelStatusColor.opacity(0.8), radius: 10)
                    .position(x: markerX, y: markerY)

                // Top angle readout, closer to outer circle
                Text(String(format: "C %.2f°   P %.2f°", displayCant, displayPitch))
                    .font(labelFont)
                    .foregroundStyle(.white)
                    .position(x: center.x, y: center.y - outerRadius * 0.76)

                // Left / center / right labels, same font size
                Text("L")
                    .font(labelFont)
                    .foregroundStyle(.red)
                    .position(x: center.x - outerRadius * 0.82, y: center.y + outerRadius * 0.52)

                Text("0")
                    .font(labelFont)
                    .foregroundStyle(.green)
                    .position(x: center.x, y: center.y + outerRadius * 0.52)

                Text("R")
                    .font(labelFont)
                    .foregroundStyle(.blue)
                    .position(x: center.x + outerRadius * 0.82, y: center.y + outerRadius * 0.52)

                // LEVEL / LEFT / RIGHT, same font size and closer to outer circle
                Text(levelStatusText)
                    .font(labelFont)
                    .foregroundStyle(levelStatusColor)
                    .position(x: center.x, y: center.y + outerRadius * 0.76)
            }
        }
    }

    // MARK: - Main Status Card

    private var mainStatusCard: some View {
        VStack(spacing: 10) {
            HStack {
                Text("Live Data")
                    .font(.subheadline)
                    .fontWeight(.semibold)
                    .foregroundStyle(.white)

                Spacer()

                batteryPill

                Button(showDebug ? "Hide Debug" : "Debug") {
                    showDebug.toggle()
                }
                .font(.caption)
                .buttonStyle(.bordered)
                .controlSize(.mini)
            }

            Divider()
                .background(Color.white.opacity(0.25))

            HStack(spacing: 12) {
                compactMetric("Pitch", String(format: "%.2f°", displayPitch))
                compactMetric("Cosine", String(format: "%.3f", bleManager.levelData.cosine))
                compactMetric("Stability", "\(bleManager.levelData.stability)%")
            }
        }
        .padding()
        .background(Color.white.opacity(0.08))
        .clipShape(RoundedRectangle(cornerRadius: 18))
        .overlay(
            RoundedRectangle(cornerRadius: 18)
                .stroke(Color.white.opacity(0.12), lineWidth: 1)
        )
    }

    private func compactMetric(_ title: String, _ value: String) -> some View {
        VStack(spacing: 3) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.gray)

            Text(value)
                .font(.body)
                .fontWeight(.semibold)
                .foregroundStyle(.white)
        }
        .frame(maxWidth: .infinity)
    }

    private var batteryPill: some View {
        Text(batteryDisplayText)
            .font(.caption)
            .fontWeight(.semibold)
            .foregroundStyle(batteryStatusColor)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(batteryStatusColor.opacity(0.15))
            .clipShape(Capsule())
    }

    // MARK: - Diagnostics

    private var diagnosticsCard: some View {
        VStack(spacing: 10) {
            HStack {
                Text("Device Diagnostics")
                    .font(.subheadline)
                    .fontWeight(.semibold)
                    .foregroundStyle(.white)

                Spacer()

                Text(dataUpdatingBadgeText)
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(isDataUpdating ? .green : .yellow)
            }

            Divider()
                .background(Color.white.opacity(0.25))

            dataRow("Device", bleManager.connectedDeviceName)
            dataRow("Data", dataUpdatingText, valueColor: isDataUpdating ? .green : .yellow)
            dataRow("Packets", "\(bleManager.packetsReceived)")
            dataRow("Last Packet", lastPacketText)
            dataRow("Offset", String(format: "%.2f°", bleManager.levelData.offset))
            dataRow("Format", "v\(bleManager.levelData.version)")
            dataRow("Battery", fullBatteryDisplayText, valueColor: batteryStatusColor)
        }
        .padding()
        .background(Color.white.opacity(0.08))
        .clipShape(RoundedRectangle(cornerRadius: 18))
        .overlay(
            RoundedRectangle(cornerRadius: 18)
                .stroke(Color.white.opacity(0.12), lineWidth: 1)
        )
    }

    private func dataRow(_ title: String, _ value: String, valueColor: Color = .white) -> some View {
        HStack {
            Text(title)
                .foregroundStyle(.gray)

            Spacer()

            Text(value)
                .fontWeight(.semibold)
                .foregroundStyle(valueColor)
                .lineLimit(1)
        }
        .font(.body)
    }

    // MARK: - Raw Data

    private var rawDataSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Raw BLE Data")
                .font(.subheadline)
                .fontWeight(.semibold)
                .foregroundStyle(.white)

            ScrollView {
                Text(bleManager.rawData)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.gray)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
                    .padding(10)
            }
            .frame(minHeight: 70, maxHeight: 110)
            .background(Color.white.opacity(0.06))
            .clipShape(RoundedRectangle(cornerRadius: 14))
        }
    }

    // MARK: - Data Updating Logic

    private var isDataUpdating: Bool {
        guard let lastPacketDate = bleManager.lastPacketDate else {
            return false
        }

        return now.timeIntervalSince(lastPacketDate) < 2.0
    }

    private var dataUpdatingText: String {
        if bleManager.lastPacketDate == nil {
            return "Waiting for data"
        }

        return isDataUpdating ? "Data updating" : "Data paused"
    }

    private var dataUpdatingBadgeText: String {
        isDataUpdating ? "LIVE" : "PAUSED"
    }

    private var lastPacketText: String {
        guard let lastPacketDate = bleManager.lastPacketDate else {
            return "--"
        }

        let seconds = max(0, Int(now.timeIntervalSince(lastPacketDate)))
        return "\(seconds)s ago"
    }

    // MARK: - Battery Logic

    private var batteryDisplayText: String {
        if bleManager.levelData.battery <= 0 {
            return "BAT --"
        }

        return String(format: "%.2fV %@", bleManager.levelData.battery, batteryStatusText)
    }

    private var fullBatteryDisplayText: String {
        if bleManager.levelData.battery <= 0 {
            return "--"
        }

        return String(format: "%.2f V  %@", bleManager.levelData.battery, batteryStatusText)
    }

    private var batteryStatusText: String {
        let voltage = bleManager.levelData.battery

        if voltage <= 0 {
            return "WAIT"
        } else if voltage >= batteryGood {
            return "GOOD"
        } else if voltage >= batteryOk {
            return "OK"
        } else if voltage >= batteryLow {
            return "LOW"
        } else {
            return "CRITICAL"
        }
    }

    private var batteryStatusColor: Color {
        let voltage = bleManager.levelData.battery

        if voltage <= 0 {
            return .gray
        } else if voltage >= batteryGood {
            return .green
        } else if voltage >= batteryOk {
            return .yellow
        } else if voltage >= batteryLow {
            return .orange
        } else {
            return .red
        }
    }

    // MARK: - Level Logic

    private var displayCant: Double {
        bleManager.levelData.cant * displayCantSign
    }

    private var displayPitch: Double {
        bleManager.levelData.pitch * displayPitchSign
    }

    private var levelStatusText: String {
        if displayCant < -levelTolerance {
            return "LEFT"
        } else if displayCant > levelTolerance {
            return "RIGHT"
        } else {
            return "LEVEL"
        }
    }

    private var levelStatusColor: Color {
        if displayCant < -levelTolerance {
            return .red
        } else if displayCant > levelTolerance {
            return .blue
        } else {
            return .green
        }
    }
}

#Preview {
    ContentView()
}
