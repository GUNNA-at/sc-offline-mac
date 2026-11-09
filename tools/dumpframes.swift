// dumpframes <video> <outdir> <start s> <end s> <step s> [width=640]: writes one PNG per step, for building an animated
// preview (img2webp) when ffmpeg is not usable. Frames are exact (zero tolerance), named f0001.png, f0002.png, ...
import AVFoundation
import AppKit
let a = CommandLine.arguments
guard a.count >= 6, let start = Double(a[3]), let end = Double(a[4]), let step = Double(a[5]) else { print("usage: dumpframes <video> <outdir> <start> <end> <step> [width]"); exit(2) }
let width = a.count > 6 ? Double(a[6]) ?? 640 : 640
let gen = AVAssetImageGenerator(asset: AVURLAsset(url: URL(fileURLWithPath: a[1])))
gen.appliesPreferredTrackTransform = true; gen.maximumSize = CGSize(width: width, height: width)
gen.requestedTimeToleranceBefore = .zero; gen.requestedTimeToleranceAfter = .zero
try? FileManager.default.createDirectory(atPath: a[2], withIntermediateDirectories: true)
var n = 0, t = start
while t <= end {
    if let img = try? gen.copyCGImage(at: CMTime(seconds: t, preferredTimescale: 600), actualTime: nil) {
        n += 1
        let rep = NSBitmapImageRep(cgImage: img)
        try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: String(format: "%@/f%04d.png", a[2], n)))
    }
    t += step
}
print("\(n) frames in \(a[2])")
