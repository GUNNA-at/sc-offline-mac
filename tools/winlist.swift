import CoreGraphics
import Foundation
let opts: CGWindowListOption = [.optionAll, .excludeDesktopElements]
guard let list = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as? [[String: Any]] else { exit(1) }
for w in list {
    let owner = w[kCGWindowOwnerName as String] as? String ?? ""
    let name = w[kCGWindowName as String] as? String ?? ""
    let num = w[kCGWindowNumber as String] as? Int ?? 0
    let layer = w[kCGWindowLayer as String] as? Int ?? 0
    let onscreen = (w[kCGWindowIsOnscreen as String] as? Bool) ?? false
    let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let lo = owner.lowercased()
    if lo.contains("wine") || lo.contains("rsi") || lo.contains("crossover") || lo.contains("starcitizen") || lo.contains("star citizen") || name.lowercased().contains("rsi") || name.lowercased().contains("star citizen") {
        print("id=\(num) layer=\(layer) onscreen=\(onscreen) owner='\(owner)' name='\(name)' bounds=\(b["X"] ?? 0),\(b["Y"] ?? 0) \(b["Width"] ?? 0)x\(b["Height"] ?? 0)")
    }
}
