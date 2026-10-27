# linfeng365 的个人软件作品

**by：linfeng365**

这里集中存放个人开发的软件作品、源码、使用说明和版本记录。每个作品放在独立目录，完整应用下载包统一放在 [Releases](https://github.com/linfeng365/software-works/releases)。

## 作品目录

| 编号 | 作品 | 当前版本 | 支持平台 | 介绍与源码 | 下载 |
|---|---|---|---|---|---|
| 01 | PDF 旋转工具 | v1.1.0 | macOS Apple Silicon / Windows 10、11 x64 | [查看作品](apps/pdf-rotate/) | [下载本版](https://github.com/linfeng365/software-works/releases/tag/pdf-rotate-v1.1.0) |

## 第一件作品：PDF 旋转工具

用于扫描 PDF 的小角度校正。默认**逆时针旋转 1°**，支持选择顺时针或逆时针，并输入 **0.001°～180°** 的小数角度。

- 多份 PDF 拖入或多选，逐份批量处理。
- 原文件保留，副本保存在原文件旁；文件名标明方向与角度，重名自动编号。
- 直接变换 PDF 内容坐标，不重新压缩原扫描图片；已有 OCR 文字层一起旋转。
- 为保留旋转后的四角，页面尺寸会略微扩大。

### 直接下载

- [Mac Apple Silicon · v1.1.0](https://github.com/linfeng365/software-works/releases/download/pdf-rotate-v1.1.0/PDF-Rotate-macOS-arm64-v1.1.0.zip)
- [Windows 10/11 64 位 · v1.1.0](https://github.com/linfeng365/software-works/releases/download/pdf-rotate-v1.1.0/PDF-Rotate-Windows-x64-v1.1.0.zip)

Windows 请完整解压后启动 `PDFRotate.exe`，保留包内 `runtime` 和 `resources`；Mac 打开 `PDF旋转工具.app`。

### 验证状态

Mac v1.1.0 已完成原生应用运行与 PDF 输出检查。Windows v1.1.0 已交叉编译并完成结构检查，新版窗口和控件尚未在 Windows 环境运行；可用包内 `SELFTEST.cmd` 和人工拖放复测。详细记录见 [验证说明](apps/pdf-rotate/validation/README.md)。

## 仓库结构

```text
apps/
  pdf-rotate/     PDF 旋转工具：说明、源码、构建方法与验证记录
```

后续新作品可按 `apps/作品英文名/` 添加，并在上面的作品目录增加一行。各应用使用独立 Release 标签，例如 `pdf-rotate-v1.1.0`。

第三方组件的许可证随应用保留，见 [PDF 旋转工具的第三方说明](apps/pdf-rotate/THIRD_PARTY_NOTICES.md)。
