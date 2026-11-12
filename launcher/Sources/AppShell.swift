// AppShell.swift: the window. A web view shows the interface in Resources/ui; this file only carries messages
// between it and the engine. Nothing here knows how anything is set up or started.
import AppKit
import WebKit
import UniformTypeIdentifiers

final class AppShell: NSObject, NSApplicationDelegate, WKScriptMessageHandler, WKNavigationDelegate, NSWindowDelegate {
    let engine: Engine
    var window: NSWindow!
    var web: WKWebView!
    private var ready = false
    private var queued: [JSON] = []

    init(engine: Engine) { self.engine = engine }

    func applicationDidFinishLaunching(_ n: Notification) {
        let config = WKWebViewConfiguration()
        config.userContentController.add(self, name: "engine")
        config.preferences.setValue(true, forKey: "developerExtrasEnabled")
        web = WKWebView(frame: .zero, configuration: config)
        web.navigationDelegate = self
        web.setValue(false, forKey: "drawsBackground")
        let size = NSSize(width: 1180, height: 760)
        window = NSWindow(contentRect: NSRect(origin: .zero, size: size), styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView], backing: .buffered, defer: false)
        window.title = "sc-offline-mac"
        window.titlebarAppearsTransparent = true
        window.titleVisibility = .hidden
        window.backgroundColor = NSColor(srgbRed: 0x0d / 255, green: 0x0e / 255, blue: 0x11 / 255, alpha: 1)
        window.appearance = NSAppearance(named: .darkAqua)
        window.minSize = NSSize(width: 980, height: 680)
        window.contentView = web
        window.delegate = self
        window.center(); window.setFrameAutosaveName("launcher")
        window.makeKeyAndOrderFront(nil)
        buildMenu()
        NSApp.activate(ignoringOtherApps: true)

        engine.emit = { [weak self] e in DispatchQueue.main.async { self?.send(e) } }
        // SCOM_AUTOPLAY=1 presses START by itself two seconds after opening (for testing the window with a real flight)
        if ProcessInfo.processInfo.environment["SCOM_AUTOPLAY"] == "1" { DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [engine] in engine.play() } }
        let ui = Bundle.main.resourceURL!.appendingPathComponent("ui")
        web.loadFileURL(ui.appendingPathComponent("index.html"), allowingReadAccessTo: ui)
    }

    func buildMenu() {
        let main = NSMenu()
        let appItem = NSMenuItem(); main.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "About sc-offline-mac", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Hide sc-offline-mac", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        appMenu.addItem(withTitle: "Quit sc-offline-mac", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        let editItem = NSMenuItem(); main.addItem(editItem)
        let edit = NSMenu(title: "Edit")
        edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        editItem.submenu = edit
        let windowItem = NSMenuItem(); main.addItem(windowItem)
        let win = NSMenu(title: "Window")
        win.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        win.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        windowItem.submenu = win
        NSApp.mainMenu = main; NSApp.windowsMenu = win
    }

    func webView(_ w: WKWebView, didFinish navigation: WKNavigation!) {
        ready = true
        for e in queued { send(e) }; queued.removeAll()
        sendArt()
    }

    // The backdrop of the Play page can be a picture of the person's own. It is kept in the launcher's folder and
    // handed to the page as data; it never leaves this Mac.
    var artFile: URL { engine.root.appendingPathComponent("backdrop.jpg") }

    func sendArt() {
        guard let d = try? Data(contentsOf: artFile) else { send(["type": "art"]); return }
        send(["type": "art", "url": "data:image/jpeg;base64," + d.base64EncodedString()])
    }

    func pickArt() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image]; panel.allowsMultipleSelection = false; panel.canChooseDirectories = false
        panel.message = "Choose a picture for the Play page"
        panel.beginSheetModal(for: window) { [self] answer in
            guard answer == .OK, let url = panel.url, let image = NSImage(contentsOf: url) else { return }
            // no larger than the window can use, and stored as JPEG
            let longest = max(image.size.width, image.size.height), scale = min(1, 2400 / max(longest, 1))
            let size = NSSize(width: (image.size.width * scale).rounded(), height: (image.size.height * scale).rounded())
            guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width), pixelsHigh: Int(size.height), bitsPerSample: 8, samplesPerPixel: 4,
                                             hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return }
            rep.size = size
            NSGraphicsContext.saveGraphicsState(); NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
            image.draw(in: NSRect(origin: .zero, size: size), from: .zero, operation: .copy, fraction: 1)
            NSGraphicsContext.restoreGraphicsState()
            guard let jpeg = rep.representation(using: .jpeg, properties: [.compressionFactor: 0.84]) else { return }
            try? jpeg.write(to: artFile); sendArt()
        }
    }

    func send(_ e: JSON) {
        guard ready else { queued.append(e); return }
        guard let d = try? JSONSerialization.data(withJSONObject: e), let s = String(data: d, encoding: .utf8) else { return }
        web.evaluateJavaScript("window.launcher && window.launcher.receive(\(s))", completionHandler: nil)
    }

    // messages from the interface: {cmd: "...", ...}
    func userContentController(_ c: WKUserContentController, didReceive message: WKScriptMessage) {
        guard let m = message.body as? JSON, let cmd = m["cmd"] as? String else { return }
        switch cmd {
        case "status": DispatchQueue.global().async { [self] in let s = engine.status(); DispatchQueue.main.async { self.send(s) }; engine.attachIfFlying() }
        case "setup": engine.setup(only: m["step"] as? String)
        case "import": if let p = m["path"] as? String { engine.importGame(from: p) }
        case "play": engine.play()
        case "stop": engine.stop()
        case "openLauncher": engine.openLauncher()
        case "closeLauncher": engine.closeLauncher()
        case "rosetta": engine.openRosettaInstall()
        case "pickArt": pickArt()
        case "resetArt": try? FileManager.default.removeItem(at: artFile); sendArt()
        case "profile":
            if let name = m["name"] as? String { DispatchQueue.global().async { [self] in try? engine.applyProfile(name); let s = engine.status(); DispatchQueue.main.async { self.send(s) } } }
        case "mute": if let on = m["on"] as? Bool { DispatchQueue.global().async { [self] in engine.setMuted(on) } }
        case "reveal":
            let what = m["what"] as? String ?? "root"
            let url = what == "logs" ? engine.logs : what == "game" ? engine.live : engine.root
            NSWorkspace.shared.activateFileViewerSelecting([url])
        case "open": if let s = m["url"] as? String, let u = URL(string: s), u.scheme == "https" { NSWorkspace.shared.open(u) }
        default: break
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
