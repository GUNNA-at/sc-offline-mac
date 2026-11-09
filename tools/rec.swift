// rec <out.mov> <seconds> [width=1280] [fps=30]: record the main display to a small H.264 movie with ScreenCaptureKit.
// Lighter than `screencapture -v` (which writes the full Retina picture, ~10 MB/s, and stopped by itself during a memory
// burst). Needs the Screen Recording permission of the app that starts it. Ctrl-C / SIGINT ends the recording early.
import Foundation
import ScreenCaptureKit
import AVFoundation
import CoreMedia

final class Recorder: NSObject, SCStreamOutput, SCStreamDelegate {
    let writer: AVAssetWriter
    let input: AVAssetWriterInput
    var started = false
    var frames = 0
    init(url: URL, width: Int, height: Int, fps: Int) throws {
        writer = try AVAssetWriter(outputURL: url, fileType: .mov)
        input = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: width, AVVideoHeightKey: height,
            AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: 5_000_000, AVVideoExpectedSourceFrameRateKey: fps,
                                              AVVideoMaxKeyFrameIntervalKey: fps * 2]])
        input.expectsMediaDataInRealTime = true
        writer.add(input)
    }
    func stream(_ stream: SCStream, didOutputSampleBuffer sb: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, sb.isValid,
              let att = (CMSampleBufferGetSampleAttachmentsArray(sb, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]])?.first,
              let raw = att[.status] as? Int, SCFrameStatus(rawValue: raw) == .complete else { return }
        if !started {
            started = true
            writer.startWriting()
            writer.startSession(atSourceTime: CMSampleBufferGetPresentationTimeStamp(sb))
        }
        if input.isReadyForMoreMediaData, input.append(sb) { frames += 1 }
    }
    func stream(_ stream: SCStream, didStopWithError error: Error) { FileHandle.standardError.write("stream stopped: \(error)\n".data(using: .utf8)!) }
}

let args = CommandLine.arguments
guard args.count >= 3, let seconds = Double(args[2]) else { print("usage: rec <out.mov> <seconds> [width=1280] [fps=30]"); exit(2) }
let width = args.count > 3 ? Int(args[3]) ?? 1280 : 1280
let fps = args.count > 4 ? Int(args[4]) ?? 30 : 30
let url = URL(fileURLWithPath: args[1])
try? FileManager.default.removeItem(at: url)
var stopNow = false
signal(SIGINT) { _ in stopNow = true }
signal(SIGTERM) { _ in stopNow = true }

let sem = DispatchSemaphore(value: 0)
Task {
    do {
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        guard let display = content.displays.first else { print("no display"); exit(1) }
        let height = Int(Double(width) * Double(display.height) / Double(display.width) / 2) * 2
        let cfg = SCStreamConfiguration()
        cfg.width = width; cfg.height = height
        cfg.minimumFrameInterval = CMTime(value: 1, timescale: CMTimeScale(fps))
        cfg.showsCursor = true
        cfg.queueDepth = 6
        cfg.pixelFormat = kCVPixelFormatType_32BGRA
        let rec = try Recorder(url: url, width: width, height: height, fps: fps)
        let stream = SCStream(filter: SCContentFilter(display: display, excludingWindows: []), configuration: cfg, delegate: rec)
        try stream.addStreamOutput(rec, type: .screen, sampleHandlerQueue: DispatchQueue(label: "rec"))
        try await stream.startCapture()
        print("recording \(width)x\(height) at up to \(fps) fps for \(Int(seconds)) s -> \(url.path)")
        let t0 = Date()
        while Date().timeIntervalSince(t0) < seconds && !stopNow { try await Task.sleep(nanoseconds: 200_000_000) }
        try await stream.stopCapture()
        rec.input.markAsFinished()
        await rec.writer.finishWriting()
        print("done: \(rec.frames) frames, \(String(format: "%.1f", Date().timeIntervalSince(t0))) s, status \(rec.writer.status.rawValue)")
    } catch { print("error: \(error)") }
    sem.signal()
}
sem.wait()
