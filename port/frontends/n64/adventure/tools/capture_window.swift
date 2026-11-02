// Record only one owned window. Cursor, system audio and microphone are excluded.
import Foundation
import AppKit
import CoreGraphics
import ScreenCaptureKit
import AVFoundation

final class RecordingDelegate: NSObject, SCRecordingOutputDelegate {
    var finished = false
    var failure: Error?
    var stopRequested = false
    func recordingOutputDidFinishRecording(_ output: SCRecordingOutput) { finished = true }
    func recordingOutput(_ output: SCRecordingOutput, didFailWithError error: Error) {
        failure = error
        finished = true
    }
}

@main struct WindowRecorder {
    static func main() async throws {
        _ = NSApplication.shared
        let args = CommandLine.arguments
        let windowID = CGWindowID(args[1])!
        let seconds = Double(args[2])!
        let destination = URL(fileURLWithPath: args[3])
        let width = Int(args[4])!, height = Int(args[5])!
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: false)
        guard let window = content.windows.first(where: { $0.windowID == windowID }) else {
            throw NSError(domain: "WindowRecorder", code: 1, userInfo: [NSLocalizedDescriptionKey: "Owned capture window missing"])
        }
        let config = SCStreamConfiguration()
        config.width = width
        config.height = height
        config.minimumFrameInterval = CMTime(value: 1, timescale: 60)
        config.queueDepth = 6
        config.showsCursor = false
        config.showMouseClicks = false
        config.capturesAudio = false
        config.captureMicrophone = false
        config.ignoreShadowsSingleWindow = true
        config.scalesToFit = true
        let outputConfig = SCRecordingOutputConfiguration()
        outputConfig.outputURL = destination
        outputConfig.videoCodecType = .h264
        outputConfig.outputFileType = outputConfig.availableOutputFileTypes.contains(.mov) ? .mov : .mp4
        let delegate = RecordingDelegate()
        let output = SCRecordingOutput(configuration: outputConfig, delegate: delegate)
        let stream = SCStream(filter: SCContentFilter(desktopIndependentWindow: window), configuration: config, delegate: nil)
        try stream.addRecordingOutput(output)
        let signals = [SIGINT, SIGTERM].map { number -> DispatchSourceSignal in
            signal(number, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: number, queue: .main)
            source.setEventHandler { delegate.stopRequested = true }
            source.resume()
            return source
        }
        try await stream.startCapture()
        let ready: [String: Any] = ["window_id": windowID, "shows_cursor": false, "captures_audio": false, "capture_microphone": false, "width": width, "height": height, "fps": 60, "ready_epoch": Date().timeIntervalSince1970]
        print(String(data: try JSONSerialization.data(withJSONObject: ready, options: .sortedKeys), encoding: .utf8)!)
        fflush(stdout)
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline && !delegate.stopRequested && delegate.failure == nil {
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        try await stream.stopCapture()
        for _ in 0..<100 {
            if delegate.finished { break }
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        withExtendedLifetime(signals) {}
        if let error = delegate.failure { throw error }
        guard delegate.finished else { throw NSError(domain: "WindowRecorder", code: 2, userInfo: [NSLocalizedDescriptionKey: "Recording did not finish"] ) }
    }
}
