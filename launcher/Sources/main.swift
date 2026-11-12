// main.swift: one program, two fronts. With arguments it is a command-line tool that prints the engine's events as
// JSON lines; without, it opens the launcher window.
//
//   sc-offline-mac status            what is set up and what is not
//   sc-offline-mac setup [step]      run every automatic step (or one), stop where a person is needed
//   sc-offline-mac import <folder>   adopt a game folder this Mac already has (same disk: a rename)
//   sc-offline-mac play              single player: start the game offline, print telemetry until it exits
//   sc-offline-mac stop              stop the game
//   sc-offline-mac launcher          open the publisher's launcher (sign in and install there yourself)
//   sc-offline-mac profile <name>    graphics profile for the next start
//   sc-offline-mac wine <command>    run any Windows command in the launcher's Windows folder
import Foundation
import AppKit

func kitFolder() -> URL {
    if let env = ProcessInfo.processInfo.environment["SCOM_KIT"] { return URL(fileURLWithPath: env) }
    if let r = Bundle.main.resourceURL, FileManager.default.fileExists(atPath: r.appendingPathComponent("kit").path) { return r.appendingPathComponent("kit") }
    return URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent().appendingPathComponent("kit")
}

func printJSON(_ j: JSON) {
    guard let d = try? JSONSerialization.data(withJSONObject: j, options: [.sortedKeys]) else { return }
    FileHandle.standardOutput.write(d); FileHandle.standardOutput.write(Data([0x0a]))
}

let args = Array(CommandLine.arguments.dropFirst()).filter { !$0.hasPrefix("-psn") && !$0.hasPrefix("-NS") && !$0.hasPrefix("-Apple") }
if let command = args.first, !command.hasPrefix("-") {
    let engine = Engine(kitSource: kitFolder())
    let done = DispatchSemaphore(value: 0)
    engine.emit = { e in
        printJSON(e)
        let type = e["type"] as? String, state = e["state"] as? String, phase = e["phase"] as? String
        if type == "setup", ["finished", "hold", "failed"].contains(state ?? "") { done.signal() }
        if type == "flight", ["ended", "refused", "failed"].contains(phase ?? "") { done.signal() }
        if type == "launcher", ["closed", "missing", "refused", "failed"].contains(state ?? "") { done.signal() }
        if type == "stopped" { done.signal() }
        if type == "step", (e["id"] as? String) == "game", ["done", "failed"].contains(state ?? ""), command == "import" { done.signal() }
    }
    signal(SIGINT) { _ in exit(130) }
    switch command {
    case "status": printJSON(engine.status())
    case "setup": engine.setup(only: args.count > 1 ? args[1] : nil); done.wait()
    case "import":
        guard args.count > 1 else { print("import needs the game folder"); exit(2) }
        engine.importGame(from: args[1]); done.wait()
    case "play": engine.play(); done.wait()
    case "stop": engine.stop(); done.wait()
    case "launcher": engine.openLauncher(); done.wait()
    case "wine":                         // any Windows command in the launcher's Windows folder, for looking into things
        let r = engine.run(engine.wine.path, Array(args.dropFirst()), env: engine.wineEnv(), quiet: false); print(r.out, terminator: ""); engine.serverKill(); exit(r.code)
    case "profile":
        guard args.count > 1 else { print(engine.profiles().joined(separator: " ")); exit(0) }
        do { try engine.applyProfile(args[1]); print("profile \(args[1]) is set for the next start") } catch let f as Engine.Fail { print(f.message); exit(1) } catch { print(error); exit(1) }
    default: print("commands: status | setup [step] | import <folder> | play | stop | launcher | profile <name> | wine <command>"); exit(2)
    }
    exit(0)
}

let app = NSApplication.shared
let delegate = AppShell(engine: Engine(kitSource: kitFolder()))
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
