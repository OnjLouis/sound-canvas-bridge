import AppKit
import Darwin
import Foundation

enum MaintenanceError: LocalizedError {
    case failed(String)
    var errorDescription: String? { if case .failed(let text) = self { return text }; return nil }
}

let maximumReplyBytes = 3072
func replyData(_ result: [String: Any]) throws -> Data {
    let data = try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys])
    guard data.count <= maximumReplyBytes else {
        return try JSONSerialization.data(withJSONObject: ["status": "error", "message": "The update helper reply exceeded its limit. See the product page or installer log for details."], options: [.sortedKeys])
    }
    return data
}

struct BridgeRelease: Codable {
    let version: String
    let asset: String
    let download: String
    let page: String
}

enum InstallerLog {
    static func write(_ message: String) {
        guard geteuid() != 0 else { return }
        let fm = FileManager.default
        let folder = fm.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/OnjMusic/SoundCanvasBridge")
        do {
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            let file = folder.appendingPathComponent("Installer.log")
            if let size = try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize, size > 262144 {
                let old = folder.appendingPathComponent("Installer.previous.log")
                try? fm.removeItem(at: old); try fm.moveItem(at: file, to: old)
            }
            if !fm.fileExists(atPath: file.path) { fm.createFile(atPath: file.path, contents: nil) }
            let handle = try FileHandle(forWritingTo: file); defer { try? handle.close() }
            try handle.seekToEnd()
            try handle.write(contentsOf: Data((ISO8601DateFormatter().string(from: Date()) + " " + String(message.prefix(12000)) + "\n").utf8))
        } catch { }
    }
}

final class InstallerTextView: NSTextView {
    override func keyDown(with event: NSEvent) {
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if event.keyCode == 48 && (flags.isEmpty || flags == .shift) {
            if flags == .shift { window?.selectPreviousKeyView(nil) } else { window?.selectNextKeyView(nil) }
            return
        }
        super.keyDown(with: event)
    }
}

final class InstallerPanel: NSPanel {
    var dismiss: (() -> Void)?
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }
    override func cancelOperation(_ sender: Any?) { dismiss?() }
    override func performClose(_ sender: Any?) { dismiss?() }
    override func sendEvent(_ event: NSEvent) {
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if event.type == .keyDown && (event.keyCode == 53 && flags.isEmpty || event.keyCode == 13 && flags == .command) {
            dismiss?(); return
        }
        super.sendEvent(event)
    }
}

final class InstallerDialog: NSObject {
    let panel: InstallerPanel
    let text = InstallerTextView(frame: NSRect(x: 0, y: 0, width: 588, height: 250))
    let primary: NSButton
    var result = false
    let secondary: NSButton?
    private let detail: String

    init(title: String, message: String, primaryTitle: String, cancel: Bool = false, progress: Bool = false) {
        detail = message
        panel = InstallerPanel(contentRect: NSRect(x: 0, y: 0, width: 620, height: 350),
            styleMask: progress ? [.titled] : [.titled, .closable, .resizable], backing: .buffered, defer: false)
        primary = NSButton(title: primaryTitle, target: nil, action: nil)
        secondary = progress ? nil : NSButton(title: cancel ? "Cancel" : "Copy Details", target: nil, action: nil)
        super.init()
        panel.title = title; panel.isReleasedWhenClosed = false; panel.isExcludedFromWindowsMenu = true
        text.isEditable = false; text.isSelectable = true; text.isRichText = false
        text.font = .systemFont(ofSize: NSFont.systemFontSize); text.string = message
        text.setAccessibilityLabel(cancel ? "Installation instructions" : "Installation details")
        text.setAccessibilityHelp("Review this text with the arrow keys. Tab moves to the buttons. Command+C copies selected text.")
        text.setSelectedRange(NSRange(location: 0, length: 0))
        text.isVerticallyResizable = true; text.isHorizontallyResizable = false
        text.autoresizingMask = [.width]; text.textContainer?.widthTracksTextView = true
        let scroll = NSScrollView(); scroll.hasVerticalScroller = true; scroll.documentView = text
        primary.target = self; primary.action = #selector(accept); primary.isEnabled = !progress
        primary.keyEquivalent = "\r"; primary.setAccessibilityHelp(cancel ? "Begin installation after reviewing the instructions." : "Close these installation details.")
        var buttons = [primary]
        if let secondary {
            secondary.target = self; secondary.action = cancel ? #selector(cancelAction) : #selector(copyDetails)
            secondary.setAccessibilityHelp(cancel ? "Cancel without changing the installed files." : "Copy all installation details to the clipboard.")
            buttons.append(secondary)
            text.nextKeyView = primary; primary.nextKeyView = secondary; secondary.nextKeyView = text
        } else { text.nextKeyView = primary; primary.nextKeyView = text }
        let row = NSStackView(views: buttons); row.spacing = 12
        let stack = NSStackView(views: [scroll, row]); stack.orientation = .vertical; stack.alignment = .leading
        stack.spacing = 12; stack.translatesAutoresizingMaskIntoConstraints = false
        panel.contentView!.addSubview(stack); let root = panel.contentView!
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 16),
            stack.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -16),
            stack.topAnchor.constraint(equalTo: root.topAnchor, constant: 16),
            stack.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -16),
            scroll.widthAnchor.constraint(equalTo: stack.widthAnchor), scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 180)])
        panel.initialFirstResponder = text
        if !progress { panel.dismiss = { [weak self] in self?.finish(false) } }
    }
    func show() -> Bool {
        NSApp.activate(ignoringOtherApps: true); panel.center(); panel.makeKeyAndOrderFront(nil)
        panel.makeFirstResponder(text)
        NSApp.runModal(for: panel); panel.orderOut(nil); panel.close()
        return result
    }
    func finish(_ accepted: Bool) { result = accepted; NSApp.stopModal() }
    @objc private func accept() { finish(true) }
    @objc private func cancelAction() { finish(false) }
    @objc private func copyDetails() { NSPasteboard.general.clearContents(); NSPasteboard.general.setString(detail, forType: .string) }
}

enum Maintenance {
    static let repository = "OnjLouis/sound-canvas-bridge"
    static let team = "83NN3HS237"
    static let appID = "me.onj.soundcanvasbridge.manager"
    static let installerID = "me.onj.soundcanvasbridge.installer"
    static let bundles = ["Sound Canvas Bridge.app", "Sound Canvas Bridge.component", "Sound Canvas Bridge.vst3"]

    static func isInstallerInterface(arguments: [String], bundleID: String?, effectiveUID: uid_t) -> Bool {
        guard effectiveUID != 0, bundleID == installerID else { return false }
        return arguments.isEmpty || arguments.count == 2 && arguments[0] == "--update-stage"
            || arguments.count == 1 && arguments[0].hasPrefix("-psn_")
    }

    static func versionParts(_ value: String) -> [Int]? {
        let values = value.split(separator: ".", omittingEmptySubsequences: false)
        guard values.count == 3, values.allSatisfy({ !$0.isEmpty && $0.allSatisfy({ $0.isASCII && $0.isNumber }) }),
              let a = Int(values[0]), let b = Int(values[1]), let c = Int(values[2]) else { return nil }
        return [a, b, c]
    }

    static func select(_ data: Data, current: String) throws -> BridgeRelease? {
        guard let currentParts = versionParts(current),
              let releases = try JSONSerialization.jsonObject(with: data) as? [[String: Any]] else {
            throw MaintenanceError.failed("The release server returned an invalid release list.")
        }
        var candidates: [(BridgeRelease, [Int])] = []
        for release in releases {
            guard release["draft"] as? Bool == false, release["prerelease"] as? Bool == false,
                  let tag = release["tag_name"] as? String, tag.hasPrefix("v"),
                  let parts = versionParts(String(tag.dropFirst())), currentParts.lexicographicallyPrecedes(parts),
                  let assets = release["assets"] as? [[String: Any]] else { continue }
            let version = String(tag.dropFirst())
            let name = "SoundCanvasBridge-\(version)-mac-universal.dmg"
            guard assets.contains(where: { $0["name"] as? String == name }) else { continue }
            candidates.append((BridgeRelease(version: version, asset: name,
                download: "https://github.com/\(repository)/releases/download/\(tag)/\(name)",
                page: "https://github.com/\(repository)/releases/tag/\(tag)"), parts))
        }
        return candidates.sorted { $1.1.lexicographicallyPrecedes($0.1) }.first?.0
    }

    static func run(_ executable: String, _ arguments: [String], timeout: TimeInterval = 30) throws -> Data {
        let task = Process(); task.executableURL = URL(fileURLWithPath: executable); task.arguments = arguments
        let pipe = Pipe(); task.standardOutput = pipe; task.standardError = pipe
        let done = DispatchSemaphore(value: 0)
        task.terminationHandler = { _ in done.signal() }
        try task.run()
        // Read concurrently so a full pipe cannot deadlock the bounded waiter.
        final class Output: @unchecked Sendable { var data = Data() }
        let output = Output(); let drained = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            while let chunk = try? pipe.fileHandleForReading.read(upToCount: 8192), !chunk.isEmpty {
                if output.data.count < 262144 { output.data.append(chunk.prefix(262144 - output.data.count)) }
            }
            drained.signal()
        }
        let timedOut: Bool
        if timeout > 0 { timedOut = done.wait(timeout: .now() + timeout) == .timedOut }
        else { done.wait(); timedOut = false }
        if timedOut {
            task.terminate()
            if done.wait(timeout: .now() + 2) == .timedOut { kill(task.processIdentifier, SIGKILL); task.waitUntilExit() }
            _ = drained.wait(timeout: .now() + 2)
            throw MaintenanceError.failed("\(URL(fileURLWithPath: executable).lastPathComponent) did not finish in time.")
        }
        guard drained.wait(timeout: .now() + 2) == .success else {
            try? pipe.fileHandleForReading.close()
            throw MaintenanceError.failed("\(URL(fileURLWithPath: executable).lastPathComponent) left its output pipe open.")
        }
        guard task.terminationStatus == 0 else {
            throw MaintenanceError.failed("\(URL(fileURLWithPath: executable).lastPathComponent) failed (\(task.terminationStatus)). " + String(decoding: output.data.suffix(2048), as: UTF8.self))
        }
        return output.data
    }

    static func verify(_ url: URL, id: String) throws {
        let requirement = "anchor apple generic and certificate leaf[subject.OU] = \"\(team)\" and identifier \"\(id)\""
        _ = try run("/usr/bin/codesign", ["--verify", "--all-architectures", "--deep", "--strict", "--test-requirement", "=" + requirement, url.path])
    }

    static func verifyUniversalExecutable(_ url: URL) throws {
        for architecture in ["arm64", "x86_64"] {
            _ = try run("/usr/bin/lipo", [url.path, "-verify_arch", architecture])
        }
    }

    static func installedVersion(_ item: URL) -> String? {
        // Bundle caches metadata for a path, even after atomic replacement.
        // Validate the file we just installed, not the previous cached bundle.
        let file = item.appendingPathComponent("Contents/Info.plist")
        guard let handle = try? FileHandle(forReadingFrom: file) else { return nil }
        defer { try? handle.close() }
        guard let data = try? handle.read(upToCount: 1_048_577), !data.isEmpty, data.count <= 1_048_576,
              let info = try? PropertyListSerialization.propertyList(from: data, options: [], format: nil) as? [String: Any] else { return nil }
        return info["CFBundleShortVersionString"] as? String
    }

    static func verifyImage(_ image: URL) throws {
        try verify(image, id: "me.onj.soundcanvasbridge.diskimage")
        _ = try run("/usr/bin/xcrun", ["stapler", "validate", image.path])
        _ = try run("/usr/sbin/spctl", ["--assess", "--type", "open", "--context", "context:primary-signature", image.path])
    }

    static func verifyMountedPayload(_ mount: URL, version: String) throws -> URL {
        let installer = mount.appendingPathComponent("Install Sound Canvas Bridge.app")
        try verify(installer, id: installerID)
        guard Bundle(url: installer)?.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String == version else {
            throw MaintenanceError.failed("The installer version does not match the update.")
        }
        try verifyPayload(mount.appendingPathComponent("Payload"), version: version)
        return installer
    }

    static func verifyPayload(_ payload: URL, version: String) throws {
        guard versionParts(version) != nil else { throw MaintenanceError.failed("Invalid update version.") }
        for (i, name) in bundles.enumerated() {
            let item = payload.appendingPathComponent(name)
            let values = try item.resourceValues(forKeys: [.isSymbolicLinkKey, .isDirectoryKey])
            guard values.isSymbolicLink != true, values.isDirectory == true,
                  let bundle = Bundle(url: item), bundle.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String == version else {
                throw MaintenanceError.failed("The installer contains a missing or mismatched \(name).")
            }
            try verify(item, id: i == 0 ? appID : "me.onj.soundcanvasbridge")
            let executable = bundle.executableURL!
            try verifyUniversalExecutable(executable)
        }
        let app = payload.appendingPathComponent(bundles[0])
        _ = try run("/usr/sbin/spctl", ["--assess", "--type", "execute", app.path])
        _ = try run("/usr/bin/xcrun", ["stapler", "validate", app.path])
    }

    static func fetch(_ request: URLRequest, maximum: Int = 2_097_152) throws -> (Data, Int) {
        final class Fetcher: NSObject, URLSessionDataDelegate, @unchecked Sendable {
            let limit: Int; var data = Data(); var error: Error?; var status = 0; let done = DispatchSemaphore(value: 0)
            init(_ value: Int) { limit = value }
            func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse, completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
                guard response.url?.scheme == "https", response.expectedContentLength <= Int64(limit) else { completionHandler(.cancel); return }
                status = (response as? HTTPURLResponse)?.statusCode ?? 0; completionHandler(.allow)
            }
            func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive bytes: Data) {
                if bytes.count > limit - data.count { error = MaintenanceError.failed("The download exceeded its size limit."); dataTask.cancel() }
                else { data.append(bytes) }
            }
            func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError failure: Error?) { if error == nil { error = failure }; done.signal() }
            func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
                completionHandler(request.url?.scheme == "https" ? request : nil)
            }
        }
        let delegate = Fetcher(maximum)
        let configuration = URLSessionConfiguration.ephemeral; configuration.timeoutIntervalForRequest = 30; configuration.timeoutIntervalForResource = 120
        let session = URLSession(configuration: configuration, delegate: delegate, delegateQueue: nil)
        defer { session.invalidateAndCancel() }
        session.dataTask(with: request).resume()
        guard delegate.done.wait(timeout: .now() + 125) == .success else { throw MaintenanceError.failed("The download timed out.") }
        if let error = delegate.error { throw error }
        return (delegate.data, delegate.status)
    }

    static func check(current: String) throws -> [String: Any] {
        var request = URLRequest(url: URL(string: "https://api.github.com/repos/\(repository)/releases?per_page=100")!)
        request.setValue("Sound Canvas Bridge updater", forHTTPHeaderField: "User-Agent")
        let (data, status) = try fetch(request)
        if status == 404 { return ["status": "no-channel", "message": "No public release is available yet."] }
        guard status == 200 else { throw MaintenanceError.failed("The release check returned HTTP \(status).") }
        guard let release = try select(data, current: current) else { return ["status": "current", "message": "Sound Canvas Bridge \(current) is up to date."] }
        return ["status": "available", "release": try JSONSerialization.jsonObject(with: JSONEncoder().encode(release))]
    }

    static func prepare(_ release: BridgeRelease) throws -> [String: Any] {
        guard versionParts(release.version) != nil,
              release.asset == "SoundCanvasBridge-\(release.version)-mac-universal.dmg",
              release.download == "https://github.com/\(repository)/releases/download/v\(release.version)/\(release.asset)" else {
            throw MaintenanceError.failed("The selected update is not a Sound Canvas Bridge release.")
        }
        let fm = FileManager.default
        let staging = fm.temporaryDirectory.appendingPathComponent("SoundCanvasUpdate-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: staging, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        var retained = false; var mounted = false
        let mount = staging.appendingPathComponent("Mounted", isDirectory: true)
        defer {
            if !retained {
                if mounted { _ = try? run("/usr/bin/hdiutil", ["detach", mount.path]) }
                try? fm.removeItem(at: staging)
            }
        }
        let (data, status) = try fetch(URLRequest(url: URL(string: release.download)!), maximum: 200 * 1024 * 1024)
        guard status == 200 else { throw MaintenanceError.failed("The update download returned HTTP \(status).") }
        let image = staging.appendingPathComponent(release.asset); try data.write(to: image, options: .atomic)
        try verifyImage(image)
        try fm.createDirectory(at: mount, withIntermediateDirectories: false)
        _ = try run("/usr/bin/hdiutil", ["attach", image.path, "-readonly", "-nobrowse", "-mountpoint", mount.path])
        mounted = true
        let installer = try verifyMountedPayload(mount, version: release.version)
        try JSONEncoder().encode(release).write(to: staging.appendingPathComponent("Release.json"), options: .atomic)
        _ = try run("/usr/bin/open", [installer.path, "--args", "--update-stage", staging.path])
        retained = true
        return ["status": "installer-open", "message": "The verified installer is open. Stop MIDI playback and close audio hosts before choosing Install."]
    }

    static func replaceAll(payload: URL, targets: [URL], validate: (URL) throws -> Void,
                           failureAfter: Int? = nil, validateInstalled: () throws -> Void = {}) throws {
        guard targets.count == bundles.count else { throw MaintenanceError.failed("Invalid installation plan.") }
        let fm = FileManager.default; let token = UUID().uuidString
        var candidates: [URL] = []; var changes: [(target: URL, backup: URL, existed: Bool)] = []
        do {
            for (index, target) in targets.enumerated() {
                guard target.standardizedFileURL == target.resolvingSymlinksInPath().standardizedFileURL else {
                    throw MaintenanceError.failed("An installation path is redirected. Nothing was replaced.")
                }
                try fm.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
                let candidate = target.deletingLastPathComponent().appendingPathComponent(".\(target.lastPathComponent).incoming-\(token)")
                candidates.append(candidate); try fm.copyItem(at: payload.appendingPathComponent(bundles[index]), to: candidate)
            }
            try validate(payload)
            for (index, target) in targets.enumerated() {
                let backup = target.deletingLastPathComponent().appendingPathComponent(".\(target.lastPathComponent).rollback-\(token)")
                let existed = fm.fileExists(atPath: target.path)
                if existed { try fm.moveItem(at: target, to: backup) }
                changes.append((target, backup, existed))
                try fm.moveItem(at: candidates[index], to: target)
                if failureAfter == index { throw MaintenanceError.failed("Injected installation failure.") }
            }
            try validateInstalled()
        } catch {
            var restored = true
            for item in changes.reversed() {
                do {
                    if fm.fileExists(atPath: item.target.path) { try fm.removeItem(at: item.target) }
                    if item.existed { try fm.moveItem(at: item.backup, to: item.target) }
                } catch { restored = false }
            }
            for item in candidates where fm.fileExists(atPath: item.path) { try? fm.removeItem(at: item) }
            if !restored { throw MaintenanceError.failed("Installation failed. A previous bundle is retained beside its installation path with a .rollback name; automatic restoration could not finish.") }
            throw error
        }
        for item in changes where item.existed { try? fm.removeItem(at: item.backup) }
    }

    static func apply(payload: URL, version: String) throws {
        guard geteuid() == 0 else { throw MaintenanceError.failed("Installation requires administrator authorisation.") }
        try verify(Bundle.main.bundleURL, id: installerID)
        try verifyPayload(payload, version: version)
        let targets = [URL(fileURLWithPath: "/Applications/\(bundles[0])"),
            URL(fileURLWithPath: "/Library/Audio/Plug-Ins/Components/\(bundles[1])"),
            URL(fileURLWithPath: "/Library/Audio/Plug-Ins/VST3/\(bundles[2])")]
        for target in targets where FileManager.default.fileExists(atPath: target.path) {
            if let executable = Bundle(url: target)?.executableURL {
                let task = Process(); task.executableURL = URL(fileURLWithPath: "/usr/sbin/lsof")
                task.arguments = ["-t", "--", executable.path]; task.standardOutput = FileHandle.nullDevice; task.standardError = FileHandle.nullDevice
                try task.run()
                let deadline = Date().addingTimeInterval(5)
                while task.isRunning && Date() < deadline { Thread.sleep(forTimeInterval: 0.05) }
                if task.isRunning {
                    task.terminate(); let stopped = Date().addingTimeInterval(1)
                    while task.isRunning && Date() < stopped { Thread.sleep(forTimeInterval: 0.05) }
                    if task.isRunning { kill(task.processIdentifier, SIGKILL); task.waitUntilExit() }
                    throw MaintenanceError.failed("Could not check whether the audio hosts are closed.")
                }
                guard task.terminationStatus == 1 else { throw MaintenanceError.failed("Sound Canvas Bridge is still loaded. Close audio hosts and try again.") }
            }
        }
        // Copy into a root-private directory before validation/replacement, so
        // downloaded user-writable files cannot change during the transaction.
        let fm = FileManager.default
        let root = URL(fileURLWithPath: "/private/var/tmp/SoundCanvasInstall-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: root, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        defer { try? fm.removeItem(at: root) }
        for name in bundles { try fm.copyItem(at: payload.appendingPathComponent(name), to: root.appendingPathComponent(name)) }
        try verifyPayload(root, version: version)
        try replaceAll(payload: root, targets: targets, validate: { try verifyPayload($0, version: version) }, validateInstalled: {
            for (index, item) in targets.enumerated() {
                try verify(item, id: index == 0 ? appID : "me.onj.soundcanvasbridge")
                let actual = installedVersion(item)
                guard actual == version else {
                    throw MaintenanceError.failed("Installed version verification failed for \(item.lastPathComponent): expected \(version), read \(actual ?? "unreadable").")
                }
            }
        })
    }

    struct QuitTarget {
        let description: String
        let request: () -> Bool
        let hasStopped: () -> Bool
    }

    static func closeStandalone(_ targets: [QuitTarget], timeout: TimeInterval = 30,
                                now: () -> Date = { Date() },
                                pump: () -> Void = { RunLoop.current.run(until: Date().addingTimeInterval(0.05)) }) throws {
        for target in targets where !target.hasStopped() {
            InstallerLog.write("Requesting standalone quit: \(target.description).")
            guard target.request() else {
                throw MaintenanceError.failed("The standalone application could not close. Stop playback and quit Sound Canvas Bridge, then try again. Nothing was installed.")
            }
        }
        let deadline = now().addingTimeInterval(timeout)
        while targets.contains(where: { !$0.hasStopped() }) && now() < deadline { pump() }
        guard targets.allSatisfy({ $0.hasStopped() }) else {
            throw MaintenanceError.failed("The standalone application did not close in time. Quit Sound Canvas Bridge and try again. Nothing was installed.")
        }
        InstallerLog.write("Standalone shutdown verified; \(targets.count) application(s) closed. Audio hosts were not asked to quit.")
    }

    static func installer(updateStage: URL?) throws -> [String: Any] {
        // The installer runs from its mounted update, so a separate helper must
        // wait for this process to exit before unmounting, including on Cancel.
        defer {
            if let stage = updateStage {
                do {
                    try validateCleanupStage(stage)
                    let helper = stage.appendingPathComponent("Cleanup.app")
                    try FileManager.default.copyItem(at: Bundle.main.bundleURL, to: helper)
                    try verify(helper, id: installerID)
                    let task = Process(); task.executableURL = Bundle(url: helper)?.executableURL
                    task.arguments = ["--cleanup", stage.path, String(getpid())]
                    let log = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/OnjMusic/SoundCanvasBridge/UpdateCleanup.log")
                    FileManager.default.createFile(atPath: log.path, contents: nil)
                    let output = try? FileHandle(forWritingTo: log)
                    if let output { task.standardOutput = output; task.standardError = output }
                    defer { try? output?.close() }
                    try task.run()
                }
                catch { InstallerLog.write("Could not start update cleanup: \(error.localizedDescription)") }
            }
        }
        let application = NSApplication.shared; application.setActivationPolicy(.regular); application.finishLaunching(); application.activate(ignoringOtherApps: true)
        InstallerLog.write("Installer opened.")
        let payload = Bundle.main.bundleURL.deletingLastPathComponent().appendingPathComponent("Payload")
        guard let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String else { throw MaintenanceError.failed("Installer version is missing.") }
        try verify(Bundle.main.bundleURL, id: installerID); try verifyPayload(payload, version: version)
        InstallerLog.write("Payload signatures, notarisation, architectures and version verified: \(version).")
        let dialog = InstallerDialog(title: "Install Sound Canvas Bridge \(version)",
            message: "Install the standalone application, Audio Unit and VST3 together.\n\nStop MIDI playback and unload Sound Canvas Bridge from your audio hosts first. The standalone application will close during installation. Your settings and presets will be kept.\n\nChoose Install to continue, or Cancel to leave the installed files unchanged.", primaryTitle: "Install", cancel: true)
        guard dialog.show() else { InstallerLog.write("Installation cancelled before changes."); return ["status": "cancelled"] }
        InstallerLog.write("Install selected.")
        let running = NSRunningApplication.runningApplications(withBundleIdentifier: appID)
        for item in running {
            guard let bundle = item.bundleURL else { throw MaintenanceError.failed("Could not identify the running standalone application. Quit it before installing.") }
            try verify(bundle, id: appID)
        }
        try closeStandalone(running.map { item in
            QuitTarget(description: "Sound Canvas Bridge, PID \(item.processIdentifier)",
                       request: { item.terminate() }, hasStopped: { item.isTerminated })
        })
        let command = quote(Bundle.main.executableURL!.path) + " --apply " + quote(payload.path) + " " + quote(version)
        let progress = InstallerDialog(title: "Installing Sound Canvas Bridge", message: "Waiting for administrator authorisation, then installing all three formats.\n\nThe installed files are backed up before replacement. If replacement fails, the installer restores them.\n\nPlease leave this window open until the operation finishes.", primaryTitle: "Installing...", progress: true)
        var outcome: Result<Data, Error>?
        DispatchQueue.global().async {
            // The native authorisation prompt has Cancel. Do not kill an
            // elevated transaction halfway through replacing the bundles.
            let value = Result { try run("/usr/bin/osascript", ["-e", "do shell script \(appleQuote(command)) with administrator privileges"], timeout: 0) }
            DispatchQueue.main.async { outcome = value; progress.finish(true) }
        }
        _ = progress.show()
        guard let outcome else { throw MaintenanceError.failed("The installation result was unavailable.") }
        if case .failure(let failure) = outcome {
            InstallerLog.write("Installation failed: \(failure.localizedDescription)")
            if !running.isEmpty { _ = NSWorkspace.shared.open(URL(fileURLWithPath: "/Applications/\(bundles[0])")) }
            throw MaintenanceError.failed("Installation did not complete.\n\n" + failure.localizedDescription)
        }
        InstallerLog.write("All three formats installed and verified.")
        _ = NSWorkspace.shared.open(URL(fileURLWithPath: "/Applications/\(bundles[0])"))
        _ = InstallerDialog(title: "Installation Complete", message: "Sound Canvas Bridge \(version) is installed.\n\nThe standalone application, Audio Unit and VST3 were updated together. Your settings and NKS presets were kept.\n\nThe standalone application has reopened. If Start hidden is enabled, use its menu-bar item to show the controls.", primaryTitle: "Close").show()
        return ["status": "installed"]
    }

    static func validateCleanupStage(_ stage: URL) throws {
        let fm = FileManager.default
        guard stage.lastPathComponent.hasPrefix("SoundCanvasUpdate-"), stage.deletingLastPathComponent().standardizedFileURL == fm.temporaryDirectory.standardizedFileURL,
              stage.resolvingSymlinksInPath().standardizedFileURL == stage.standardizedFileURL,
              fm.fileExists(atPath: stage.appendingPathComponent("Release.json").path) else {
            throw MaintenanceError.failed("Unexpected update cleanup location.")
        }
    }

    static func cleanup(stage: URL, installerPID: Int32) throws {
        try validateCleanupStage(stage)
        let fm = FileManager.default
        let deadline = Date().addingTimeInterval(30)
        while kill(installerPID, 0) == 0 && Date() < deadline { Thread.sleep(forTimeInterval: 0.1) }
        guard kill(installerPID, 0) != 0 else { throw MaintenanceError.failed("The installer is still open; its download was retained.") }
        // LaunchServices can retain the image briefly after the installer exits.
        // Retry ordinary unmounts; never force an image out from under a process.
        var unmounted = false
        for attempt in 0..<8 {
            do {
                _ = try run("/usr/bin/hdiutil", ["detach", stage.appendingPathComponent("Mounted").path])
                unmounted = true; break
            } catch {
                if attempt == 7 { throw error }
                Thread.sleep(forTimeInterval: 0.5)
            }
        }
        guard unmounted else { throw MaintenanceError.failed("The update image is still in use; its download was retained.") }
        try fm.removeItem(at: stage)
    }

    static func quote(_ value: String) -> String { "'" + value.replacingOccurrences(of: "'", with: "'\\''") + "'" }
    static func appleQuote(_ value: String) -> String { "\"" + value.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"") + "\"" }

    #if SCB_MAINTENANCE_TESTS
    static func testInstallerUI() throws -> [String: Any] {
        let app = NSApplication.shared; app.setActivationPolicy(.regular); app.finishLaunching(); app.activate(ignoringOtherApps: true)
        let dialog = InstallerDialog(title: "Installer Message Test", message: "First line.\nSecond line.\nThird line.", primaryTitle: "Close")
        dialog.panel.makeKeyAndOrderFront(nil); dialog.panel.makeFirstResponder(dialog.text)
        RunLoop.current.run(until: Date().addingTimeInterval(0.25))
        guard dialog.text.accessibilityRole() == .textArea, !dialog.text.isEditable, dialog.text.isSelectable,
              dialog.panel.firstResponder === dialog.text, dialog.text.frame.width > 100, dialog.text.frame.height > 100,
              dialog.text.accessibilityLabel() == "Installation details", dialog.panel.title == "Installer Message Test" else {
            throw MaintenanceError.failed("Installer details are not a focused, visible, read-only text area.")
        }
        dialog.panel.orderOut(nil); dialog.panel.close()
        let progress = InstallerDialog(title: "Installer Progress Test", message: "The installer remains responsive while work runs.", primaryTitle: "Working...", progress: true)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { progress.finish(true) }
        guard progress.show() else { throw MaintenanceError.failed("Installer progress did not complete through the modal event loop.") }
        return ["status": "passed", "tests": "native installer details text-area role, visible bounds, initial focus, read-only/selectable content and distinct window title"]
    }
    static func inspectImage(_ image: URL, version: String) throws -> [String: Any] {
        try verifyImage(image)
        let fm = FileManager.default
        let mount = fm.temporaryDirectory.appendingPathComponent("SoundCanvasImageTest-\(UUID().uuidString)")
        try fm.createDirectory(at: mount, withIntermediateDirectories: false)
        var attached = false
        defer {
            if attached { _ = try? run("/usr/bin/hdiutil", ["detach", mount.path]) }
            try? fm.removeItem(at: mount)
        }
        _ = try run("/usr/bin/hdiutil", ["attach", image.path, "-readonly", "-nobrowse", "-mountpoint", mount.path])
        attached = true
        _ = try verifyMountedPayload(mount, version: version)
        return ["status": "verified", "version": version, "productionTrustChecks": true]
    }
    static func selfTest() throws -> [String: Any] {
        var quitRequests = 0
        var stopped = false
        let target = QuitTarget(description: "test standalone", request: { quitRequests += 1; return true }, hasStopped: { stopped })
        try closeStandalone([target], pump: { stopped = true })
        guard quitRequests == 1, stopped else { throw MaintenanceError.failed("Standalone quit was not requested and verified.") }
        try closeStandalone([])
        try closeStandalone([target])
        guard quitRequests == 1 else { throw MaintenanceError.failed("An already stopped application was asked to quit again.") }
        let refused = QuitTarget(description: "refused test", request: { false }, hasStopped: { false })
        do { try closeStandalone([refused]); throw MaintenanceError.failed("Refused quit accepted.") }
        catch MaintenanceError.failed(let text) where text.contains("could not close") { }
        do { try closeStandalone([QuitTarget(description: "stalled test", request: { true }, hasStopped: { false })], timeout: 0); throw MaintenanceError.failed("Stalled quit accepted.") }
        catch MaintenanceError.failed(let text) where text.contains("did not close in time") { }
        guard try replyData(["status": "error", "message": String(repeating: "x", count: 8192)]).count <= maximumReplyBytes else {
            throw MaintenanceError.failed("An oversized helper reply can block the parent output pipe.")
        }
        let verbose = String(repeating: "x", count: 20000)
        guard try run("/usr/bin/printf", [verbose]) == Data(verbose.utf8) else {
            throw MaintenanceError.failed("Subprocess output was truncated or blocked.")
        }
        guard !isInstallerInterface(arguments: ["--apply", "/fixture", "1.0.0"], bundleID: installerID, effectiveUID: 0) else {
            throw MaintenanceError.failed("Privileged helper must not present an installer window.")
        }
        guard !isInstallerInterface(arguments: ["--apply", "/fixture", "1.0.0"], bundleID: installerID, effectiveUID: 501),
              !isInstallerInterface(arguments: ["--cleanup", "/fixture", "100"], bundleID: installerID, effectiveUID: 501),
              isInstallerInterface(arguments: [], bundleID: installerID, effectiveUID: 501),
              isInstallerInterface(arguments: ["--update-stage", "/fixture"], bundleID: installerID, effectiveUID: 501) else {
            throw MaintenanceError.failed("Installer and command-line helper roles are not separated.")
        }
        try verifyUniversalExecutable(URL(fileURLWithPath: CommandLine.arguments[0]))
        let fm = FileManager.default; let root = fm.temporaryDirectory.appendingPathComponent("SoundCanvasMaintenanceTests-\(UUID().uuidString)")
        try fm.createDirectory(at: root, withIntermediateDirectories: true); defer { try? fm.removeItem(at: root) }
        let fixture = root.appendingPathComponent("Version Fixture.app")
        try fm.createDirectory(at: fixture.appendingPathComponent("Contents/MacOS"), withIntermediateDirectories: true)
        try Data("fixture".utf8).write(to: fixture.appendingPathComponent("Contents/MacOS/Fixture"))
        let infoFile = fixture.appendingPathComponent("Contents/Info.plist")
        var metadata = ["CFBundleIdentifier": "me.onj.soundcanvasbridge.versionfixture", "CFBundleExecutable": "Fixture", "CFBundlePackageType": "APPL", "CFBundleShortVersionString": "0.2.6"]
        try PropertyListSerialization.data(fromPropertyList: metadata, format: .xml, options: 0).write(to: infoFile)
        let cached = Bundle(url: fixture)
        guard cached?.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String == "0.2.6", installedVersion(fixture) == "0.2.6" else { throw MaintenanceError.failed("Version fixture did not load.") }
        metadata["CFBundleShortVersionString"] = "1.0.0"
        try PropertyListSerialization.data(fromPropertyList: metadata, format: .xml, options: 0).write(to: infoFile, options: .atomic)
        guard installedVersion(fixture) == "1.0.0" else { throw MaintenanceError.failed("Installed version reader retained cached metadata after replacement.") }
        let payload = root.appendingPathComponent("Payload"); try fm.createDirectory(at: payload, withIntermediateDirectories: false)
        let targets = bundles.map { root.appendingPathComponent("Installed/" + $0) }
        for name in bundles { try Data("new-\(name)".utf8).write(to: payload.appendingPathComponent(name)) }
        try fm.createDirectory(at: targets[0].deletingLastPathComponent(), withIntermediateDirectories: false)
        for item in targets { try Data("old-\(item.lastPathComponent)".utf8).write(to: item) }
        for index in 0..<3 {
            do { try replaceAll(payload: payload, targets: targets, validate: { _ in }, failureAfter: index); throw MaintenanceError.failed("Rollback injection did not fail.") }
            catch MaintenanceError.failed("Injected installation failure.") { }
            for item in targets { guard try Data(contentsOf: item) == Data("old-\(item.lastPathComponent)".utf8) else { throw MaintenanceError.failed("Rollback differs.") } }
        }
        try replaceAll(payload: payload, targets: targets, validate: { _ in })
        for item in targets { guard try Data(contentsOf: item) == Data("new-\(item.lastPathComponent)".utf8) else { throw MaintenanceError.failed("Replacement differs.") } }
        try fm.removeItem(at: targets[2])
        do { try replaceAll(payload: payload, targets: targets, validate: { _ in }, failureAfter: 2); throw MaintenanceError.failed("Absent-destination injection did not fail.") }
        catch MaintenanceError.failed("Injected installation failure.") { }
        guard !fm.fileExists(atPath: targets[2].path) else { throw MaintenanceError.failed("Rollback did not restore an absent destination.") }
        do {
            try replaceAll(payload: payload, targets: targets, validate: { _ in }, validateInstalled: { throw MaintenanceError.failed("Injected verification failure.") })
            throw MaintenanceError.failed("Post-install verification injection did not fail.")
        } catch MaintenanceError.failed("Injected verification failure.") { }
        guard !fm.fileExists(atPath: targets[2].path) else { throw MaintenanceError.failed("Verification rollback differs.") }
        for item in targets.prefix(2) { guard try Data(contentsOf: item) == Data("new-\(item.lastPathComponent)".utf8) else { throw MaintenanceError.failed("Verification rollback changed previous files.") } }
        let releases: [[String: Any]] = [
            ["tag_name": "v1.2.0", "draft": false, "prerelease": false, "assets": [["name": "SoundCanvasBridge-1.2.0-mac-universal.dmg"]]],
            ["tag_name": "v9.0.0", "draft": true, "prerelease": false, "assets": [["name": "SoundCanvasBridge-9.0.0-mac-universal.dmg"]]],
            ["tag_name": "windows-v8.0.0", "draft": false, "prerelease": false, "assets": []]]
        guard try select(JSONSerialization.data(withJSONObject: releases), current: "1.0.0")?.version == "1.2.0" else { throw MaintenanceError.failed("Release selection differs.") }
        do { try verify(payload, id: appID); throw MaintenanceError.failed("Unsigned content accepted.") }
        catch MaintenanceError.failed(let text) where text != "Unsigned content accepted." { }
        return ["status": "passed", "tests": "all three replacement failure points restore exact bytes; absent destination and post-install verification rollback; full replacement; stable universal DMG selection; unsigned content rejected"]
    }
    #endif
}

let maintenanceArguments = Array(CommandLine.arguments.dropFirst())
do {
    let args = maintenanceArguments
    #if SCB_MAINTENANCE_TESTS
    if args.count == 3 && args[0] == "--test-close", let pid = Int32(args[1]),
       let item = NSRunningApplication(processIdentifier: pid), item.bundleIdentifier == Maintenance.appID,
       item.bundleURL?.standardizedFileURL == URL(fileURLWithPath: args[2]).standardizedFileURL {
        try Maintenance.closeStandalone([Maintenance.QuitTarget(description: "test standalone PID \(pid)",
            request: { item.terminate() }, hasStopped: { item.isTerminated })])
        print("PASS native standalone quit and exit verification")
        exit(0)
    }
    if args == ["--self-test"] {
        FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject: Maintenance.selfTest(), options: [.sortedKeys])); print(""); exit(0)
    }
    if args.count == 3 && args[0] == "--verify-dmg" {
        FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject: Maintenance.inspectImage(URL(fileURLWithPath: args[1]), version: args[2]), options: [.sortedKeys])); print(""); exit(0)
    }
    if args == ["--self-test-ui"] {
        FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject: Maintenance.testInstallerUI(), options: [.sortedKeys])); print(""); exit(0)
    }
    #endif
    let result: [String: Any]
    if args.count == 2 && args[0] == "--check" { result = try Maintenance.check(current: args[1]) }
    else if args == ["--prepare"] {
        let data = FileHandle.standardInput.readDataToEndOfFile()
        guard data.count <= 65536 else { throw MaintenanceError.failed("Update request is too large.") }
        result = try Maintenance.prepare(JSONDecoder().decode(BridgeRelease.self, from: data))
    } else if args.count == 2 && args[0] == "--prepare-json" {
        guard args[1].utf8.count <= 65536 else { throw MaintenanceError.failed("Update request is too large.") }
        result = try Maintenance.prepare(JSONDecoder().decode(BridgeRelease.self, from: Data(args[1].utf8)))
    } else if args.count == 3 && args[0] == "--apply" { try Maintenance.apply(payload: URL(fileURLWithPath: args[1]), version: args[2]); result = ["status": "installed"] }
    else if args.count == 3 && args[0] == "--cleanup", let pid = Int32(args[2]), pid > 1 { try Maintenance.cleanup(stage: URL(fileURLWithPath: args[1]), installerPID: pid); result = ["status": "cleaned"] }
    else if Maintenance.isInstallerInterface(arguments: args, bundleID: Bundle.main.bundleIdentifier, effectiveUID: geteuid()) {
        let stage = args.count == 2 && args[0] == "--update-stage" ? URL(fileURLWithPath: args[1]) : nil
        result = try Maintenance.installer(updateStage: stage)
    } else { throw MaintenanceError.failed("Unknown maintenance command.") }
    FileHandle.standardOutput.write(try replyData(result)); print("")
} catch {
    let message = String(error.localizedDescription.prefix(4096))
    if Maintenance.isInstallerInterface(arguments: maintenanceArguments, bundleID: Bundle.main.bundleIdentifier, effectiveUID: geteuid()) {
        InstallerLog.write("Installer error: \(message)")
        let app = NSApplication.shared; app.setActivationPolicy(.regular); app.finishLaunching()
        _ = InstallerDialog(title: "Installation Could Not Finish", message: message, primaryTitle: "Close").show()
    }
    let data = (try? replyData(["status": "error", "message": message])) ?? Data("{\"status\":\"error\",\"message\":\"Update helper failed.\"}".utf8)
    FileHandle.standardOutput.write(data); print(""); exit(1)
}
