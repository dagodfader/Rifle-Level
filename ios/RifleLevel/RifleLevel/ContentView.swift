/*
  LevelSense iOS App
  File: ContentView.swift
  Version: iOS UI v0.5.3

  Changes in v0.5.3:
  - Fixed reticle vertical line gap.
  - Vertical center line now returns through most of the lower reticle.
  - Only the portion behind L / 0 / R and LEVEL is removed.
  - Keeps Scan / Disc text wrapping fix.
  - Keeps LevelSense title color styling using HStack instead of Text + Text.
  - Keeps Cant / Pitch / Cosine / Stability on one row.
  - Keeps calibration status on main page.
  - Keeps BLE behavior unchanged.

  Current BLE packet format:
  - Fast live packet every 100 ms:
    c:-0.42,p:1.80,x:0.999

  - Slow status packet every 1000 ms:
    o:0.10,s:82,b:3.92,cc:1,pc:1
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

    private let appAccent = Color(red: 0.48, green: 0.88, blue: 0.25)
    private let cardColor = Color(red: 0.07, green: 0.075, blue: 0.08)
    private let cardBorder = Color.white.opacity(0.12)
    private let softText = Color.white.opacity(0.72)

    private let refreshTimer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            backgroundGradient

            VStack(spacing: 10) {
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

    // MARK: - Background

    private var backgroundGradient: some View {
        LinearGradient(
            colors: [
                Color.black,
                Color(red: 0.015, green: 0.018, blue: 0.02),
                Color.black
            ],
            startPoint: .top,
            endPoint: .bottom
        )
        .ignoresSafeArea()
    }

    // MARK: - Compact Header

    private var compactHeader: some View {
        HStack(spacing: 10) {
            Circle()
                .fill(connectionColor)
                .frame(width: 11, height: 11)
                .shadow(color: connectionColor.opacity(0.8), radius: 6)

            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 0) {
                    Text("Level")
                        .foregroundStyle(.white)

                    Text("Sense")
                        .foregroundStyle(appAccent)
                }
                .font(.title2)
                .fontWeight(.bold)

                Text(compactStatusText)
                    .font(.caption)
                    .foregroundStyle(softText)
                    .lineLimit(1)
            }

            Spacer()

            HStack(spacing: 6) {
                Button {
                    bleManager.startScan()
                } label: {
                    Text("Scan")
                        .font(.caption)
                        .fontWeight(.semibold)
                        .lineLimit(1)
                        .minimumScaleFactor(0.8)
                        .frame(width: 46)
                }
                .buttonStyle(.bordered)
                .controlSize(.mini)
                .tint(appAccent)

                Button {
                    bleManager.disconnect()
                } label: {
                    Text("Disc")
                        .font(.caption)
                        .fontWeight(.semibold)
                        .lineLimit(1)
                        .minimumScaleFactor(0.8)
                        .frame(width: 42)
                }
                .buttonStyle(.bordered)
                .controlSize(.mini)
                .tint(.red)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
        .background(headerBackground)
        .shadow(color: appAccent.opacity(0.08), radius: 18, y: 8)
    }

    private var headerBackground: some View {
        RoundedRectangle(cornerRadius: 22)
            .fill(cardColor.opacity(0.95))
            .overlay(
                RoundedRectangle(cornerRadius: 22)
                    .stroke(cardBorder, lineWidth: 1)
            )
    }

    private var compactStatusText: String {
        if bleManager.connectedDeviceName == "Not connected" {
            return bleManager.bluetoothStatus
        }

        return "\(dataUpdatingText) • 100 ms"
    }

    private var connectionColor: Color {
        if bleManager.connectedDeviceName == "Not connected" {
            return .red
        }

        return isDataUpdating ? appAccent : .yellow
    }

    // MARK: - Scanner

    private var scannerSection: some View {
        VStack(spacing: 14) {
            VStack(spacing: 6) {
                Text("Looking for LevelSense")
                    .font(.title3)
                    .fontWeight(.bold)
                    .foregroundStyle(.white)

                Text("The app will connect automatically when it sees your RifleLevel BLE device.")
                    .font(.caption)
                    .foregroundStyle(softText)
                    .multilineTextAlignment(.center)

                Button(showAllDevices ? "Hide Other Devices" : "Show All Devices") {
                    showAllDevices.toggle()
                }
                .font(.caption)
                .buttonStyle(.bordered)
                .controlSize(.small)
                .tint(appAccent)
                .padding(.top, 4)
            }
            .padding()
            .background(cardBackground())
            .shadow(color: appAccent.opacity(0.06), radius: 18, y: 8)

            if scannerDevices.isEmpty {
                VStack(spacing: 10) {
                    ProgressView()
                        .tint(appAccent)

                    Text(showAllDevices ? "No BLE devices found yet" : "RifleLevel not found yet")
                        .font(.caption)
                        .foregroundStyle(softText)
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
                                    .foregroundStyle(.white)

                                Text(device.id.uuidString)
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }

                            Spacer()

                            Text("\(device.rssi) dBm")
                                .font(.caption)
                                .foregroundStyle(appAccent)
                        }
                        .padding(.vertical, 4)
                    }
                }
                .scrollContentBackground(.hidden)
                .background(Color.clear)
                .listStyle(.plain)
            }
        }
        .padding(.top, 10)
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
            VStack(spacing: 16) {
                reticleCantView
                    .frame(width: 315, height: 315)
                    .padding(.top, 6)

                mainStatusCard

                if showDebug {
                    deviceControlsCard
                    diagnosticsCard
                    rawDataSection
                }
            }
            .padding(.bottom, 18)
        }
        .scrollIndicators(.hidden)
    }

    // MARK: - Reticle Graphic

    private var reticleCantView: some View {
        GeometryReader { geometry in
            let size = min(geometry.size.width, geometry.size.height)
            let center = CGPoint(x: geometry.size.width / 2, y: geometry.size.height / 2)

            let outerRadius = size * 0.47
            let innerRadius = size * 0.40
            let travelRadius = size * 0.34
            let tickCount = 24

            let clampedCant = min(max(displayCant, -maxDisplayCant), maxDisplayCant)
            let normalizedCant = clampedCant / maxDisplayCant
            let markerX = center.x + CGFloat(normalizedCant) * travelRadius

            let clampedPitch = min(max(displayPitch, -maxDisplayPitch), maxDisplayPitch)
            let normalizedPitch = clampedPitch / maxDisplayPitch

            // Positive pitch moves marker upward.
            let markerY = center.y - CGFloat(normalizedPitch) * travelRadius

            // Gap only behind the lower labels:
            // L / 0 / R and LEVEL.
            let labelGapTop = center.y + outerRadius * 0.50
            let labelGapBottom = center.y + outerRadius * 0.84

            let labelFont = Font.system(size: reticleLabelSize, weight: .bold)

            ZStack {
                Circle()
                    .fill(
                        RadialGradient(
                            colors: [
                                appAccent.opacity(0.14),
                                Color.white.opacity(0.025),
                                Color.clear
                            ],
                            center: .center,
                            startRadius: 10,
                            endRadius: outerRadius
                        )
                    )

                Circle()
                    .stroke(Color.white.opacity(0.10), lineWidth: 10)

                Circle()
                    .stroke(appAccent.opacity(0.85), lineWidth: 2.5)
                    .frame(width: innerRadius * 2, height: innerRadius * 2)
                    .shadow(color: appAccent.opacity(0.25), radius: 10)

                Circle()
                    .stroke(Color.white.opacity(0.12), lineWidth: 2)
                    .frame(width: outerRadius * 2, height: outerRadius * 2)

                // Horizontal crosshair
                Path { path in
                    path.move(to: CGPoint(x: center.x - innerRadius, y: center.y))
                    path.addLine(to: CGPoint(x: center.x + innerRadius, y: center.y))
                }
                .stroke(Color.white.opacity(0.48), lineWidth: 1.5)

                // Vertical crosshair, main section.
                // This restores the line through the center and down close to the labels.
                Path { path in
                    path.move(to: CGPoint(x: center.x, y: center.y - innerRadius))
                    path.addLine(to: CGPoint(x: center.x, y: labelGapTop))
                }
                .stroke(Color.white.opacity(0.38), lineWidth: 1.3)

                // Vertical crosshair, lower section below the label gap.
                Path { path in
                    path.move(to: CGPoint(x: center.x, y: labelGapBottom))
                    path.addLine(to: CGPoint(x: center.x, y: center.y + innerRadius))
                }
                .stroke(Color.white.opacity(0.38), lineWidth: 1.3)

                // Tick marks around reticle.
                // Skip the bottom major tick because it runs behind the RIGHT / LEVEL label area.
                ForEach(0..<tickCount, id: \.self) { index in
                    let angle = Double(index) * 2.0 * Double.pi / Double(tickCount)
                    let isMajor = index % 6 == 0

                    // index 6 is the 6 o'clock tick.
                    // It sits behind the lower text labels, so hide it.
                    let hideBottomMajorTick = index == 6

                    if !hideBottomMajorTick {
                        let outer = innerRadius
                        let inner = innerRadius - (isMajor ? 22 : 12)

                        Path { path in
                            let start = CGPoint(
                                x: center.x + CGFloat(cos(angle)) * inner,
                                y: center.y + CGFloat(sin(angle)) * inner
                            )

                            let end = CGPoint(
                                x: center.x + CGFloat(cos(angle)) * outer,
                                y: center.y + CGFloat(sin(angle)) * outer
                            )

                            path.move(to: start)
                            path.addLine(to: end)
                        }
                        .stroke(
                            isMajor ? appAccent.opacity(0.9) : Color.white.opacity(0.28),
                            lineWidth: isMajor ? 3 : 1
                        )
                    }
                }
                RoundedRectangle(cornerRadius: 7)
                    .fill(appAccent.opacity(0.18))
                    .frame(width: size * 0.14, height: 18)
                    .position(center)

                Circle()
                    .stroke(Color.white.opacity(0.55), lineWidth: 2)
                    .frame(width: 20, height: 20)
                    .position(center)

                Circle()
                    .fill(levelStatusColor)
                    .frame(width: 28, height: 28)
                    .shadow(color: levelStatusColor.opacity(0.9), radius: 14)
                    .position(x: markerX, y: markerY)

                Text(String(format: "C %.2f°     P %.2f°", displayCant, displayPitch))
                    .font(labelFont)
                    .foregroundStyle(.white.opacity(0.88))
                    .position(x: center.x, y: center.y - outerRadius * 0.63)

                Text("L")
                    .font(labelFont)
                    .foregroundStyle(.red)
                    .position(x: center.x - outerRadius * 0.54, y: center.y + outerRadius * 0.58)

                Text("0")
                    .font(labelFont)
                    .foregroundStyle(appAccent)
                    .position(x: center.x, y: center.y + outerRadius * 0.58)

                Text("R")
                    .font(labelFont)
                    .foregroundStyle(.blue)
                    .position(x: center.x + outerRadius * 0.54, y: center.y + outerRadius * 0.58)

                Text(levelStatusText)
                    .font(.system(size: 22, weight: .bold))
                    .foregroundStyle(levelStatusColor)
                    .shadow(color: levelStatusColor.opacity(0.35), radius: 8)
                    .position(x: center.x, y: center.y + outerRadius * 0.76)
            }
        }
    }

    // MARK: - Main Status Card

    private var mainStatusCard: some View {
        VStack(spacing: 14) {
            HStack {
                HStack(spacing: 8) {
                    Image(systemName: "waveform.path.ecg")
                        .foregroundStyle(appAccent)

                    Text("Live Data")
                        .font(.headline)
                        .fontWeight(.semibold)
                        .foregroundStyle(.white)
                }

                Spacer()

                batteryPill

                Button {
                    showDebug.toggle()
                } label: {
                    HStack(spacing: 5) {
                        Image(systemName: showDebug ? "chevron.down" : "terminal")
                        Text(showDebug ? "Hide" : "Debug")
                    }
                }
                .font(.caption)
                .buttonStyle(.bordered)
                .controlSize(.small)
                .tint(.white.opacity(0.75))
            }

            Divider()
                .background(Color.white.opacity(0.18))

            HStack(spacing: 0) {
                polishedMetric("Cant", String(format: "%.2f°", displayCant))

                metricDivider

                polishedMetric("Pitch", String(format: "%.2f°", displayPitch))

                metricDivider

                polishedMetric("Cosine", String(format: "%.3f", bleManager.levelData.cosine))

                metricDivider

                polishedMetric("Stability", "\(bleManager.levelData.stability)%")
            }

            Divider()
                .background(Color.white.opacity(0.18))

            VStack(spacing: 10) {
                HStack(spacing: 8) {
                    Image(systemName: "scope")
                        .foregroundStyle(appAccent)

                    Text("Calibration Status")
                        .font(.subheadline)
                        .fontWeight(.semibold)
                        .foregroundStyle(.white)

                    Spacer()
                }

                HStack(spacing: 10) {
                    calibrationPill("Cant", saved: bleManager.levelData.cantCalibrated)
                    calibrationPill("Pitch", saved: bleManager.levelData.pitchCalibrated)
                }
            }
        }
        .padding()
        .background(cardBackground())
        .shadow(color: appAccent.opacity(0.08), radius: 22, y: 10)
    }

    private func polishedMetric(_ title: String, _ value: String) -> some View {
        VStack(spacing: 5) {
            Text(title)
                .font(.caption2)
                .foregroundStyle(softText)

            Text(value)
                .font(.system(size: 23, weight: .bold, design: .rounded))
                .foregroundStyle(appAccent)
                .minimumScaleFactor(0.65)
                .lineLimit(1)
        }
        .frame(maxWidth: .infinity)
    }

    private var metricDivider: some View {
        Rectangle()
            .fill(Color.white.opacity(0.12))
            .frame(width: 1, height: 48)
    }

    private var batteryPill: some View {
        HStack(spacing: 5) {
            Image(systemName: "battery.100")
            Text(batteryDisplayText)
        }
        .font(.caption)
        .fontWeight(.semibold)
        .foregroundStyle(batteryStatusColor)
        .padding(.horizontal, 9)
        .padding(.vertical, 5)
        .background(batteryStatusColor.opacity(0.13))
        .overlay(
            Capsule()
                .stroke(batteryStatusColor.opacity(0.35), lineWidth: 1)
        )
        .clipShape(Capsule())
    }

    // MARK: - Device Controls

    private var deviceControlsCard: some View {
        VStack(spacing: 12) {
            HStack {
                HStack(spacing: 8) {
                    Image(systemName: "slider.horizontal.3")
                        .foregroundStyle(appAccent)

                    Text("Device Controls")
                        .font(.headline)
                        .fontWeight(.semibold)
                        .foregroundStyle(.white)
                }

                Spacer()

                Text("Use carefully")
                    .font(.caption)
                    .foregroundStyle(softText)
            }

            Divider()
                .background(Color.white.opacity(0.18))

            HStack(spacing: 10) {
                calibrationPill("Cant", saved: bleManager.levelData.cantCalibrated)
                calibrationPill("Pitch", saved: bleManager.levelData.pitchCalibrated)
            }

            HStack(spacing: 10) {
                commandButton("Zero Cant", command: "ZERO_CANT\n", tint: appAccent)
                commandButton("Reset Cant", command: "RESET_CANT\n", tint: .red)
            }

            HStack(spacing: 10) {
                commandButton("Zero Pitch", command: "ZERO_PITCH\n", tint: appAccent)
                commandButton("Reset Pitch", command: "RESET_PITCH\n", tint: .red)
            }

            Divider()
                .background(Color.white.opacity(0.18))

            dataRow("Last Sent", bleManager.lastCommandSent)
            dataRow("Last Reply", bleManager.lastCommandReply, valueColor: commandReplyColor)
        }
        .padding()
        .background(cardBackground(borderColor: appAccent.opacity(0.22)))
        .shadow(color: appAccent.opacity(0.06), radius: 18, y: 8)
    }

    private func calibrationPill(_ title: String, saved: Bool) -> some View {
        let statusColor = saved ? appAccent : softText
        let backgroundColor = saved ? appAccent.opacity(0.12) : Color.white.opacity(0.06)
        let borderColor = saved ? appAccent.opacity(0.35) : Color.white.opacity(0.14)

        return HStack(spacing: 7) {
            Image(systemName: saved ? "checkmark.circle.fill" : "circle")
                .foregroundStyle(statusColor)

            Text("\(title):")
                .foregroundStyle(.white.opacity(0.78))

            Text(saved ? "Saved" : "Not Saved")
                .foregroundStyle(statusColor)
        }
        .font(.caption)
        .fontWeight(.semibold)
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .frame(maxWidth: .infinity)
        .background(backgroundColor)
        .overlay(
            Capsule()
                .stroke(borderColor, lineWidth: 1)
        )
        .clipShape(Capsule())
    }

    private func commandButton(_ title: String, command: String, tint: Color) -> some View {
        Button {
            bleManager.sendCommand(command)
        } label: {
            Text(title)
                .font(.caption)
                .fontWeight(.semibold)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 9)
        }
        .buttonStyle(.bordered)
        .tint(tint)
    }

    private var commandReplyColor: Color {
        if bleManager.lastCommandReply.lowercased().hasPrefix("ack:") {
            return appAccent
        }

        return softText
    }

    // MARK: - Diagnostics

    private var diagnosticsCard: some View {
        VStack(spacing: 10) {
            HStack {
                HStack(spacing: 8) {
                    Image(systemName: "gauge.with.dots.needle.67percent")
                        .foregroundStyle(appAccent)

                    Text("Device Diagnostics")
                        .font(.headline)
                        .fontWeight(.semibold)
                        .foregroundStyle(.white)
                }

                Spacer()

                Text(dataUpdatingBadgeText)
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundStyle(isDataUpdating ? appAccent : .yellow)
            }

            Divider()
                .background(Color.white.opacity(0.18))

            dataRow("Device", bleManager.connectedDeviceName)
            dataRow("Live Data", dataUpdatingText, valueColor: isDataUpdating ? appAccent : .yellow)
            dataRow("Live Packets", "\(bleManager.packetsReceived)")
            dataRow("Last Live", lastLivePacketText)
            dataRow("Status Packets", "\(bleManager.statusPacketsReceived)")
            dataRow("Last Status", lastStatusPacketText)
            dataRow("Offset", String(format: "%.2f°", bleManager.levelData.offset))

            dataRow(
                "Cant Calibration",
                bleManager.levelData.cantCalibrated ? "Saved" : "Not saved",
                valueColor: bleManager.levelData.cantCalibrated ? appAccent : softText
            )

            dataRow(
                "Pitch Calibration",
                bleManager.levelData.pitchCalibrated ? "Saved" : "Not saved",
                valueColor: bleManager.levelData.pitchCalibrated ? appAccent : softText
            )

            dataRow("Battery", fullBatteryDisplayText, valueColor: batteryStatusColor)
        }
        .padding()
        .background(cardBackground())
        .shadow(color: appAccent.opacity(0.06), radius: 18, y: 8)
    }

    private func dataRow(_ title: String, _ value: String, valueColor: Color = .white) -> some View {
        HStack {
            Text(title)
                .foregroundStyle(softText)

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
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Image(systemName: "chevron.left.forwardslash.chevron.right")
                    .foregroundStyle(appAccent)

                Text("Raw BLE Data")
                    .font(.headline)
                    .fontWeight(.semibold)
                    .foregroundStyle(.white)
            }

            ScrollView {
                Text(bleManager.rawData)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(softText)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
                    .padding(10)
            }
            .frame(minHeight: 72, maxHeight: 115)
            .background(Color.black.opacity(0.35))
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .stroke(Color.white.opacity(0.08), lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 14))
        }
        .padding()
        .background(cardBackground())
        .shadow(color: appAccent.opacity(0.05), radius: 16, y: 8)
    }

    // MARK: - Card Background Helper

    private func cardBackground(
        cornerRadius: CGFloat = 22,
        borderColor: Color? = nil
    ) -> some View {
        RoundedRectangle(cornerRadius: cornerRadius)
            .fill(cardColor.opacity(0.96))
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(borderColor ?? cardBorder, lineWidth: 1)
            )
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

    private var lastLivePacketText: String {
        guard let lastPacketDate = bleManager.lastPacketDate else {
            return "--"
        }

        let seconds = max(0, Int(now.timeIntervalSince(lastPacketDate)))
        return "\(seconds)s ago"
    }

    private var lastStatusPacketText: String {
        guard let lastStatusPacketDate = bleManager.lastStatusPacketDate else {
            return "--"
        }

        let seconds = max(0, Int(now.timeIntervalSince(lastStatusPacketDate)))
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
            return appAccent
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
            return appAccent
        }
    }
}

#Preview {
    ContentView()
}
