import AppKit
import Foundation
import UniformTypeIdentifiers

let appVersion = "1.1.0"
let authorLabel = "by：linfeng365"

func launchValue(_ flag: String) -> String? {
    guard let index = CommandLine.arguments.firstIndex(of: flag), CommandLine.arguments.count > index + 1 else { return nil }
    return CommandLine.arguments[index + 1]
}

func launchFiles() -> [String] {
    var files: [String] = []
    var index = 1
    while index < CommandLine.arguments.count {
        let value = CommandLine.arguments[index]
        if ["--angle", "--direction", "--preview"].contains(value) { index += 2; continue }
        if !value.hasPrefix("-") { files.append(value) }
        index += 1
    }
    return files
}

func runBackend(_ arguments: [String]) throws -> (Int32, String, String) {
    guard let script = Bundle.main.url(forResource: "rotate_pdf", withExtension: "py") else {
        throw NSError(domain: "PDFRotate", code: 1, userInfo: [NSLocalizedDescriptionKey: "应用资源不完整，请重新解压工具。"])
    }
    let process = Process()
    process.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
    process.arguments = ["-B", script.path] + arguments
    let output = Pipe(), errors = Pipe()
    process.standardOutput = output
    process.standardError = errors
    try process.run()
    let data = output.fileHandleForReading.readDataToEndOfFile()
    let errorData = errors.fileHandleForReading.readDataToEndOfFile()
    process.waitUntilExit()
    return (process.terminationStatus,
            String(data: data, encoding: .utf8)?.trimmingCharacters(in: .newlines) ?? "",
            String(data: errorData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "")
}

if CommandLine.arguments.contains("--self-test") {
    do {
        let (status, output, error) = try runBackend(["--self-test"])
        print(output.isEmpty ? error : output)
        exit(status)
    } catch {
        fputs(error.localizedDescription + "\n", stderr)
        exit(1)
    }
}

final class DropView: NSView {
    var onDrop: (([URL]) -> Void)?
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        registerForDraggedTypes([.fileURL])
        wantsLayer = true
        layer?.backgroundColor = NSColor.windowBackgroundColor.cgColor
    }
    required init?(coder: NSCoder) { fatalError() }
    func files(_ sender: NSDraggingInfo) -> [URL] {
        sender.draggingPasteboard.readObjects(forClasses: [NSURL.self],
            options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
    }
    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        files(sender).isEmpty ? [] : .copy
    }
    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        let urls = files(sender)
        guard !urls.isEmpty else { return false }
        onDrop?(urls)
        return true
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var chooseButton: NSButton!
    var revealButton: NSButton!
    var status: NSTextField!
    var progress: NSProgressIndicator!
    var results: NSTextView!
    var angleField: NSTextField!
    var directionChoice: NSPopUpButton!
    var outputs: [URL] = []
    var isProcessing = false

    func label(_ text: String, size: CGFloat, weight: NSFont.Weight = .regular) -> NSTextField {
        let field = NSTextField(labelWithString: text)
        field.font = .systemFont(ofSize: size, weight: weight)
        field.isSelectable = false
        return field
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        let menu = NSMenu()
        let applicationItem = NSMenuItem()
        let applicationMenu = NSMenu()
        let about = NSMenuItem(title: "关于 PDF 旋转工具", action: #selector(showAbout), keyEquivalent: "")
        about.target = self
        applicationMenu.addItem(about)
        applicationMenu.addItem(.separator())
        applicationMenu.addItem(withTitle: "退出 PDF 旋转工具", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        applicationItem.submenu = applicationMenu
        menu.addItem(applicationItem)
        NSApp.mainMenu = menu

        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 650, height: 530),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "PDF 旋转工具"
        window.isReleasedWhenClosed = false
        window.center()
        let drop = DropView(frame: NSRect(x: 0, y: 0, width: 650, height: 530))
        drop.onDrop = { [weak self] urls in self?.process(urls) }
        window.contentView = drop

        let title = label("将 PDF 拖到这里", size: 26, weight: .semibold)
        title.frame = NSRect(x: 30, y: 468, width: 590, height: 36)
        drop.addSubview(title)
        let subtitle = label("先设置角度，再拖入 PDF · 可多选 · 另存副本", size: 15)
        subtitle.textColor = .secondaryLabelColor
        subtitle.frame = NSRect(x: 30, y: 432, width: 590, height: 25)
        drop.addSubview(subtitle)
        let note = label("保留原件与完整页边；输出在原文件旁，重名时自动编号。", size: 13)
        note.textColor = .secondaryLabelColor
        note.frame = NSRect(x: 30, y: 399, width: 590, height: 22)
        drop.addSubview(note)

        directionChoice = NSPopUpButton(frame: NSRect(x: 26, y: 347, width: 125, height: 32), pullsDown: false)
        directionChoice.addItems(withTitles: ["逆时针", "顺时针"])
        directionChoice.selectItem(at: launchValue("--direction") == "cw" ? 1 : 0)
        drop.addSubview(directionChoice)
        let angleLabel = label("角度", size: 14)
        angleLabel.frame = NSRect(x: 166, y: 351, width: 40, height: 23)
        drop.addSubview(angleLabel)
        angleField = NSTextField(frame: NSRect(x: 210, y: 348, width: 90, height: 28))
        angleField.stringValue = launchValue("--angle") ?? "1"
        angleField.font = .systemFont(ofSize: 16)
        angleField.alignment = .right
        drop.addSubview(angleField)
        let degree = label("°", size: 18)
        degree.frame = NSRect(x: 309, y: 351, width: 22, height: 25)
        drop.addSubview(degree)
        let hint = label("支持小数，例如 0.5、1.25", size: 12)
        hint.textColor = .secondaryLabelColor
        hint.frame = NSRect(x: 348, y: 352, width: 270, height: 22)
        drop.addSubview(hint)

        chooseButton = NSButton(title: "选择 PDF…", target: self, action: #selector(chooseFiles))
        chooseButton.bezelStyle = .rounded
        chooseButton.keyEquivalent = "o"
        chooseButton.keyEquivalentModifierMask = .command
        chooseButton.frame = NSRect(x: 26, y: 297, width: 125, height: 34)
        drop.addSubview(chooseButton)
        revealButton = NSButton(title: "显示结果", target: self, action: #selector(revealOutputs))
        revealButton.bezelStyle = .rounded
        revealButton.frame = NSRect(x: 157, y: 297, width: 110, height: 34)
        revealButton.isEnabled = false
        drop.addSubview(revealButton)

        status = label("等待添加 PDF", size: 13, weight: .medium)
        status.lineBreakMode = .byTruncatingMiddle
        status.frame = NSRect(x: 30, y: 262, width: 590, height: 22)
        drop.addSubview(status)
        progress = NSProgressIndicator(frame: NSRect(x: 30, y: 242, width: 590, height: 8))
        progress.style = .bar
        progress.isIndeterminate = false
        progress.minValue = 0
        progress.maxValue = 1
        drop.addSubview(progress)

        let scroll = NSScrollView(frame: NSRect(x: 30, y: 52, width: 590, height: 175))
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        results = NSTextView(frame: scroll.bounds)
        results.isEditable = false
        results.isSelectable = true
        results.font = .systemFont(ofSize: 12)
        results.textContainerInset = NSSize(width: 10, height: 10)
        results.autoresizingMask = [.width]
        results.textContainer?.widthTracksTextView = true
        results.string = "默认逆时针 1°。可修改方向与角度，再拖入或选择 PDF。\n本批所有页面使用同一设置；支持 0.001° 至 180°。"
        scroll.documentView = results
        drop.addSubview(scroll)
        let version = label("v\(appVersion)  ·  \(authorLabel)", size: 12)
        version.textColor = .secondaryLabelColor
        version.frame = NSRect(x: 30, y: 18, width: 590, height: 20)
        drop.addSubview(version)
        if let preview = CommandLine.arguments.firstIndex(of: "--preview"),
           CommandLine.arguments.count > preview + 1 {
            window.displayIfNeeded()
            if let bitmap = drop.bitmapImageRepForCachingDisplay(in: drop.bounds) {
                drop.cacheDisplay(in: drop.bounds, to: bitmap)
                if let png = bitmap.representation(using: .png, properties: [:]) {
                    try? png.write(to: URL(fileURLWithPath: CommandLine.arguments[preview + 1]))
                }
            }
            NSApp.terminate(nil)
            return
        }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)

        // Also allow ordinary command-line invocation with PDF paths.
        let paths = launchFiles()
        if !paths.isEmpty { process(paths.map { URL(fileURLWithPath: $0) }) }
    }

    func application(_ sender: NSApplication, openFiles filenames: [String]) {
        DispatchQueue.main.async { [weak self] in
            self?.process(filenames.map { URL(fileURLWithPath: $0) })
        }
        sender.reply(toOpenOrPrint: .success)
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        window?.makeKeyAndOrderFront(nil)
        return true
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { !isProcessing }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if isProcessing {
            let alert = NSAlert()
            alert.messageText = "正在处理 PDF"
            alert.informativeText = "本批处理完成后即可退出。"
            alert.runModal()
            return .terminateCancel
        }
        return .terminateNow
    }

    @objc func chooseFiles() {
        guard !isProcessing else { return }
        let panel = NSOpenPanel()
        panel.title = "选择要旋转的 PDF"
        panel.allowedContentTypes = [.pdf]
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        if panel.runModal() == .OK { process(panel.urls) }
    }

    @objc func showAbout() {
        NSApp.orderFrontStandardAboutPanel(options: [.applicationName: "PDF 旋转工具", .applicationVersion: appVersion, .credits: NSAttributedString(string: authorLabel)])
    }

    @objc func revealOutputs() {
        guard !outputs.isEmpty else { return }
        NSWorkspace.shared.activateFileViewerSelecting(outputs)
    }

    func process(_ urls: [URL]) {
        guard !isProcessing else {
            status.stringValue = "当前批次正在处理，完成后可继续添加。"
            return
        }
        guard !urls.isEmpty else { return }
        let angleText = angleField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines).replacingOccurrences(of: ",", with: ".")
        guard let angle = Double(angleText), angle.isFinite, angle >= 0.001, angle <= 180 else {
            let alert = NSAlert()
            alert.messageText = "请输入有效角度"
            alert.informativeText = "角度范围为 0.001° 至 180°，支持小数，例如 0.5、1 或 1.25。请通过方向选项选择逆时针或顺时针。"
            alert.runModal()
            window.makeFirstResponder(angleField)
            return
        }
        let direction = directionChoice.indexOfSelectedItem == 1 ? "cw" : "ccw"
        let directionText = direction == "cw" ? "顺时针" : "逆时针"
        let batchDescription = "本批次：\(directionText) \(angleText)°"
        isProcessing = true
        outputs = []
        chooseButton.isEnabled = false
        revealButton.isEnabled = false
        angleField.isEnabled = false
        directionChoice.isEnabled = false
        window.standardWindowButton(.closeButton)?.isEnabled = false
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        progress.maxValue = Double(urls.count)
        progress.doubleValue = 0
        results.string = batchDescription

        DispatchQueue.global(qos: .userInitiated).async { [self] in
            var outputURLs: [URL] = []
            var lines: [String] = [batchDescription]
            var failures = 0
            var seen = Set<String>()
            for (index, url) in urls.enumerated() {
                let source = url.standardizedFileURL.resolvingSymlinksInPath()
                DispatchQueue.main.async {
                    self.status.stringValue = "正在处理 \(index + 1) / \(urls.count)：\(url.lastPathComponent)"
                }
                if seen.insert(source.path).inserted {
                    do {
                        let (code, output, error) = try runBackend(["--angle", angleText, "--direction", direction, source.path])
                        if code == 0 && !output.isEmpty {
                            let target = URL(fileURLWithPath: output)
                            outputURLs.append(target)
                            lines.append("✓ \(url.lastPathComponent) → \(target.lastPathComponent)")
                        } else {
                            failures += 1
                            lines.append("未处理：" + (error.isEmpty ? url.lastPathComponent : error))
                        }
                    } catch {
                        failures += 1
                        lines.append("未处理：\(url.lastPathComponent)：\(error.localizedDescription)")
                    }
                } else {
                    lines.append("已跳过重复输入：\(url.lastPathComponent)")
                }
                let snapshot = lines.joined(separator: "\n\n")
                DispatchQueue.main.async {
                    self.progress.doubleValue = Double(index + 1)
                    self.results.string = snapshot
                    self.results.scrollToEndOfDocument(nil)
                }
            }
            let finalOutputs = outputURLs
            let failureCount = failures
            DispatchQueue.main.async {
                self.outputs = finalOutputs
                self.isProcessing = false
                self.chooseButton.isEnabled = true
                self.angleField.isEnabled = true
                self.directionChoice.isEnabled = true
                self.revealButton.isEnabled = !finalOutputs.isEmpty
                self.window.standardWindowButton(.closeButton)?.isEnabled = true
                self.status.stringValue = "处理完成：成功 \(finalOutputs.count) 份，失败 \(failureCount) 份。"
            }
        }
    }
}

let application = NSApplication.shared
let delegate = AppDelegate()
application.delegate = delegate
application.run()
