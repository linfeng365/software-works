"""Offline smoke test for the delivered Windows package; no external files."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from pypdf import PdfReader, PdfWriter
from pypdf.generic import DecodedStreamObject
import rotate_pdf


def main():
    with tempfile.TemporaryDirectory(prefix="pdf-rotate-check-") as directory:
        folder = Path(directory) / "中文 空格 (测试)"
        folder.mkdir()
        source = folder / "扫描.pdf"
        writer = PdfWriter()
        page = writer.add_blank_page(width=600, height=800)
        content = DecodedStreamObject()
        content.set_data(b"1 0 0 RG 4 w 100 400 m 500 400 l S\n")
        page.replace_contents(content)
        writer.add_blank_page(width=800, height=600).rotate(90)
        writer.write(source)
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        first = rotate_pdf.process_file(source)
        second = rotate_pdf.process_file(source)
        assert first.name == "扫描_逆时针1度.pdf"
        assert second.name == "扫描_逆时针1度_2.pdf"
        assert first != second and first.exists() and second.exists()
        assert hashlib.sha256(source.read_bytes()).hexdigest() == before
        result = PdfReader(first)
        assert len(result.pages) == 2 and result.pages[1].rotation == 90
        assert 613 < float(result.pages[0].mediabox.width) < 615
        matrices = [operands for operands, op in result.pages[0].get_contents().operations if op == b"cm"]
        assert matrices and 0.017 < float(matrices[0][1]) < 0.018
        assert -0.018 < float(matrices[0][2]) < -0.017
        # Exercise non-default values and both directions through the public API.
        ccw = rotate_pdf.process_file(source, 0.5, "ccw")
        cw = rotate_pdf.process_file(source, 1.25, "cw")
        assert ccw.name == "扫描_逆时针0.5度.pdf"
        assert cw.name == "扫描_顺时针1.25度.pdf"
        cw_ops = PdfReader(cw).pages[0].get_contents().operations
        cw_matrix = next(values for values, op in cw_ops if op == b"cm")
        assert float(cw_matrix[1]) < 0 and float(cw_matrix[2]) > 0
        quarter = PdfReader(rotate_pdf.process_file(source, 90, "ccw")).pages[0]
        assert abs(float(quarter.mediabox.width)-800) < 0.00001
        assert abs(float(quarter.mediabox.height)-600) < 0.00001
        half = PdfReader(rotate_pdf.process_file(source, 180, "cw")).pages[0]
        assert abs(float(half.mediabox.width)-600) < 0.00001
        assert abs(float(half.mediabox.height)-800) < 0.00001
        before_invalid = set(folder.iterdir())
        for invalid in (0, -1, 181, "nan", "inf", "", None):
            try:
                rotate_pdf.process_file(source, invalid)
            except rotate_pdf.RotationError:
                pass
            else:
                raise AssertionError("invalid angle accepted: " + repr(invalid))
        assert set(folder.iterdir()) == before_invalid
        bad = folder / "损坏.pdf"
        bad.write_bytes(b"broken pdf")
        run = subprocess.run([sys.executable, "-I", "-X", "utf8", "-B", str(Path(rotate_pdf.__file__)),
                              str(bad), str(source)], capture_output=True, text=True, encoding="utf-8")
        assert run.returncode == 1 and "损坏.pdf" in run.stderr
        assert "_逆时针1度_3.pdf" in run.stdout
        assert not list(folder.glob(".pdf-rotate-*"))
        print("通过：默认逆时针 1°、小数角度、顺时针/逆时针、输入校验、多页、中文路径、重名编号与原件保护。")
        print("PDF 旋转工具 v1.1.0 · by：linfeng365")
        print("平台：", sys.platform, "；Python：", sys.version.split()[0])
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print("自检失败：", repr(exc), file=sys.stderr)
        sys.exit(1)
