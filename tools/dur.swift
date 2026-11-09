import AVFoundation
let url = URL(fileURLWithPath: CommandLine.arguments[1])
let asset = AVURLAsset(url: url)
let sem = DispatchSemaphore(value: 0)
Task {
    do {
        let d = try await asset.load(.duration)
        let tracks = try await asset.loadTracks(withMediaType: .video)
        var size = CGSize.zero; var fps: Float = 0
        if let t = tracks.first { size = try await t.load(.naturalSize); fps = try await t.load(.nominalFrameRate) }
        print(String(format: "duration %.1f s, %dx%d, %.1f fps", CMTimeGetSeconds(d), Int(size.width), Int(size.height), fps))
    } catch { print("error: \(error)") }
    sem.signal()
}
sem.wait()
