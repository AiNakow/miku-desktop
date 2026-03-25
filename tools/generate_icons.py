from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "Pillow is required to generate app icons. Install it with: pip install Pillow"
    ) from exc


def _prepare_square(image: Image.Image, size: int) -> Image.Image:
    rgba = image.convert("RGBA")
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    source_w, source_h = rgba.size
    scale = min(size / source_w, size / source_h)
    resized = rgba.resize((max(1, int(source_w * scale)), max(1, int(source_h * scale))), Image.Resampling.LANCZOS)
    offset = ((size - resized.width) // 2, (size - resized.height) // 2)
    canvas.paste(resized, offset, resized)
    return canvas


def _ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def generate_windows_ico(image: Image.Image, output_dir: Path, name: str) -> Path:
    sizes = [16, 24, 32, 48, 64, 128, 256]
    ico_path = output_dir / "windows" / f"{name}.ico"
    _ensure_dir(ico_path.parent)
    base = _prepare_square(image, 1024)
    base.save(ico_path, format="ICO", sizes=[(s, s) for s in sizes])
    return ico_path


def generate_linux_pngs(image: Image.Image, output_dir: Path, name: str) -> None:
    sizes = [16, 24, 32, 48, 64, 128, 256, 512]
    for size in sizes:
        path = output_dir / "linux" / "hicolor" / f"{size}x{size}" / "apps" / f"{name}.png"
        _ensure_dir(path.parent)
        _prepare_square(image, size).save(path, format="PNG")


def generate_macos_iconset(image: Image.Image, output_dir: Path, name: str) -> Path:
    iconset_dir = output_dir / "macos" / f"{name}.iconset"
    if iconset_dir.exists():
        shutil.rmtree(iconset_dir)
    _ensure_dir(iconset_dir)

    mapping = {
        16: ["icon_16x16.png", "icon_16x16@2x.png"],
        32: ["icon_32x32.png", "icon_32x32@2x.png"],
        128: ["icon_128x128.png", "icon_128x128@2x.png"],
        256: ["icon_256x256.png", "icon_256x256@2x.png"],
        512: ["icon_512x512.png", "icon_512x512@2x.png"],
    }

    for base_size, names in mapping.items():
        _prepare_square(image, base_size).save(iconset_dir / names[0], format="PNG")
        _prepare_square(image, base_size * 2).save(iconset_dir / names[1], format="PNG")

    return iconset_dir


def generate_icns_if_possible(iconset_dir: Path, output_dir: Path, name: str) -> Path | None:
    iconutil = shutil.which("iconutil")
    if iconutil is None:
        return None

    _ensure_dir(output_dir / "macos")
    icns_path = output_dir / "macos" / f"{name}.icns"
    if icns_path.exists():
        icns_path.unlink()

    subprocess.run(
        [iconutil, "-c", "icns", str(iconset_dir), "-o", str(icns_path)],
        check=True,
    )
    return icns_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate Windows/macOS/Linux app icons from a PNG source")
    parser.add_argument("--input", required=True, type=Path, help="Input PNG image")
    parser.add_argument("--output-dir", required=True, type=Path, help="Output directory")
    parser.add_argument("--name", default="mikupet", help="Icon base name")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.input.exists():
        print(f"Icon source not found: {args.input}", file=sys.stderr)
        return 2

    image = Image.open(args.input)
    generate_windows_ico(image, args.output_dir, args.name)
    generate_linux_pngs(image, args.output_dir, args.name)
    iconset_dir = generate_macos_iconset(image, args.output_dir, args.name)
    icns = generate_icns_if_possible(iconset_dir, args.output_dir, args.name)

    if icns is None:
        print("iconutil not found; generated .iconset only (run on macOS to produce .icns)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
