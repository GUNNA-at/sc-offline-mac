import AVFoundation
import AppKit
let url = URL(fileURLWithPath: CommandLine.arguments[1]); let out = CommandLine.arguments[2]
let times = CommandLine.arguments.dropFirst(3).compactMap { Double($0) }
let gen = AVAssetImageGenerator(asset: AVURLAsset(url: url))
gen.appliesPreferredTrackTransform = true; gen.maximumSize = CGSize(width: 640, height: 420)
gen.requestedTimeToleranceBefore = .zero; gen.requestedTimeToleranceAfter = .zero
var images: [CGImage] = []
for t in times { if let img = try? gen.copyCGImage(at: CMTime(seconds: t, preferredTimescale: 600), actualTime: nil) { images.append(img) } }
guard let first = images.first else { print("no frames"); exit(1) }
let w = first.width, h = first.height, cols = 3, rows = (images.count + cols - 1) / cols
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: w * cols, pixelsHigh: h * rows, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
for (i, img) in images.enumerated() { NSGraphicsContext.current!.cgContext.draw(img, in: CGRect(x: (i % cols) * w, y: (rows - 1 - i / cols) * h, width: w, height: h)) }
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: out))
print("\(images.count) frames -> \(out) (\(w * cols)x\(h * rows))")
