// Compile MSL files through Metal's runtime compiler (MTLCompilerService over XPC), the same path MoltenVK uses.
// usage: rtcompile <fastmath 0|1> <file.metal>...   prints OK/FAIL per file; FAIL lines carry the error text.
import Foundation
import Metal
let args = CommandLine.arguments
guard args.count >= 3, let dev = MTLCreateSystemDefaultDevice() else { print("usage: rtcompile <fastmath 0|1> files..."); exit(2) }
let fast = args[1] == "1"
var fails = 0
for path in args[2...] {
    guard let src = try? String(contentsOfFile: path, encoding: .utf8) else { print("SKIP \(path)"); continue }
    let opt = MTLCompileOptions()
    if #available(macOS 15.0, *) { opt.mathMode = fast ? .fast : .safe } else { opt.fastMathEnabled = fast }
    let t0 = Date()
    do {
        let lib = try dev.makeLibrary(source: src, options: opt)
        // also build the function + a pipeline state to exercise the back-end compile for fragment/vertex/compute entry points
        let names = lib.functionNames
        var pso = "no-pso"
        if let fn = names.first.flatMap({ lib.makeFunction(name: $0) }) {
            if fn.functionType == .kernel { _ = try dev.makeComputePipelineState(function: fn); pso = "cs-pso" }
        }
        print(String(format: "OK   %5.2fs %@ %@ (%@)", Date().timeIntervalSince(t0), pso, (path as NSString).lastPathComponent, names.joined(separator: ",")))
    } catch {
        fails += 1
        print(String(format: "FAIL %5.2fs %@ :: %@", Date().timeIntervalSince(t0), (path as NSString).lastPathComponent, "\(error)".replacingOccurrences(of: "\n", with: " ").prefix(300) as CVarArg))
    }
}
print("done, \(fails) failures")
exit(fails == 0 ? 0 : 1)
