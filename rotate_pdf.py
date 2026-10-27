#!/usr/bin/python3
"""Adjustable PDF rotation; default counterclockwise 1 degree; no rasterization."""

import argparse
import logging
import math
import os
from pathlib import Path
import sys
import tempfile
from decimal import Decimal

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))

from pypdf import PdfReader, PdfWriter, Transformation
from pypdf.generic import (
    ArrayObject,
    DecodedStreamObject,
    FloatObject,
    NameObject,
    RectangleObject,
)

logging.getLogger("pypdf").setLevel(logging.ERROR)
VERSION = "1.1.0"
AUTHOR = "linfeng365"
MIN_ANGLE = 0.001
MAX_ANGLE = 180.0
BOX_NAMES = ("/MediaBox", "/CropBox", "/BleedBox", "/TrimBox", "/ArtBox")


class RotationError(Exception):
    pass


def validate_rotation(angle=1.0, direction="ccw"):
    try:
        value = float(str(angle).strip().replace(",", "."))
    except (ValueError, TypeError):
        raise RotationError("请输入有效角度，例如 0.5、1 或 1.25。")
    if not math.isfinite(value) or not MIN_ANGLE <= value <= MAX_ANGLE:
        raise RotationError("角度范围为 0.001° 至 180°；请通过方向选项选择逆时针或顺时针。")
    if direction not in ("ccw", "cw"):
        raise RotationError("旋转方向必须为 ccw（逆时针）或 cw（顺时针）。")
    return value, direction


def rotation_suffix(angle, direction):
    number = format(Decimal(str(angle)).normalize(), "f")
    return "_" + ("逆时针" if direction == "ccw" else "顺时针") + number + "度"


def corners(rect):
    x0, y0, x1, y1 = map(float, rect)
    return ((x0, y0), (x0, y1), (x1, y0), (x1, y1))


def transformed_box(rect, transform):
    pts = [transform.apply_on(pt) for pt in corners(rect)]
    return RectangleObject((min(p[0] for p in pts), min(p[1] for p in pts),
                            max(p[0] for p in pts), max(p[1] for p in pts)))


def clip_to_original_view(page, crop):
    """Keep previously cropped content hidden when expanding the page."""
    content = page.get_contents()
    if content is None:
        return
    x0, y0, x1, y1 = map(float, crop)
    prefix = "q\n%.10f %.10f %.10f %.10f re W n\n" % (x0, y0, x1-x0, y1-y0)
    stream = DecodedStreamObject()
    stream.set_data(prefix.encode("ascii") + content.get_data() + b"\nQ\n")
    page.replace_contents(stream)


def rotate_page(page, signed_angle):
    # Materialize inherited boxes before making any change.
    media = RectangleObject(page.mediabox)
    crop = RectangleObject(page.cropbox)
    if not all(math.isfinite(float(v)) for v in list(media) + list(crop)):
        raise RotationError("页面尺寸无效。")
    if media.width <= 0 or media.height <= 0 or crop.width <= 0 or crop.height <= 0:
        raise RotationError("页面尺寸无效。")
    boxes = {name: RectangleObject(page[name]) for name in BOX_NAMES if name in page}
    boxes["/MediaBox"] = media
    boxes["/CropBox"] = crop
    cx = (float(crop.left) + float(crop.right)) / 2
    cy = (float(crop.bottom) + float(crop.top)) / 2
    transform = Transformation().translate(-cx, -cy).rotate(signed_angle)
    rotated_media = transformed_box(media, transform)
    transform = transform.translate(-float(rotated_media.left), -float(rotated_media.bottom))
    clip_to_original_view(page, crop)
    page.add_transformation(transform)
    for name, rect in boxes.items():
        page[NameObject(name)] = transformed_box(rect, transform)

    # Scanner-generated hyperlinks remain clickable in their new positions.
    for ref in page.get("/Annots", []):
        annotation = ref.get_object()
        if "/Rect" in annotation:
            annotation[NameObject("/Rect")] = transformed_box(annotation["/Rect"], transform)
        if "/QuadPoints" in annotation:
            values = annotation["/QuadPoints"]
            mapped = []
            for i in range(0, len(values), 2):
                mapped.extend(transform.apply_on((float(values[i]), float(values[i+1]))))
            annotation[NameObject("/QuadPoints")] = ArrayObject([FloatObject(v) for v in mapped])


def preflight(reader):
    if reader.is_encrypted:
        raise RotationError("文件已加密；请先导出未加密的 PDF 副本。")
    if not reader.pages:
        raise RotationError("文件没有页面。")
    root = reader.trailer["/Root"]
    if root.get("/Perms"):
        raise RotationError("文件含数字签名或签名权限；请使用未签名的扫描副本。")
    form = root.get("/AcroForm")
    if form is not None and form.get_object().get("/Fields"):
        raise RotationError("文件含表单或签名字段；请先导出普通扫描副本。")
    for i, page in enumerate(reader.pages, 1):
        for ref in page.get("/Annots", []):
            annotation = ref.get_object()
            if annotation.get("/Subtype") != "/Link":
                raise RotationError("第 %d 页含批注、表单或签名；请先导出不含这些对象的扫描副本。" % i)


def publish_without_overwrite(temp_path, source, suffix):
    """Publish without overwrite; Windows rename also works on exFAT/FAT32."""
    for number in range(1, 100001):
        extra = "" if number == 1 else "_%d" % number
        target = source.with_name(source.stem + suffix + extra + ".pdf")
        try:
            if os.name == "nt":
                # On Windows, rename raises FileExistsError if target exists.
                # The temp file is beside the source, so this stays on one volume.
                os.rename(str(temp_path), str(target))
            else:
                os.link(str(temp_path), str(target))
            return target
        except FileExistsError:
            continue
    raise RotationError("重名文件过多，请整理输出目录后重试。")


def process_file(filename, angle=1.0, direction="ccw"):
    angle, direction = validate_rotation(angle, direction)
    # PDF's y axis points up; positive angles rotate counterclockwise.
    signed_angle = angle if direction == "ccw" else -angle
    source = Path(filename).expanduser().resolve(strict=True)
    if not source.is_file() or source.suffix.lower() != ".pdf":
        raise RotationError("请拖入 PDF 文件。")
    temp_path = None
    try:
        with source.open("rb") as input_stream:
            reader = PdfReader(input_stream)
            preflight(reader)
            writer = PdfWriter(clone_from=reader)
            for page in writer.pages:
                rotate_page(page, signed_angle)
            fd, temp_name = tempfile.mkstemp(prefix=".pdf-rotate-", suffix=".tmp", dir=str(source.parent))
            temp_path = Path(temp_name)
            with os.fdopen(fd, "wb") as output_stream:
                writer.write(output_stream)
                output_stream.flush()
                os.fsync(output_stream.fileno())
            # Verify the written file can be opened and has every original page.
            with temp_path.open("rb") as output_stream:
                check = PdfReader(output_stream)
                if len(check.pages) != len(reader.pages):
                    raise RotationError("输出页数不一致，已取消保存。")
            return publish_without_overwrite(temp_path, source, rotation_suffix(angle, direction))
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description="PDF 旋转工具，默认逆时针 1°，在原文件旁另存副本。")
    parser.add_argument("files", nargs="*", help="一个或多个 PDF 文件")
    parser.add_argument("--angle", default="1", help="角度，0.001 至 180，支持小数，默认 1")
    parser.add_argument("--direction", default="ccw", choices=("ccw", "cw"), help="ccw 逆时针（默认）；cw 顺时针")
    parser.add_argument("--version", action="version", version="v%s · by：%s" % (VERSION, AUTHOR))
    parser.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.self_test:
        matrix = Transformation().rotate(1)
        x, y = matrix.apply_on((1, 0))
        if not (0 < x < 1 and y > 0):
            return 1
        print("OK: PDF 旋转工具 v%s · by：%s；运行环境和 PDF 引擎正常。" % (VERSION, AUTHOR))
        return 0
    if not args.files:
        parser.error("请指定 PDF 文件。")
    try:
        angle, direction = validate_rotation(args.angle, args.direction)
    except RotationError as exc:
        parser.error(str(exc))
    failed = False
    for filename in args.files:
        try:
            print(str(process_file(filename, angle, direction)))
        except Exception as exc:
            failed = True
            if isinstance(exc, PermissionError):
                message = "文件或所在文件夹权限不足，请复制到可读写的文件夹后重试。"
            elif isinstance(exc, FileNotFoundError):
                message = "找不到文件。"
            elif isinstance(exc, RotationError):
                message = str(exc)
            else:
                message = "无法处理此 PDF：" + str(exc)
            print(Path(filename).name + "：" + message, file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
