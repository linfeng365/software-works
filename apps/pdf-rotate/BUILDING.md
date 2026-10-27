# 构建与命令行用法

## 运行共用处理引擎

在 `apps/pdf-rotate` 目录中，使用 Python 3.9 或更新版本：

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python tests/selftest.py
python src/rotate_pdf.py --angle 0.5 --direction cw "/资料/合同.pdf"
```

Windows 中可用 `py -m venv .venv` 创建环境，再执行 `.venv\Scripts\activate`。

- `--angle`：0.001～180 的正数，默认 `1`。
- `--direction ccw`：逆时针，默认方向。
- `--direction cw`：顺时针。
- 可以在命令末尾传入多份 PDF 的完整路径。

## Mac 原生界面

需要 macOS、Apple Command Line Tools、Swift 编译器和已安装依赖的 Python。下面命令在应用目录执行：

```bash
PDFROTATE_APP="$PWD/dist/PDF旋转工具.app"
mkdir -p "$PDFROTATE_APP/Contents/MacOS" "$PDFROTATE_APP/Contents/Resources"
cp src/macos/Info.plist "$PDFROTATE_APP/Contents/Info.plist"
cp src/rotate_pdf.py "$PDFROTATE_APP/Contents/Resources/rotate_pdf.py"
python -m pip install --no-compile --target "$PDFROTATE_APP/Contents/Resources/lib" -r requirements.txt
swiftc -swift-version 5 -O -framework AppKit src/macos/PDFRotate.swift -o "$PDFROTATE_APP/Contents/MacOS/PDFRotate"
codesign --force --sign - "$PDFROTATE_APP"
```

Mac 窗口调用 `/usr/bin/python3`。目标机器需要该路径下有可运行的 Python 3.9 或更新版本；依赖随应用打包。发行的 Mac 包面向 Apple Silicon。

## Windows 原生窗口交叉编译

在 Mac 上使用 Zig 0.15.2：

```bash
sh src/windows/build-launcher-macos.sh /absolute/path/to/zig
```

该脚本只构建原生窗口 EXE，输出在 `src/PDFRotate.exe`。完整便携包还需按以下结构放置官方 Windows Python 嵌入式环境和处理引擎：

```text
PDFRotate.exe
runtime/                    Python 3.13.17 amd64 嵌入式发行包
resources/
  rotate_pdf.py
  selftest.py
  lib/                      pypdf 与必要依赖
```

已发布的完整 Windows 下载包已包含这些文件。对应 Python 运行环境只能在 Windows 上运行；跨平台编译成功不等于 Windows 实机验证通过。
