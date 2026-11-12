// Engine.swift: everything the launcher does, with no interface attached. The app window and the command line both drive
// this one class and both receive the same events.
//
// What it sets up, all under ~/Library/Application Support/sc-offline-mac:
//   engine/   the official WineHQ build for macOS (downloaded, never modified)
//   dxvk/     DXVK for macOS (downloaded; Wine's "builtin" stamp removed so the files load as ordinary DLLs)
//   kit/      this project's own parts: the patched MoltenVK, the shim in front of it, winegrid, the squeezer
//   prefix/   the Windows folder (Wine prefix) with the publisher's launcher, the game and the mod
// The game only ever runs inside a macOS sandbox profile that denies it all network access.
import Foundation
import CryptoKit
import Darwin

typealias JSON = [String: Any]

enum StepState: String { case done, todo, running, hold, failed }

struct Fetch {
    let url: String, file: String, bytes: Int64, sha256: String
}

final class Engine {
    static let version = "0.1.1"

    // Pinned sources. Each is the project's own official release channel; every file is checked against its digest.
    static let wine = Fetch(url: "https://github.com/Gcenx/macOS_Wine_builds/releases/download/11.0_1/wine-stable-11.0_1-osx64.tar.xz",
                            file: "wine-stable-11.0_1-osx64.tar.xz", bytes: 185_103_252,
                            sha256: "b50dc50ec7f41d58b115a6b685d4d1315ba3c797bd3aa0f49213f2703cb82388")
    static let dxvk = Fetch(url: "https://github.com/Gcenx/DXVK-macOS/releases/download/v1.10.3-20230507-repack/dxvk-macOS-async-v1.10.3-20230507-repack-builtin.tar.gz",
                            file: "dxvk-macOS-async-v1.10.3-20230507-repack-builtin.tar.gz", bytes: 2_785_833,
                            sha256: "810b1e5caf8ce975b784fae866a130ad23fa0ea233b0e5609cbc4a45f3ef6f00")
    static let mod = Fetch(url: "https://github.com/scuba-sc/sc-offline/releases/download/v0.7.7/sc-offline-v0.7.7.zip",
                           file: "sc-offline-v0.7.7.zip", bytes: 3_460_000,
                           sha256: "24599b8f140e137cd1b8d0701cb1c0b8b895e154a3bbed0107d446b266a0ef0f")
    static let modVersion = "0.7.7"
    static let rsiFeed = "https://install.robertsspaceindustries.com/rel/2/latest.yml"
    static let rsiBase = "https://install.robertsspaceindustries.com/rel/2/"

    let fm = FileManager.default
    let root: URL
    let kitSource: URL                       // where this build's own parts are (the app bundle's Resources/kit)
    var emit: (JSON) -> Void = { _ in }      // every event goes through here; may be called from any thread
    private let work = DispatchQueue(label: "engine.work")
    private var busy = false
    private let lock = NSLock()

    // session state
    private var gameTask: Process?
    private var launcherTask: Process?
    private var telemetry: DispatchSourceTimer?
    private var squeezer: Process?

    init(kitSource: URL) {
        self.kitSource = kitSource
        // SCOM_ROOT moves the whole setup elsewhere (used to try the fresh-Mac path without touching the real one)
        if let alt = ProcessInfo.processInfo.environment["SCOM_ROOT"], !alt.isEmpty { root = URL(fileURLWithPath: alt) }
        else { root = fm.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/sc-offline-mac") }
        try? fm.createDirectory(at: root, withIntermediateDirectories: true)
    }

    // MARK: places
    var engineDir: URL { root.appendingPathComponent("engine/wine-stable-11.0_1") }
    var wineHome: URL { engineDir.appendingPathComponent("Wine Stable.app/Contents/Resources/wine") }
    var wine: URL { wineHome.appendingPathComponent("bin/wine") }
    var wineserver: URL { wineHome.appendingPathComponent("bin/wineserver") }
    var dxvkDir: URL { root.appendingPathComponent("dxvk") }
    var kitDir: URL { root.appendingPathComponent("kit") }
    var downloads: URL { root.appendingPathComponent("downloads") }
    var logs: URL { root.appendingPathComponent("logs") }
    var prefix: URL { root.appendingPathComponent("prefix") }
    var driveC: URL { prefix.appendingPathComponent("drive_c") }
    var system32: URL { driveC.appendingPathComponent("windows/system32") }
    var rsiHome: URL { driveC.appendingPathComponent("Program Files/Roberts Space Industries") }
    var gameDir: URL { rsiHome.appendingPathComponent("StarCitizen") }
    var live: URL { gameDir.appendingPathComponent("LIVE") }
    var gameLog: URL { live.appendingPathComponent("Game.log") }
    var rsiLauncherExe: URL { rsiHome.appendingPathComponent("RSI Launcher/RSI Launcher.exe") }
    var kitUser: URL { driveC.appendingPathComponent("users/crossover") }   // macfix reads its switches from C:\users\crossover
    var modDir: URL { kitUser.appendingPathComponent("sc-offline") }
    var winUser: URL { driveC.appendingPathComponent("users/\(NSUserName())") }
    var rsiLog: URL { winUser.appendingPathComponent("AppData/Roaming/rsilauncher/logs/log.log") }
    static let shimLog = URL(fileURLWithPath: "/private/tmp/mvkshim.log")
    static let gridLog = URL(fileURLWithPath: "/private/tmp/winegrid.log")
    static let gameWin = "C:\\Program Files\\Roberts Space Industries\\StarCitizen"
    static let modWin = "C:\\users\\crossover\\sc-offline\\sc-offline.exe"

    func exists(_ u: URL) -> Bool { fm.fileExists(atPath: u.path) }

    // MARK: small tools
    @discardableResult
    func run(_ exe: String, _ args: [String], env: [String: String]? = nil, cwd: URL? = nil, quiet: Bool = true) -> (code: Int32, out: String) {
        let p = Process(); p.executableURL = URL(fileURLWithPath: exe); p.arguments = args
        if let env = env { p.environment = env }
        if let cwd = cwd { p.currentDirectoryURL = cwd }
        let pipe = Pipe(); p.standardOutput = pipe; p.standardError = quiet ? FileHandle.nullDevice : pipe
        p.standardInput = FileHandle.nullDevice
        do { try p.run() } catch { return (-1, "\(error.localizedDescription)") }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        return (p.terminationStatus, String(decoding: data, as: UTF8.self))
    }

    func say(_ step: String, _ state: StepState, _ detail: String, progress: Double? = nil, extra: JSON = [:]) {
        var e: JSON = ["type": "step", "id": step, "state": state.rawValue, "detail": detail]
        if let p = progress { e["progress"] = p }
        for (k, v) in extra { e[k] = v }
        emit(e)
    }

    /// The environment every Wine process of this setup gets. `forGame` adds what only the game needs.
    func wineEnv(forGame: Bool = false) -> [String: String] {
        var e: [String: String] = [:]
        let keep = ["HOME", "USER", "LOGNAME", "PATH", "TMPDIR", "LANG", "LC_ALL", "SHELL"]
        for k in keep { if let v = ProcessInfo.processInfo.environment[k] { e[k] = v } }
        e["WINEPREFIX"] = prefix.path
        e["WINEDEBUG"] = "-all"
        e["WINEDLLOVERRIDES"] = "winemenubuilder.exe=d;mscoree=d;mshtml=d;dinput8=n,b;vulkan-1=n,b;d3d11,d3d10core=n,b"
        e["DYLD_LIBRARY_PATH"] = kitDir.path                  // the shim answers for libMoltenVK and libvulkan
        e["ROSETTA_ADVERTISE_AVX"] = "1"                      // Rosetta reports AVX only when asked; the game needs it
        e["MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS"] = "0"
        e["MVK_CONFIG_FAST_MATH_ENABLED"] = "0"
        e["MVK_CONFIG_SHOULD_MAXIMIZE_CONCURRENT_COMPILATION"] = "1"
        e["MVKSHIM_FRAME_STATS"] = "1"
        e["MVKSHIM_PRESENT_MODE"] = "immediate"
        if forGame {
            e["WINEGRID"] = "1"; e["WINEGRID_ONLY"] = "StarCitizen.exe"
            e["DYLD_INSERT_LIBRARIES"] = kitDir.appendingPathComponent("libwinegrid.dylib").path
        }
        return e
    }

    @discardableResult
    func wineRun(_ args: [String], forGame: Bool = false, cwd: URL? = nil) -> (code: Int32, out: String) {
        run(wine.path, args, env: wineEnv(forGame: forGame), cwd: cwd)
    }
    func serverKill() { run(wineserver.path, ["-k"], env: wineEnv()); usleep(1_500_000) }
    func serverWait() { run(wineserver.path, ["-w"], env: wineEnv()) }

    func sha(_ url: URL, _ use512: Bool = false) -> Data? {
        guard let h = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? h.close() }
        var s256 = SHA256(), s512 = SHA512()
        while true {
            let d = h.readData(ofLength: 4 << 20); if d.isEmpty { break }
            if use512 { s512.update(data: d) } else { s256.update(data: d) }
        }
        return use512 ? Data(s512.finalize()) : Data(s256.finalize())
    }
    func hex(_ d: Data) -> String { d.map { String(format: "%02x", $0) }.joined() }

    /// Download with progress and a digest check. Returns the local file, or throws with a plain message.
    func fetch(step: String, what: String, url: String, file: String, sha256: String? = nil, sha512b64: String? = nil) throws -> URL {
        try? fm.createDirectory(at: downloads, withIntermediateDirectories: true)
        let dest = downloads.appendingPathComponent(file)
        func good(_ u: URL) -> Bool {
            if let want = sha256, let got = sha(u) { return hex(got) == want }
            if let want = sha512b64, let got = sha(u, true) { return got.base64EncodedString() == want }
            return sha256 == nil && sha512b64 == nil
        }
        if exists(dest), good(dest) { return dest }
        try? fm.removeItem(at: dest)
        guard let u = URL(string: url.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? url) else { throw Fail("bad address: \(url)") }
        let sem = DispatchSemaphore(value: 0)
        var failure: String?
        let delegate = DownloadDelegate(progress: { [weak self] done, total in
            let mb = Double(done) / 1_048_576, tot = Double(total) / 1_048_576
            self?.say(step, .running, total > 0 ? String(format: "%@: %.0f of %.0f MB", what, mb, tot) : String(format: "%@: %.0f MB", what, mb),
                      progress: total > 0 ? Double(done) / Double(total) : nil)
        }, finished: { [fm] tmp, error in
            if let error = error { failure = error }
            else if let tmp = tmp { do { try fm.moveItem(at: tmp, to: dest) } catch { failure = error.localizedDescription } }
            sem.signal()
        })
        let session = URLSession(configuration: .ephemeral, delegate: delegate, delegateQueue: nil)
        var req = URLRequest(url: u); req.setValue("sc-offline-mac/\(Engine.version)", forHTTPHeaderField: "User-Agent")
        session.downloadTask(with: req).resume()
        sem.wait(); session.finishTasksAndInvalidate()
        if let f = failure { throw Fail("\(what): download failed (\(f))") }
        say(step, .running, "\(what): checking the file")
        guard good(dest) else { try? fm.removeItem(at: dest); throw Fail("\(what): the downloaded file does not match its published checksum, so it was deleted") }
        return dest
    }

    // MARK: status
    func gamePid() -> Int32? {
        let r = run("/bin/ps", ["-axo", "pid=,command="])
        for line in r.out.split(separator: "\n") where line.contains("Bin64\\StarCitizen.exe") || line.contains("Bin64/StarCitizen.exe") {
            if let pid = Int32(line.trimmingCharacters(in: .whitespaces).split(separator: " ").first ?? "") { return pid }
        }
        return nil
    }

    func machine() -> JSON {
        var size = 0; sysctlbyname("hw.memsize", nil, &size, nil, 0)
        var mem: UInt64 = 0; var len = MemoryLayout<UInt64>.size; sysctlbyname("hw.memsize", &mem, &len, nil, 0)
        var brand = [CChar](repeating: 0, count: 128); var blen = brand.count; sysctlbyname("machdep.cpu.brand_string", &brand, &blen, nil, 0)
        var model = [CChar](repeating: 0, count: 64); var mlen = model.count; sysctlbyname("hw.model", &model, &mlen, nil, 0)
        var arm: Int32 = 0; var alen = MemoryLayout<Int32>.size; sysctlbyname("hw.optional.arm64", &arm, &alen, nil, 0)
        let free = (try? root.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey]).volumeAvailableCapacityForImportantUsage) ?? 0
        let os = ProcessInfo.processInfo.operatingSystemVersion
        let rosetta = exists(URL(fileURLWithPath: "/Library/Apple/usr/libexec/oah/libRosettaRuntime"))
        return ["chip": String(cString: brand), "model": String(cString: model), "appleSilicon": arm == 1,
                "memoryGB": Int((Double(mem) / 1_073_741_824).rounded()), "freeGB": Double(free) / 1_000_000_000,
                "macos": "\(os.majorVersion).\(os.minorVersion)" + (os.patchVersion > 0 ? ".\(os.patchVersion)" : ""), "macosMajor": os.majorVersion,
                "rosetta": rosetta]
    }

    func gameVersion() -> String? {
        // build_manifest.id is a small JSON file the publisher's launcher writes next to the game
        guard let d = try? Data(contentsOf: live.appendingPathComponent("build_manifest.id")),
              let j = try? JSONSerialization.jsonObject(with: d) as? JSON, let data = j["Data"] as? JSON else { return nil }
        let branch = (data["Branch"] as? String) ?? ""; let cl = (data["RequestedP4ChangeNum"] as? String) ?? ""
        let v = branch.replacingOccurrences(of: "sc-alpha-", with: "")
        return v.isEmpty ? nil : (cl.isEmpty ? v : "\(v) (\(cl))")
    }

    /// Game folders this Mac already has outside our own prefix: CrossOver bottles (the one layout that was tried).
    func foundGames() -> [URL] {
        var out: [URL] = []
        let home = fm.homeDirectoryForCurrentUser
        let roots = [home.appendingPathComponent("Library/Application Support/CrossOver/Bottles")]
        for r in roots {
            for b in (try? fm.contentsOfDirectory(at: r, includingPropertiesForKeys: nil)) ?? [] {
                let g = b.appendingPathComponent("drive_c/Program Files/Roberts Space Industries/StarCitizen")
                let p4k = g.appendingPathComponent("LIVE/Data.p4k")
                let isLink = (try? g.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) ?? false
                if !isLink, exists(p4k) { out.append(g) }
            }
        }
        return out
    }

    func profileName() -> String {
        guard let s = try? String(contentsOf: live.appendingPathComponent("user.cfg"), encoding: .utf8) else { return "" }
        for line in s.split(separator: "\n") where line.hasPrefix("-- >>> profile:") {
            return line.dropFirst(15).trimmingCharacters(in: .whitespaces).split(separator: " ").first.map(String.init) ?? ""
        }
        return ""
    }

    func stepStates() -> [JSON] {
        let m = machine()
        var steps: [JSON] = []
        func add(_ id: String, _ title: String, _ state: StepState, _ detail: String, _ extra: JSON = [:]) {
            var s: JSON = ["id": id, "title": title, "state": state.rawValue, "detail": detail]
            for (k, v) in extra { s[k] = v }
            steps.append(s)
        }
        // 1 the Mac
        let silicon = (m["appleSilicon"] as? Bool) ?? false, rosetta = (m["rosetta"] as? Bool) ?? false
        if !silicon { add("mac", "This Mac", .failed, "needs a Mac with Apple silicon") }
        else if !rosetta { add("mac", "This Mac", .hold, "Rosetta 2 is not installed", ["action": "rosetta"]) }
        else { add("mac", "This Mac", .done, "\(m["chip"] ?? ""), \(m["memoryGB"] ?? 0) GB, macOS \(m["macos"] ?? "")") }
        // 2 Wine
        add("wine", "Wine", exists(wine) ? .done : .todo, exists(wine) ? "WineHQ 11.0, the official Mac build" : "official WineHQ build, 176 MB")
        // 3 DXVK
        let dx = exists(dxvkDir.appendingPathComponent("d3d11.dll"))
        add("dxvk", "DXVK", dx ? .done : .todo, dx ? "1.10.3 for macOS" : "Direct3D on Vulkan, 3 MB")
        // 4 kit
        let kitOK = exists(kitDir.appendingPathComponent("libMoltenVK.real.dylib")) && exists(kitDir.appendingPathComponent("libwinegrid.dylib"))
            && ((try? String(contentsOf: kitDir.appendingPathComponent("version"), encoding: .utf8)) ?? "") == Engine.version
        add("kit", "Kit parts", kitOK ? .done : .todo, kitOK ? "MoltenVK, shim, winegrid, squeezer" : "this project's own fixes")
        // 5 prefix
        let pre = exists(prefix.appendingPathComponent("system.reg")) && exists(driveC.appendingPathComponent("kit/dismiss.exe"))
            && exists(system32.appendingPathComponent("d3d11.dll.wine"))
        add("prefix", "Windows folder", pre ? .done : .todo, pre ? "Windows 11 settings, DXVK and the Vulkan wrapper in place" : "a private Windows folder for the game")
        // 6 game (before the launcher: an existing game makes the publisher's launcher optional)
        let haveGame = exists(live.appendingPathComponent("Bin64/StarCitizen.exe")) && exists(live.appendingPathComponent("Data.p4k"))
        let found = haveGame ? [] : foundGames()
        // 7 the publisher's launcher
        let rsi = exists(rsiLauncherExe)
        add("rsi", "Publisher's launcher", rsi ? .done : (haveGame || !found.isEmpty ? .done : .todo),
            rsi ? "installed" : (haveGame || !found.isEmpty ? "not needed: the game is already here" : "RSI Launcher, 345 MB, from the publisher"), ["installed": rsi])
        if haveGame { add("game", "Game", .done, "Star Citizen \(gameVersion() ?? "LIVE")") }
        else if let g = found.first { add("game", "Game", .hold, "found in the \(g.pathComponents.dropLast(4).last ?? "other") bottle", ["action": "import", "found": g.path]) }
        else if rsi { add("game", "Game", .hold, "sign in to the publisher's launcher and press Install there", ["action": "signin"]) }
        else { add("game", "Game", .todo, "about 130 GB, downloaded by the publisher's launcher after you sign in") }
        // 8 mod
        let modOK = exists(modDir.appendingPathComponent("sc-offline.exe")) && exists(modDir.appendingPathComponent("dinput8.dll"))
        add("mod", "sc-offline mod", modOK ? .done : .todo, modOK ? "sc-offline \(Engine.modVersion)" : "release \(Engine.modVersion), 3 MB, from its GitHub page")
        // 9 macfix and profile
        let tune = exists(modDir.appendingPathComponent("data/plugins/macfix/macfix.dll")) && exists(live.appendingPathComponent("user.cfg"))
        add("tune", "macfix and profile", tune && modOK && haveGame ? .done : .todo, tune ? "profile: \(profileName().isEmpty ? "custom" : profileName())" : "the mod's Mac plugin and a graphics profile")
        return steps
    }

    func status() -> JSON {
        let steps = stepStates()
        let ready = steps.allSatisfy { ($0["state"] as? String) == "done" }
        let pid = gamePid()
        return ["type": "status", "version": Engine.version, "machine": machine(), "steps": steps, "ready": ready,
                "gameRunning": pid != nil, "launcherOpen": launcherTask?.isRunning ?? false, "busy": busy,
                "profile": profileName(), "profiles": profiles(), "root": root.path,
                "muted": isMuted(), "tested": ["mac": "MacBook Pro 16-inch, M4 Pro", "memory": "24 GB", "macos": "26.2", "game": "LIVE 4.10.2", "mod": "sc-offline 0.7.7"]]
    }

    // MARK: setup
    struct Fail: Error { let message: String; init(_ m: String) { message = m } }

    /// Runs every step that can run by itself, in order, and stops at the first one that needs the person.
    func setup(only: String? = nil) {
        work.async { [self] in
            lock.lock(); if busy { lock.unlock(); return }; busy = true; lock.unlock()
            defer { lock.lock(); busy = false; lock.unlock(); emit(status()) }
            emit(["type": "setup", "state": "started"])
            for s in stepStates() {
                let id = s["id"] as! String, state = s["state"] as! String
                if let only = only, only != id { continue }
                if state == "done" { continue }
                if state == "hold" || state == "failed" { emit(["type": "setup", "state": "hold", "at": id]); return }
                do {
                    if id != "game" { say(id, .running, "starting") }
                    switch id {
                    case "wine": try stepWine()
                    case "dxvk": try stepDXVK()
                    case "kit": try stepKit()
                    case "prefix": try stepPrefix()
                    case "rsi": try stepRSI()
                    case "game": emit(["type": "setup", "state": "hold", "at": id]); return
                    case "mod": try stepMod()
                    case "tune": try stepTune()
                    default: break
                    }
                    let now = stepStates().first { ($0["id"] as? String) == id }
                    emit((now ?? [:]).merging(["type": "step"]) { a, _ in a })
                } catch let f as Fail {
                    say(id, .failed, f.message); emit(["type": "setup", "state": "failed", "at": id, "message": f.message]); return
                } catch {
                    say(id, .failed, error.localizedDescription); emit(["type": "setup", "state": "failed", "at": id, "message": error.localizedDescription]); return
                }
            }
            emit(["type": "setup", "state": "finished"])
        }
    }

    func stepWine() throws {
        let f = try fetch(step: "wine", what: "Wine 11.0", url: Engine.wine.url, file: Engine.wine.file, sha256: Engine.wine.sha256)
        say("wine", .running, "unpacking")
        try? fm.removeItem(at: engineDir); try fm.createDirectory(at: engineDir, withIntermediateDirectories: true)
        let r = run("/usr/bin/tar", ["-xJf", f.path, "-C", engineDir.path])
        guard r.code == 0, exists(wine) else { throw Fail("Wine: the archive did not unpack") }
        run("/usr/bin/xattr", ["-dr", "com.apple.quarantine", engineDir.path])
        try? fm.removeItem(at: f)
    }

    func stepDXVK() throws {
        let f = try fetch(step: "dxvk", what: "DXVK", url: Engine.dxvk.url, file: Engine.dxvk.file, sha256: Engine.dxvk.sha256)
        let tmp = downloads.appendingPathComponent("dxvk-unpack"); try? fm.removeItem(at: tmp); try fm.createDirectory(at: tmp, withIntermediateDirectories: true)
        guard run("/usr/bin/tar", ["-xzf", f.path, "-C", tmp.path]).code == 0 else { throw Fail("DXVK: the archive did not unpack") }
        try fm.createDirectory(at: dxvkDir, withIntermediateDirectories: true)
        let stamp = Array("Wine builtin DLL".utf8)
        for name in ["d3d11.dll", "d3d10core.dll"] {
            guard let src = fm.enumerator(at: tmp, includingPropertiesForKeys: nil)?.compactMap({ $0 as? URL }).first(where: { $0.path.hasSuffix("x86_64-windows/\(name)") }),
                  var d = try? Data(contentsOf: src) else { throw Fail("DXVK: \(name) is not in the archive") }
            // Wine loads a file carrying this stamp from its own folders only; without the stamp it is an ordinary DLL.
            if d.count > 0x60, Array(d[0x40..<0x40 + stamp.count]) == stamp { for i in 0..<stamp.count { d[0x40 + i] = 0 } }
            try d.write(to: dxvkDir.appendingPathComponent(name))
        }
        try? fm.removeItem(at: tmp); try? fm.removeItem(at: f)
    }

    func stepKit() throws {
        try? fm.removeItem(at: kitDir); try fm.createDirectory(at: kitDir, withIntermediateDirectories: true)
        for name in ["libMoltenVK.dylib", "libMoltenVK.real.dylib", "libwinegrid.dylib", "balloon"] {
            let src = kitSource.appendingPathComponent(name)
            guard exists(src) else { throw Fail("kit: \(name) is missing from this build of the launcher") }
            try fm.copyItem(at: src, to: kitDir.appendingPathComponent(name))
        }
        try? fm.createSymbolicLink(atPath: kitDir.appendingPathComponent("libvulkan.1.dylib").path, withDestinationPath: "libMoltenVK.dylib")
        run("/usr/bin/xattr", ["-dr", "com.apple.quarantine", kitDir.path])
        try Engine.version.write(to: kitDir.appendingPathComponent("version"), atomically: true, encoding: .utf8)
    }

    func stepPrefix() throws {
        if !exists(prefix.appendingPathComponent("system.reg")) {
            say("prefix", .running, "creating the Windows folder")
            try fm.createDirectory(at: prefix, withIntermediateDirectories: true)
            wineRun(["wineboot", "-u"]); serverWait()
            guard exists(prefix.appendingPathComponent("system.reg")) else { throw Fail("Wine could not create the Windows folder") }
        }
        say("prefix", .running, "adding DXVK and the Vulkan wrapper")
        for name in ["d3d11.dll", "d3d10core.dll"] {
            let dst = system32.appendingPathComponent(name), keep = system32.appendingPathComponent(name + ".wine")
            if !exists(keep) { try fm.copyItem(at: dst, to: keep) }
            try? fm.removeItem(at: dst); try fm.copyItem(at: dxvkDir.appendingPathComponent(name), to: dst)
        }
        let vk = system32.appendingPathComponent("vulkan-1.dll"), vkKeep = system32.appendingPathComponent("vulkan-1.dll.wine")
        if !exists(vkKeep) { try fm.copyItem(at: vk, to: vkKeep) }
        try? fm.removeItem(at: vk); try fm.copyItem(at: kitSource.appendingPathComponent("vulkan-1.dll"), to: vk)
        let kit = driveC.appendingPathComponent("kit"); try fm.createDirectory(at: kit, withIntermediateDirectories: true)
        for name in ["dismiss.exe"] { let d = kit.appendingPathComponent(name); try? fm.removeItem(at: d); try fm.copyItem(at: kitSource.appendingPathComponent(name), to: d) }
        try fm.createDirectory(at: kitUser, withIntermediateDirectories: true)
        say("prefix", .running, "writing settings")
        // One registry file: load order for the wrapper, the adapter identity Wine's DXGI reports (it must match what the
        // shim gives the Vulkan side), Windows 11.
        let reg = """
        Windows Registry Editor Version 5.00

        [HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides]
        "vulkan-1"="native,builtin"
        "d3d11"="native,builtin"
        "d3d10core"="native,builtin"

        [HKEY_CURRENT_USER\\Software\\Wine\\Direct3D]
        "VideoPciVendorID"=dword:00001002
        "VideoPciDeviceID"=dword:000066af

        """
        let regFile = kit.appendingPathComponent("setup.reg")
        try reg.replacingOccurrences(of: "\n", with: "\r\n").write(to: regFile, atomically: true, encoding: .utf8)
        wineRun(["regedit", "/S", "C:\\kit\\setup.reg"])
        wineRun(["winecfg", "/v", "win11"])
        serverWait()
    }

    // The publisher's launcher: fetched from the publisher's own update feed and checked against the digest in that feed.
    func stepRSI() throws {
        if exists(live.appendingPathComponent("Data.p4k")) || !foundGames().isEmpty { return }      // the game is here: optional
        say("rsi", .running, "asking the publisher for its version")
        guard let feedURL = URL(string: Engine.rsiFeed), let feed = try? String(contentsOf: feedURL, encoding: .utf8) else { throw Fail("could not reach the publisher's download site") }
        func field(_ k: String) -> String? {
            for line in feed.split(separator: "\n") { let t = line.trimmingCharacters(in: .whitespaces); if t.hasPrefix(k + ":") { return t.dropFirst(k.count + 1).trimmingCharacters(in: CharacterSet(charactersIn: " '\"")) } }
            return nil
        }
        guard let name = field("path"), let digest = field("sha512") else { throw Fail("the publisher's update feed has an unexpected format") }
        let f = try fetch(step: "rsi", what: "RSI Launcher", url: Engine.rsiBase + name, file: name, sha512b64: digest)
        try installRSI(from: f)
    }

    /// The publisher's installer does not complete under Wine (it stops at the step that asks Windows for administrator
    /// rights). What it would install is one 7z archive stored inside it, so that archive is cut out and unpacked into
    /// the place the installer would have used. macOS's own tar reads 7z.
    func installRSI(from installer: URL) throws {
        say("rsi", .running, "unpacking the publisher's launcher")
        guard let data = try? Data(contentsOf: installer, options: .alwaysMapped) else { throw Fail("cannot read the publisher's installer") }
        let magic = Data([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c])
        var best: (start: Int, length: Int)?
        var from = data.startIndex
        while let r = data.range(of: magic, options: [], in: from..<data.endIndex) {
            let i = r.lowerBound
            if i + 32 <= data.endIndex, data[i + 6] == 0 {
                // a 7z file states where its closing header is: the archive's length is 32 + that offset + that header's size
                let off = data.subdata(in: (i + 12)..<(i + 20)).withUnsafeBytes { $0.loadUnaligned(as: UInt64.self) }
                let size = data.subdata(in: (i + 20)..<(i + 28)).withUnsafeBytes { $0.loadUnaligned(as: UInt64.self) }
                let total = 32 &+ off &+ size
                if total > 1_000_000, total <= UInt64(data.endIndex - i), Int(total) > (best?.length ?? 0) { best = (i, Int(total)) }
            }
            from = i + 1
        }
        guard let found = best else { throw Fail("the publisher's installer has an unexpected layout (no archive found inside)") }
        let archive = downloads.appendingPathComponent("rsi-launcher.7z"); try? fm.removeItem(at: archive)
        try data.subdata(in: found.start..<(found.start + found.length)).write(to: archive)
        let dest = rsiHome.appendingPathComponent("RSI Launcher")
        try? fm.removeItem(at: dest); try fm.createDirectory(at: dest, withIntermediateDirectories: true)
        guard run("/usr/bin/tar", ["-xf", archive.path, "-C", dest.path]).code == 0, exists(rsiLauncherExe) else {
            try? fm.removeItem(at: dest); throw Fail("the launcher's files did not unpack from the publisher's installer")
        }
        try? fm.removeItem(at: archive); try? fm.removeItem(at: installer)
    }

    func stepMod() throws {
        let f = try fetch(step: "mod", what: "sc-offline \(Engine.modVersion)", url: Engine.mod.url, file: Engine.mod.file, sha256: Engine.mod.sha256)
        say("mod", .running, "unpacking")
        let tmp = downloads.appendingPathComponent("mod-unpack"); try? fm.removeItem(at: tmp)
        guard run("/usr/bin/ditto", ["-x", "-k", f.path, tmp.path]).code == 0 else { throw Fail("the mod's archive did not unpack") }
        // the archive holds either the files or one folder with them
        var src = tmp
        if !exists(src.appendingPathComponent("sc-offline.exe")), let inner = (try? fm.contentsOfDirectory(at: tmp, includingPropertiesForKeys: nil))?.first(where: { exists($0.appendingPathComponent("sc-offline.exe")) }) { src = inner }
        guard exists(src.appendingPathComponent("sc-offline.exe")) else { throw Fail("the mod's archive has an unexpected layout") }
        try fm.createDirectory(at: kitUser, withIntermediateDirectories: true)
        try? fm.removeItem(at: modDir); try fm.moveItem(at: src, to: modDir)
        try? fm.removeItem(at: tmp); try? fm.removeItem(at: f)
    }

    func stepTune() throws {
        guard exists(modDir.appendingPathComponent("sc-offline.exe")) else { throw Fail("the mod is not installed yet") }
        say("tune", .running, "installing the macfix plugin")
        let plug = modDir.appendingPathComponent("data/plugins/macfix")
        let firstTime = !exists(plug.appendingPathComponent("macfix.dll"))
        try fm.createDirectory(at: plug, withIntermediateDirectories: true)
        for name in ["macfix.dll", "plugin.ini"] {
            let d = plug.appendingPathComponent(name), tmp = plug.appendingPathComponent(name + ".new")
            try? fm.removeItem(at: tmp); try fm.copyItem(at: kitSource.appendingPathComponent(name), to: tmp)
            _ = try? fm.replaceItemAt(d, withItemAt: tmp)                       // by rename: a running game may have the old file mapped
            if !exists(d) { try fm.moveItem(at: tmp, to: d) }
        }
        let ctl = kitUser.appendingPathComponent("macfix.ctl")
        if !exists(ctl) { try "padfix=1 fixgame=1 keepalive=50 clip=1 diag=0 status=60\n".write(to: ctl, atomically: true, encoding: .utf8) }
        // the mod's settings: the game's place, offline, no update checks
        let ini = modDir.appendingPathComponent("sc-offline.ini")
        var text = (try? String(contentsOf: ini, encoding: .utf8)) ?? ""
        func set(_ key: String, _ value: String) {
            var lines = text.components(separatedBy: "\n"); var hit = false
            for i in lines.indices { let t = lines[i].trimmingCharacters(in: .whitespaces)
                if t.hasPrefix(key + " =") || t.hasPrefix(key + "=") { lines[i] = "\(key) = \(value)"; hit = true } }
            if !hit { lines.append("\(key) = \(value)") }
            text = lines.joined(separator: "\n")
        }
        set("game", Engine.gameWin); set("channel", "LIVE"); set("check_updates", "off"); set("block_network", "on")
        // the mod's own default boots every star system; the kit was measured with one (less memory), so that is the start
        if firstTime || !text.contains("boot_map") { set("boot_map", "PU") }
        try text.write(to: ini, atomically: true, encoding: .utf8)
        if exists(live), profileName().isEmpty { try applyProfile("pretty") }
    }

    // MARK: graphics profiles (the kit's profiles folder: <name>.cfg for user.cfg, <name>.cvars for macfix)
    func profiles() -> [String] {
        ((try? fm.contentsOfDirectory(atPath: kitSource.appendingPathComponent("profiles").path)) ?? []).filter { $0.hasSuffix(".cfg") }.map { String($0.dropLast(4)) }.sorted()
    }

    func applyProfile(_ name: String) throws {
        let dir = kitSource.appendingPathComponent("profiles")
        guard let body = try? String(contentsOf: dir.appendingPathComponent("\(name).cfg"), encoding: .utf8) else { throw Fail("no such profile: \(name)") }
        let cfg = live.appendingPathComponent("user.cfg")
        var s = (try? String(contentsOf: cfg, encoding: .utf8)) ?? ""
        if s.isEmpty { s = (try? String(contentsOf: kitSource.appendingPathComponent("user.cfg.base"), encoding: .utf8)) ?? "" }
        if let a = s.range(of: "-- >>> profile:") { s = String(s[..<a.lowerBound]) }
        s = s.trimmingCharacters(in: .newlines) + "\n-- >>> profile: \(name) (written by the launcher; the profiles are in the kit's profiles folder)\n" + body.trimmingCharacters(in: .newlines) + "\n-- <<< profile\n"
        try s.write(to: cfg, atomically: true, encoding: .utf8)
        let cvars = kitUser.appendingPathComponent("macfix.cvars"), src = dir.appendingPathComponent("\(name).cvars")
        try? fm.removeItem(at: cvars)
        if exists(src) { try fm.copyItem(at: src, to: cvars) }
    }

    func isMuted() -> Bool {
        guard let s = try? String(contentsOf: prefix.appendingPathComponent("user.reg"), encoding: .utf8), let r = s.range(of: "[Software\\\\Wine\\\\Drivers]") else { return false }
        let tail = s[r.upperBound...].prefix(300)
        return tail.contains("\"Audio\"=\"\"")
    }
    func setMuted(_ on: Bool) {
        if on { wineRun(["reg", "add", "HKCU\\Software\\Wine\\Drivers", "/v", "Audio", "/t", "REG_SZ", "/d", "", "/f"]) }
        else { wineRun(["reg", "delete", "HKCU\\Software\\Wine\\Drivers", "/v", "Audio", "/f"]) }
        serverWait(); emit(status())
    }

    // MARK: adopting a game this Mac already has
    /// Moves the game (and the mod, the publisher's launcher and the game's own per-user data if they are in the same
    /// bottle) into this setup. On the same disk this is a rename and takes no extra space. A link is left behind so the
    /// old bottle still finds the game.
    func importGame(from path: String) {
        work.async { [self] in
            lock.lock(); if busy { lock.unlock(); return }; busy = true; lock.unlock()
            defer { lock.lock(); busy = false; lock.unlock(); emit(status()) }
            do {
                let src = URL(fileURLWithPath: path)
                guard exists(src.appendingPathComponent("LIVE/Data.p4k")) else { throw Fail("no game in \(path)") }
                guard exists(prefix.appendingPathComponent("system.reg")) else { throw Fail("the Windows folder is not ready yet") }
                guard gamePid() == nil else { throw Fail("the game is running; stop it first") }
                say("game", .running, "moving the game here")
                let oldC = src.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()     // …/drive_c
                let sameDisk = (try? src.resourceValues(forKeys: [.volumeIdentifierKey]).volumeIdentifier as? NSObject) == (try? root.resourceValues(forKeys: [.volumeIdentifierKey]).volumeIdentifier as? NSObject)
                guard sameDisk else { throw Fail("that game is on another disk; this version only adopts a game on the same disk") }
                try fm.createDirectory(at: rsiHome, withIntermediateDirectories: true)
                if exists(gameDir) { try fm.removeItem(at: gameDir) }      // only ever an empty folder or a stale link here: haveGame was false
                try fm.moveItem(at: src, to: gameDir)
                try? fm.createSymbolicLink(at: src, withDestinationURL: gameDir)
                // the publisher's launcher and its own settings (so that Verify and updates keep working without a new install)
                let oldRSI = src.deletingLastPathComponent().appendingPathComponent("RSI Launcher")
                if exists(oldRSI.appendingPathComponent("RSI Launcher.exe")), !exists(rsiLauncherExe) {
                    say("game", .running, "bringing the launcher along")
                    let newRSI = rsiHome.appendingPathComponent("RSI Launcher"); try? fm.removeItem(at: newRSI)
                    try fm.moveItem(at: oldRSI, to: newRSI); try? fm.createSymbolicLink(at: oldRSI, withDestinationURL: newRSI)
                }
                for user in (try? fm.contentsOfDirectory(at: oldC.appendingPathComponent("users"), includingPropertiesForKeys: nil)) ?? [] where user.lastPathComponent != "Public" {
                    let oldMod = user.appendingPathComponent("sc-offline")
                    if exists(oldMod.appendingPathComponent("sc-offline.exe")), !exists(modDir.appendingPathComponent("sc-offline.exe")) {
                        say("game", .running, "bringing the mod along")
                        try fm.createDirectory(at: kitUser, withIntermediateDirectories: true)
                        try? fm.removeItem(at: modDir); try fm.copyItem(at: oldMod, to: modDir)
                        for f in ["macfix.ctl", "macfix.cvars"] where exists(user.appendingPathComponent(f)) && !exists(kitUser.appendingPathComponent(f)) { try? fm.copyItem(at: user.appendingPathComponent(f), to: kitUser.appendingPathComponent(f)) }
                    }
                    let oldData = user.appendingPathComponent("AppData/Local/star citizen"), newData = winUser.appendingPathComponent("AppData/Local/star citizen")
                    if exists(oldData), !exists(newData) {
                        say("game", .running, "bringing settings and shaders along")
                        try fm.createDirectory(at: newData.deletingLastPathComponent(), withIntermediateDirectories: true)
                        try fm.moveItem(at: oldData, to: newData); try? fm.createSymbolicLink(at: oldData, withDestinationURL: newData)
                    }
                    let oldLauncher = user.appendingPathComponent("AppData/Roaming/rsilauncher"), newLauncher = winUser.appendingPathComponent("AppData/Roaming/rsilauncher")
                    if exists(oldLauncher), !exists(newLauncher) {
                        try fm.createDirectory(at: newLauncher.deletingLastPathComponent(), withIntermediateDirectories: true)
                        try fm.moveItem(at: oldLauncher, to: newLauncher); try? fm.createSymbolicLink(at: oldLauncher, withDestinationURL: newLauncher)
                    }
                }
                say("game", .done, "Star Citizen \(gameVersion() ?? "LIVE")")
            } catch let f as Fail { say("game", .failed, f.message) } catch { say("game", .failed, error.localizedDescription) }
        }
    }

    // MARK: the publisher's launcher (sign-in and download are the person's own; the game is never started from it)
    static let rsiFlags = ["--use-gl=angle", "--use-angle=swiftshader", "--in-process-gpu", "--disable-features=CalculateNativeWinOcclusion"]

    func openLauncher() {
        work.async { [self] in
            guard exists(rsiLauncherExe) else { emit(["type": "launcher", "state": "missing"]); return }
            guard gamePid() == nil else { emit(["type": "launcher", "state": "refused", "message": "the game is running"]); return }
            // A game folder with the mod in it must never meet the publisher's servers: take the mod out first.
            cleanMod()
            guard !modInGameFolder() else { emit(["type": "launcher", "state": "refused", "message": "the mod is still in the game folder"]); return }
            serverKill()
            let p = Process(); p.executableURL = wine
            p.arguments = ["C:\\Program Files\\Roberts Space Industries\\RSI Launcher\\RSI Launcher.exe"] + Engine.rsiFlags
            var e = wineEnv(); e["WINEDLLOVERRIDES"] = "winemenubuilder.exe=d;mscoree=d;mshtml=d"       // nothing of the kit is loaded into the launcher
            p.environment = e
            p.standardInput = FileHandle.nullDevice; p.standardOutput = FileHandle.nullDevice; p.standardError = FileHandle.nullDevice
            p.terminationHandler = { [weak self] _ in self?.emit(["type": "launcher", "state": "closed"]); self.map { $0.emit($0.status()) } }
            do { try p.run(); launcherTask = p; emit(["type": "launcher", "state": "open"]); watchLauncher() } catch { emit(["type": "launcher", "state": "failed", "message": error.localizedDescription]) }
        }
    }

    func closeLauncher() { work.async { [self] in serverKill(); launcherTask = nil; emit(["type": "launcher", "state": "closed"]); emit(status()) } }

    /// While the publisher's launcher is open: follow its log for the phases of an install and report the game folder's size.
    private func watchLauncher() {
        DispatchQueue.global().async { [self] in
            var offset: UInt64 = (try? fm.attributesOfItem(atPath: rsiLog.path)[.size] as? UInt64) ?? 0
            var phase = "waiting for you to sign in and press Install"
            var total: Double = 0
            while launcherTask?.isRunning ?? false {
                if let h = try? FileHandle(forReadingFrom: rsiLog) {
                    let size = (try? h.seekToEnd()) ?? 0
                    if size < offset { offset = 0 }
                    try? h.seek(toOffset: offset); let d = h.readDataToEndOfFile(); offset += UInt64(d.count); try? h.close()
                    for line in String(decoding: d, as: UTF8.self).split(separator: "\n") {
                        if line.contains("[Pipeline] Installing") { phase = "the publisher's launcher is preparing the install" }
                        else if line.contains("Calculating required space") { phase = "calculating the space needed" }
                        else if line.contains("[InitialDownloadPhase] Starting download") { phase = "downloading the game" }
                        else if line.contains("Initial download done") { phase = "download done, applying the last changes" }
                        else if line.contains("[PatcherPhase] Starting delta update") { phase = "applying the last changes" }
                        else if line.contains("[ShaderCompilationPhase]") || line.contains("PsoCacheGen") { phase = "the publisher's launcher is finishing" }
                    }
                }
                let p4k = live.appendingPathComponent("Data.p4k")
                // a sparse file is created at full size first: its allocated size is what has arrived
                let alloc = Double((try? p4k.resourceValues(forKeys: [.fileAllocatedSizeKey]).fileAllocatedSize) ?? 0)
                let full = Double((try? p4k.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
                if full > total { total = full }
                var e: JSON = ["type": "download", "phase": phase, "gb": alloc / 1e9]
                if total > 0 { e["totalGB"] = total / 1e9; e["progress"] = min(1, alloc / total) }
                emit(e)
                sleep(3)
            }
        }
    }

    // MARK: single player
    static let noNetwork = """
    (version 1)
    (allow default)
    (deny network*)
    (allow network* (subpath "/private/tmp"))
    (allow network* (subpath "/private/var/folders"))
    (allow network-outbound (remote ip "localhost:*"))
    (allow network-inbound (local ip "localhost:*"))
    """

    func play() {
        work.async { [self] in
            guard gamePid() == nil, gameTask == nil else { emit(["type": "flight", "phase": "refused", "message": "the game is already running"]); return }
            guard stepStates().allSatisfy({ ($0["state"] as? String) == "done" }) else { emit(["type": "flight", "phase": "refused", "message": "setup is not finished"]); return }
            emit(["type": "flight", "phase": "start", "t": 0])
            serverKill(); launcherTask = nil
            try? fm.createDirectory(at: logs, withIntermediateDirectories: true)
            let oldStamp = (try? fm.attributesOfItem(atPath: gameLog.path)[.modificationDate] as? Date) ?? .distantPast
            // the dialog clicker: presses OK on the message boxes the game raises while starting, and logs their text
            DispatchQueue.global().asyncAfter(deadline: .now() + 10) { [self] in wineRun(["C:\\kit\\dismiss.exe", "C:\\kit\\dismiss.log"]) }
            let p = Process(); p.executableURL = URL(fileURLWithPath: "/usr/bin/sandbox-exec")
            // The DYLD variables must reach Wine only. sandbox-exec and env are system programs built for another
            // architecture slice: given the variables directly they refuse to start. So they get a plain environment
            // and env sets the rest for Wine.
            let env = wineEnv(forGame: true)
            p.arguments = ["-p", Engine.noNetwork, "/usr/bin/env"] + env.map { "\($0.key)=\($0.value)" } + [wine.path, Engine.modWin, "play", "--game", Engine.gameWin]
            p.environment = env.filter { !$0.key.hasPrefix("DYLD_") }
            let out = logs.appendingPathComponent("play.log"); fm.createFile(atPath: out.path, contents: nil)
            let oh = try? FileHandle(forWritingTo: out)
            p.standardInput = FileHandle.nullDevice; p.standardOutput = oh ?? FileHandle.nullDevice; p.standardError = oh ?? FileHandle.nullDevice
            p.terminationHandler = { [weak self] _ in self?.flightEnded() }
            do { try p.run() } catch { emit(["type": "flight", "phase": "failed", "message": error.localizedDescription]); return }
            gameTask = p
            startTelemetry(since: oldStamp)
        }
    }

    func stop() {
        work.async { [self] in
            if let pid = gamePid() { kill(pid, SIGTERM); for _ in 0..<16 { if gamePid() == nil { break }; usleep(500_000) } }
            for _ in 0..<16 { if !modInGameFolder() { break }; usleep(500_000) }      // the mod removes itself when the game ends
            serverKill()
            cleanMod()
            emit(["type": "stopped"])
        }
    }

    /// The mod leaves a marker in the game folder while it is installed there.
    func modInGameFolder() -> Bool { exists(live.appendingPathComponent("Bin64/sc-offline.installed")) }

    /// Takes the mod out of the game folder if a stopped or crashed run left it there. The game folder is then the
    /// publisher's own again, which is what the publisher's launcher must find.
    func cleanMod() {
        guard modInGameFolder(), exists(modDir.appendingPathComponent("sc-offline.exe")), gamePid() == nil else { return }
        var e = wineEnv(); e["WINEDLLOVERRIDES"] = "winemenubuilder.exe=d;mscoree=d;mshtml=d;dinput8=n,b"
        run(wine.path, [Engine.modWin, "uninstall", "--game", Engine.gameWin], env: e); serverWait()
    }

    private func flightEnded() {
        telemetry?.cancel(); telemetry = nil
        squeezer?.terminate(); squeezer = nil
        gameTask = nil
        emit(["type": "flight", "phase": "ended"])
        work.async { [self] in cleanMod(); emit(status()) }
    }

    /// Called when the app opens: if the game is already flying (started earlier, or from the command line), follow it.
    func attachIfFlying() {
        work.async { [self] in
            guard telemetry == nil, gameTask == nil, gamePid() != nil else { return }
            let began = (try? fm.attributesOfItem(atPath: gameLog.path)[.creationDate] as? Date) ?? Date()
            startTelemetry(since: .distantPast, began: began)
        }
    }

    private func startTelemetry(since stamp: Date, began: Date? = nil) {
        let t0 = began ?? Date(); let attached = began != nil
        var logOffset: UInt64 = 0, shimOffset: UInt64 = (try? fm.attributesOfItem(atPath: Engine.shimLog.path)[.size] as? UInt64) ?? 0
        var stage = "boot", fresh = false, worldAt: Date?, fatal: String?, fps: Double = 0, lastDecomp: UInt64 = 0, lastT = Date(), pid: Int32?
        let stages: [(String, String)] = [("Renderer initialization", "renderer"), ("Loading level megamap", "level"), ("ShardSeeding Success", "seeded"), ("Exit Bed", "world"), ("SpawnClosetArea", "world")]
        let order = ["boot", "renderer", "level", "seeded", "world"]
        let timer = DispatchSource.makeTimerSource(queue: DispatchQueue(label: "engine.telemetry"))
        timer.schedule(deadline: .now() + 1, repeating: 2.0)
        timer.setEventHandler { [self] in
            if pid == nil { pid = gamePid() }
            // the game's own log, only once this run has rewritten it
            if attached { fresh = true; if pid != nil, gamePid() == nil { flightEnded(); return } }
            if !fresh, let m = try? fm.attributesOfItem(atPath: gameLog.path)[.modificationDate] as? Date, m > stamp, m > t0.addingTimeInterval(-1) { fresh = true; logOffset = 0 }
            if fresh, let h = try? FileHandle(forReadingFrom: gameLog) {
                try? h.seek(toOffset: logOffset); let d = h.readDataToEndOfFile(); logOffset += UInt64(d.count); try? h.close()
                let text = String(decoding: d, as: UTF8.self)
                for (needle, name) in stages where text.contains(needle) { if (order.firstIndex(of: name) ?? 0) > (order.firstIndex(of: stage) ?? 0) { stage = name } }
                if stage == "world", worldAt == nil { worldAt = Date(); startSqueezer(pid: pid) }
                if fatal == nil, let r = text.range(of: "Fatal Error Thrown: ") { fatal = String(text[r.upperBound...].prefix(160)).components(separatedBy: "\n").first; handleFatal(fatal ?? "") }
            }
            // frame rate: the shim writes one line every five seconds
            if let h = try? FileHandle(forReadingFrom: Engine.shimLog) {
                let size = (try? h.seekToEnd()) ?? 0; if size < shimOffset { shimOffset = 0 }
                try? h.seek(toOffset: shimOffset); let d = h.readDataToEndOfFile(); shimOffset += UInt64(d.count); try? h.close()
                for line in String(decoding: d, as: UTF8.self).split(separator: "\n") {
                    if let r = line.range(of: "frames: "), let v = Double(line[r.upperBound...].split(separator: " ").first ?? "") { fps = v }
                }
            }
            var e: JSON = ["type": "flight", "phase": stage, "t": Int(Date().timeIntervalSince(t0)), "fps": fps]
            if let w = worldAt { e["worldAfter"] = Int(w.timeIntervalSince(t0)) }
            if let pid = pid {
                var info = rusage_info_v4()
                let ok = withUnsafeMutablePointer(to: &info) { $0.withMemoryRebound(to: rusage_info_t?.self, capacity: 1) { proc_pid_rusage(pid, RUSAGE_INFO_V4, $0) } }
                if ok == 0 { e["gameGB"] = Double(info.ri_phys_footprint) / 1_073_741_824 }
            }
            var vm = vm_statistics64(); var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64>.size / MemoryLayout<integer_t>.size)
            let kr = withUnsafeMutablePointer(to: &vm) { $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count) } }
            if kr == KERN_SUCCESS {
                let page = Double(vm_kernel_page_size)
                e["compressedGB"] = Double(vm.compressor_page_count) * page / 1_073_741_824
                e["freeGB"] = Double(vm.free_count) * page / 1_073_741_824
                let now = Date(); let dt = now.timeIntervalSince(lastT)
                if lastDecomp > 0, dt > 0 { e["paging"] = Int(Double(vm.decompressions &- lastDecomp) / dt) }
                lastDecomp = vm.decompressions; lastT = now
            }
            var swap = xsw_usage(); var slen = MemoryLayout<xsw_usage>.size
            if sysctlbyname("vm.swapusage", &swap, &slen, nil, 0) == 0 { e["swapGB"] = Double(swap.xsu_used) / 1_073_741_824 }
            if let f = fatal { e["fatal"] = f }
            emit(e)
        }
        timer.resume(); telemetry = timer
    }

    /// The squeezer: once in the world, keeps data the game never touches again from crowding live data out of RAM.
    private func startSqueezer(pid: Int32?) {
        guard let pid = pid, squeezer == nil else { return }
        DispatchQueue.global().asyncAfter(deadline: .now() + 20) { [self] in
            guard gameTask != nil else { return }
            run("/usr/bin/pkill", ["-f", "dismiss.exe"])
            let p = Process(); p.executableURL = kitDir.appendingPathComponent("balloon")
            p.arguments = ["auto", "\(pid)", logs.appendingPathComponent("squeezer.log").path]
            p.standardOutput = FileHandle.nullDevice; p.standardError = FileHandle.nullDevice
            try? p.run(); squeezer = p
        }
    }

    private func handleFatal(_ text: String) {
        // the game's own crash handler writes its dump first; then the run is ended so nothing is left hanging
        DispatchQueue.global().asyncAfter(deadline: .now() + 25) { [self] in
            run("/usr/bin/pkill", ["-f", "CrashHandler.exe"]); if let pid = gamePid() { kill(pid, SIGKILL) }
        }
    }

    // MARK: Rosetta (Apple asks the person to agree to its licence, so this only opens Terminal with the command typed)
    func openRosettaInstall() {
        let script = "tell application \"Terminal\"\nactivate\ndo script \"softwareupdate --install-rosetta\"\nend tell"
        run("/usr/bin/osascript", ["-e", script])
    }
}

final class DownloadDelegate: NSObject, URLSessionDownloadDelegate {
    let progress: (Int64, Int64) -> Void, finished: (URL?, String?) -> Void
    private var last = Date.distantPast, done = false
    init(progress: @escaping (Int64, Int64) -> Void, finished: @escaping (URL?, String?) -> Void) { self.progress = progress; self.finished = finished }
    func urlSession(_ s: URLSession, downloadTask: URLSessionDownloadTask, didWriteData n: Int64, totalBytesWritten w: Int64, totalBytesExpectedToWrite t: Int64) {
        if Date().timeIntervalSince(last) > 0.25 { last = Date(); progress(w, t) }
    }
    func urlSession(_ s: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        if let http = downloadTask.response as? HTTPURLResponse, http.statusCode >= 400 { done = true; finished(nil, "the server answered \(http.statusCode)"); return }
        // the temporary file is deleted when this returns, so it is moved aside here
        let keep = location.deletingLastPathComponent().appendingPathComponent(UUID().uuidString)
        do { try FileManager.default.moveItem(at: location, to: keep); done = true; finished(keep, nil) } catch { done = true; finished(nil, error.localizedDescription) }
    }
    func urlSession(_ s: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        if let error = error, !done { done = true; finished(nil, error.localizedDescription) }
    }
}
