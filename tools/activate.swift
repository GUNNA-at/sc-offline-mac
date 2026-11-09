// activate <pid>: bring that process's windows to the front and print the pid that was in front before.
// Used to give the game keyboard focus for a few seconds (then called again with the printed pid to hand focus back).
import AppKit
let args = CommandLine.arguments
guard args.count > 1, let pid = Int32(args[1]) else { print("usage: activate <pid>"); exit(2) }
let before = NSWorkspace.shared.frontmostApplication?.processIdentifier ?? 0
guard let app = NSRunningApplication(processIdentifier: pid) else { print("no such app"); exit(1) }
let ok = app.activate(options: [.activateAllWindows, .activateIgnoringOtherApps])
var front: Int32 = 0
for _ in 0..<20 { usleep(100_000); front = NSWorkspace.shared.frontmostApplication?.processIdentifier ?? 0; if front == pid { break } }
print("\(before) requested=\(ok) front_now=\(front)")
