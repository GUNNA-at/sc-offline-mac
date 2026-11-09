import Foundation
import CoreGraphics
import ImageIO
let path = CommandLine.arguments[1]
guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil), let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else { print("ERR"); exit(1) }
let w = img.width, h = img.height
let cs = CGColorSpaceCreateDeviceRGB()
var data = [UInt8](repeating: 0, count: w*h*4)
let ctx = CGContext(data: &data, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w*4, space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))
var white = 0, total = 0
// skip the top 40px title bar
for y in 40..<h { for x in stride(from: 0, to: w, by: 2) { let i = (y*w+x)*4; total += 1; if data[i] > 245 && data[i+1] > 245 && data[i+2] > 245 { white += 1 } } }
print(String(format: "%.1f", Double(white)*100/Double(max(total,1))))
