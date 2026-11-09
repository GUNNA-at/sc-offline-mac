// Build a render pipeline state from a MoltenVK-dumped fragment shader + a generated stub vertex function, through
// Metal's runtime compiler (MTLCompilerService), to reproduce back-end (pipeline) compile failures outside the game.
// usage: psotest <fastmath 0|1> <opt default|size> <file.metal>...
import Foundation
import Metal
let args = CommandLine.arguments
guard args.count >= 4, let dev = MTLCreateSystemDefaultDevice() else { print("usage: psotest <fastmath 0|1> <opt default|size> files..."); exit(2) }
let fast = args[1] == "1", optSize = args[2] == "size"
func opts() -> MTLCompileOptions { let o = MTLCompileOptions(); if #available(macOS 15.0, *) { o.mathMode = fast ? .fast : .safe } else { o.fastMathEnabled = fast }; if #available(macOS 13.0, *) { o.optimizationLevel = optSize ? .size : .default }; return o }
func firstMatch(_ pattern: String, _ s: String) -> [String]? {
    guard let re = try? NSRegularExpression(pattern: pattern, options: [.dotMatchesLineSeparators]) else { return nil }
    guard let m = re.firstMatch(in: s, range: NSRange(s.startIndex..., in: s)) else { return nil }
    return (0..<m.numberOfRanges).map { Range(m.range(at: $0), in: s).map { String(s[$0]) } ?? "" }
}
func pixelFormat(_ t: String) -> MTLPixelFormat {
    switch t {
    case "float4", "half4": return .rgba16Float
    case "float2", "half2": return .rg16Float
    case "float", "half": return .r16Float
    case "uint4", "int4": return .rgba32Uint
    case "uint2", "int2": return .rg32Uint
    case "uint", "int": return .r32Uint
    default: return .rgba16Float
    }
}
var fails = 0
for path in args[3...] {
    let name = (path as NSString).lastPathComponent
    guard let src = try? String(contentsOfFile: path, encoding: .utf8) else { print("SKIP \(name)"); continue }
    guard let sig = firstMatch(#"fragment\s+(\w+)\s+(\w+)\s*\(([^)]*)\)"#, src) else { print("SKIP \(name): no fragment entry"); continue }
    let outType = sig[1], entry = sig[2], params = sig[3]
    // fragment input struct (stage_in) -> stub vertex output
    var vsSrc = "#include <metal_stdlib>\nusing namespace metal;\n"
    var vOut = "struct StubOut { float4 pos [[position]]; "
    if let si = firstMatch(#"(\w+)\s+\w+\s+\[\[stage_in\]\]"#, params), let def = firstMatch("struct\\s+\(si[1])\\s*\\{(.*?)\\};", src) {
        var body = def[1]
        body = body.replacingOccurrences(of: #"\[\[user\((locn\d+(?:_\d+)?)\)[^\]]*\]\]"#, with: "[[user($1)]]", options: .regularExpression)
        body = body.replacingOccurrences(of: #"\[\[(flat|centroid_perspective|centroid_no_perspective|center_no_perspective|sample_perspective|sample_no_perspective|center_perspective)\]\]"#, with: "", options: .regularExpression)
        // the fragment input struct may also contain [[position]], [[front_facing]], etc.: drop those lines in the stub
        body = body.split(separator: "\n").filter { !$0.contains("[[position]]") && !$0.contains("[[front_facing]]") && !$0.contains("[[sample_id]]") && !$0.contains("[[color(") && !$0.contains("[[sample_mask]]") && !$0.contains("[[point_coord]]") && !$0.contains("[[primitive_id]]") && !$0.contains("[[barycentric") && !$0.contains("[[render_target_array_index]]") && !$0.contains("[[viewport_array_index]]") }.joined(separator: "\n")
        vOut += body
    }
    vOut += " };\n"
    vsSrc += vOut + "vertex StubOut stub_vs(uint vid [[vertex_id]]) { StubOut o = {}; o.pos = float4(0,0,0,1); return o; }\n"
    // outputs
    let desc = MTLRenderPipelineDescriptor()
    var attachments: [String] = []
    if let od = firstMatch("struct\\s+\(outType)\\s*\\{(.*?)\\};", src) {
        for line in od[1].split(separator: "\n") {
            if let m = firstMatch(#"(\w+)\s+\w+\s+\[\[color\((\d+)\)\]\]"#, String(line)), let idx = Int(m[2]) {
                desc.colorAttachments[idx].pixelFormat = pixelFormat(m[1]); attachments.append("c\(idx):\(m[1])")
            }
            if line.contains("[[depth(") { desc.depthAttachmentPixelFormat = .depth32Float; attachments.append("depth") }
        }
    } else if outType == "void" { attachments.append("void") } else { desc.colorAttachments[0].pixelFormat = .rgba16Float; attachments.append("c0:\(outType)") }
    if attachments.isEmpty { desc.colorAttachments[0].pixelFormat = .rgba16Float }
    if params.contains("[[color(") { for i in 0..<4 where desc.colorAttachments[i].pixelFormat == .invalid { desc.colorAttachments[i].pixelFormat = .rgba16Float } }
    let t0 = Date()
    do {
        let fsLib = try dev.makeLibrary(source: src, options: opts())
        let vsLib = try dev.makeLibrary(source: vsSrc, options: opts())
        guard let fsFn = fsLib.makeFunction(name: entry), let vsFn = vsLib.makeFunction(name: "stub_vs") else { print("FAIL \(name): function lookup"); fails += 1; continue }
        desc.vertexFunction = vsFn; desc.fragmentFunction = fsFn
        let t1 = Date()
        _ = try dev.makeRenderPipelineState(descriptor: desc)
        print(String(format: "OK   lib %5.2fs pso %6.2fs  %@ (%@) [%@]", t1.timeIntervalSince(t0), Date().timeIntervalSince(t1), name, entry, attachments.joined(separator: " ")))
    } catch {
        fails += 1
        print(String(format: "FAIL %6.2fs %@ (%@) :: %@", Date().timeIntervalSince(t0), name, entry, String("\(error)".replacingOccurrences(of: "\n", with: " ").prefix(160))))
        // fallback: same descriptor, empty fragment function, all color writes masked off
        do {
            let stubLib = try dev.makeLibrary(source: "#include <metal_stdlib>\nusing namespace metal;\nfragment void stub_fs() {}\n", options: opts())
            desc.fragmentFunction = stubLib.makeFunction(name: "stub_fs")
            for i in 0..<8 where desc.colorAttachments[i].pixelFormat != .invalid { desc.colorAttachments[i].writeMask = [] }
            let t2 = Date(); _ = try dev.makeRenderPipelineState(descriptor: desc)
            print(String(format: "     fallback OK %5.2fs (empty fragment stage, write masks off)", Date().timeIntervalSince(t2)))
        } catch { print("     fallback FAILED :: \("\(error)".prefix(200))") }
    }
}
print("done, \(fails) failures"); exit(fails == 0 ? 0 : 1)
