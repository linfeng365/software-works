# PDF 旋转工具

**v1.1.0 · by：linfeng365**

为扫描 PDF 做小角度校正，支持 Mac 和 Windows 原生窗口。默认逆时针 1°，方向与数值分开设置。

## 下载

| 平台 | 完整应用包 |
|---|---|
| macOS · Apple Silicon | [PDF-Rotate-macOS-arm64-v1.1.0.zip](https://github.com/linfeng365/software-works/releases/download/pdf-rotate-v1.1.0/PDF-Rotate-macOS-arm64-v1.1.0.zip) |
| Windows 10/11 · x64 | [PDF-Rotate-Windows-x64-v1.1.0.zip](https://github.com/linfeng365/software-works/releases/download/pdf-rotate-v1.1.0/PDF-Rotate-Windows-x64-v1.1.0.zip) |

[发布说明与全部附件](https://github.com/linfeng365/software-works/releases/tag/pdf-rotate-v1.1.0)

## 使用

1. 完整解压应用包并打开程序。
2. 选择“逆时针”或“顺时针”，输入角度，如 `1`、`0.5`、`1.25`。
3. 拖入多个 PDF，或点“选择 PDF…”多选。
4. 处理完成后，在原文件旁查看副本。

角度范围为 **0.001° 至 180°**。每次启动默认为逆时针 1°；本批开始后设置暂时锁定。直接把文件拖到尚未运行的程序图标上，会使用默认设置。

```text
合同.pdf
合同_逆时针1度.pdf
合同_顺时针0.5度.pdf
合同_顺时针0.5度_2.pdf
```

## 处理特点

- 原件和重名副本均不覆盖，一份失败后继续处理其余文件。
- 扫描图片、正文与已有 OCR 文字一起旋转，不把页面转成图片，不重新压缩原扫描图片。
- 为保留四角，页面及可见边界略微扩大，纸张尺寸会略增。
- 原裁切区域以外的隐藏内容继续保持隐藏。
- 适用于普通扫描 PDF；加密、数字签名、批注和表单文件会提示跳过。普通超链接可保留。

## 文档与源码

- [Mac 使用说明](docs/macOS.md)
- [Windows 使用说明](docs/Windows.md)
- [版本记录](CHANGELOG.md)
- [构建与命令行用法](BUILDING.md)
- [验证范围](validation/README.md)
- [第三方组件与许可证](THIRD_PARTY_NOTICES.md)

```text
src/rotate_pdf.py       两端共用的 PDF 处理引擎
src/macos/             Swift / AppKit 界面与 Info.plist
src/windows/           Windows C 界面、manifest、版本资源与交叉编译脚本
tests/selftest.py      可在本机运行的处理引擎自检
validation/            当前版本的验证记录
```
