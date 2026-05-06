"""Generate Windows .ico, macOS .icns, and PNG icons from source PNG.

Uses PNG-compressed ICO frames (Vista+) — no hand-written BMP DIB, no row-order
bugs, no vertical flip.
"""
import argparse
import io
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

SIZES = [16, 24, 32, 48, 64, 128, 256]
PADDING_PCT = 0.06  # 6% transparent margin around content


def find_source() -> Path:
    """Auto-detect source image from common locations."""
    candidates = [
        Path("resources/icons/app-icon-source.png"),
        Path("app-icon-source.png"),
    ]
    for c in candidates:
        if c.exists():
            return c
    sys.exit("Source image not found. Tried: resources/icons/app-icon-source.png, app-icon-source.png")


def make_master(img: Image.Image, master_size: int = 1024) -> Image.Image:
    """
    Contain source image inside a master_size×master_size transparent canvas.
    Maintains aspect ratio, centers, applies padding. No crop, no stretch, no flip.
    """
    if img.mode != "RGBA":
        img = img.convert("RGBA")

    w, h = img.size

    # First, ensure square: if not square, center on transparent square canvas
    if w != h:
        side = max(w, h)
        square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        square.paste(img, ((side - w) // 2, (side - h) // 2))
        img = square
        w, h = side, side

    # Calculate contain dimensions on master canvas with padding
    content_size = int(master_size * (1.0 - PADDING_PCT * 2))
    scale = content_size / max(w, h)
    new_w = max(1, int(w * scale))
    new_h = max(1, int(h * scale))

    resized = img.resize((new_w, new_h), Image.LANCZOS)

    master = Image.new("RGBA", (master_size, master_size), (0, 0, 0, 0))
    ox = (master_size - new_w) // 2
    oy = (master_size - new_h) // 2
    master.paste(resized, (ox, oy))
    return master


def make_frame(master: Image.Image, size: int) -> Image.Image:
    """Resize master down to size×size with Lanczos."""
    return master.resize((size, size), Image.LANCZOS)


def save_ico(frames: dict[int, Image.Image], out_path: Path) -> None:
    """
    Save multi-resolution .ico using PNG-compressed frames.

    Each frame is stored as a standalone PNG inside the ICO container.
    No BMP DIB headers, no BGRA byte-twiddling, no scanline-order bugs.
    Supported by Windows Vista and later.
    """
    num = len(SIZES)
    header = struct.pack("<HHH", 0, 1, num)

    dir_entries = b""
    image_chunks = b""
    data_offset = 6 + num * 16

    for s in SIZES:
        frame = frames[s]
        if frame.mode != "RGBA":
            frame = frame.convert("RGBA")

        buf = io.BytesIO()
        frame.save(buf, format="PNG")
        png_data = buf.getvalue()

        w_entry = s if s < 256 else 0
        h_entry = s if s < 256 else 0

        entry = struct.pack("<BBBBHHII",
            w_entry, h_entry, 0, 0, 1, 32, len(png_data), data_offset)
        dir_entries += entry
        image_chunks += png_data
        data_offset += len(png_data)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(header + dir_entries + image_chunks)

    print(f"  .ico -> {out_path} ({num} PNG-compressed frames: {', '.join(str(s) for s in SIZES)})")


def save_pngs(frames: dict[int, Image.Image], out_dir: Path, prefix: str = "preview") -> None:
    """Save preview PNGs for each frame size."""
    for s in SIZES:
        p = out_dir / f"{prefix}_{s}x{s}.png"
        frames[s].save(str(p), format="PNG")
        print(f"  PNG -> {p}")


def readback_ico(ico_path: Path, out_dir: Path) -> dict[int, Image.Image]:
    """
    Read every frame from the generated .ico and export as PNG for verification.
    Returns dict of {size: Image}.
    """
    readback = {}
    img = Image.open(str(ico_path))
    n = getattr(img, "n_frames", 1)
    print(f"\n[ICO readback] {n} frame(s) detected by Pillow")
    for i in range(n):
        img.seek(i)
        size = img.size
        readback[size[0]] = img.copy()
        rb_path = out_dir / f"ico_readback_{size[0]}x{size[1]}.png"
        img.save(str(rb_path), format="PNG")
        print(f"  readback frame {i}: {size} -> {rb_path}")
    img.close()
    return readback


def save_icns(img: Image.Image, iconset_dir: Path, out_path: Path) -> None:
    """Build .icns (macOS only, falls back to minimal writer)."""
    iconset_dir.mkdir(parents=True, exist_ok=True)

    iconset_spec = [
        (16, "icon_16x16.png"),
        (32, "icon_16x16@2x.png"),
        (32, "icon_32x32.png"),
        (64, "icon_32x32@2x.png"),
        (128, "icon_128x128.png"),
        (256, "icon_128x128@2x.png"),
        (256, "icon_256x256.png"),
        (512, "icon_256x256@2x.png"),
        (512, "icon_512x512.png"),
        (1024, "icon_512x512@2x.png"),
    ]
    for size, name in iconset_spec:
        f = make_frame(img, size)
        f.save(str(iconset_dir / name), format="PNG")

    import shutil
    if shutil.which("iconutil"):
        import subprocess
        subprocess.run(["iconutil", "-c", "icns", str(iconset_dir), "-o", str(out_path)], check=True)
        print(f"  .icns -> {out_path} (via iconutil)")
    else:
        write_minimal_icns(img, out_path)
        if out_path.exists():
            print(f"  .icns -> {out_path} (minimal)")


def write_minimal_icns(img: Image.Image, out_path: Path) -> None:
    """Minimal .icns with ic07-ic10 (ARGB data)."""
    data = {}
    for size, icn_type in [(128, b"ic07"), (256, b"ic08"), (512, b"ic09"), (1024, b"ic10")]:
        f = make_frame(img, size)
        if f.mode != "RGBA":
            f = f.convert("RGBA")
        pixels = f.tobytes()
        argb = bytearray()
        for i in range(0, len(pixels), 4):
            r, g, b, a = pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]
            argb.extend([a, r, g, b])
        data[icn_type] = bytes(argb)

    entries = []
    for icn_type, icn_data in data.items():
        entry_len = 8 + len(icn_data)
        entries.append(icn_type + struct.pack(">I", entry_len) + icn_data)

    total_size = 8 + sum(len(e) for e in entries)
    header = b"icns" + struct.pack(">I", total_size)
    with open(out_path, "wb") as f:
        f.write(header)
        for e in entries:
            f.write(e)


def verify_direction(preview_frames: dict[int, Image.Image],
                     readback_frames: dict[int, Image.Image]) -> bool:
    """
    Compare preview and readback frames pixel-by-pixel.
    Returns True if all matching sizes are identical (no flip, no mirror).
    """
    all_ok = True
    for s in SIZES:
        if s not in readback_frames:
            print(f"  WARN: size {s}x{s} not found in ICO readback")
            all_ok = False
            continue

        preview = preview_frames[s]
        readback = readback_frames[s]

        if preview.size != readback.size:
            print(f"  FAIL: {s}x{s} size mismatch preview={preview.size} vs readback={readback.size}")
            all_ok = False
            continue

        p_px = preview.tobytes()
        r_px = readback.tobytes()

        if p_px == r_px:
            print(f"  OK: {s}x{s} — pixel-identical")
        else:
            # Count differing pixels for diagnosis
            diffs = sum(1 for i in range(len(p_px)) if p_px[i] != r_px[i])
            print(f"  DIFF: {s}x{s} — {diffs} bytes differ out of {len(p_px)}")
            all_ok = False

    return all_ok


def main():
    parser = argparse.ArgumentParser(description="Generate desktop icons from a source PNG")
    parser.add_argument("--source", type=Path, default=None,
                        help="Source PNG image (auto-detect if omitted)")
    parser.add_argument("--out-dir", type=Path, default=Path("resources/icons"),
                        help="Output directory")
    parser.add_argument("--ico-name", default="app_icon.ico", help=".ico filename")
    parser.add_argument("--icns-name", default="app_icon.icns", help=".icns filename")
    parser.add_argument("--master-size", type=int, default=1024,
                        help="Master canvas size (default: 1024)")
    args = parser.parse_args()

    src_path = args.source if args.source else find_source()
    if not src_path.exists():
        sys.exit(f"Source image not found: {src_path}")

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    # --- 1. Load source ---
    img = Image.open(src_path)
    print(f"Source: {src_path} ({img.width}x{img.height}, mode={img.mode})")

    # --- 2. Build master ---
    print(f"\n[Master] contain + {PADDING_PCT*100:.0f}% padding -> {args.master_size}x{args.master_size}")
    master = make_master(img, args.master_size)
    master_path = out_dir / "master_icon.png"
    master.save(str(master_path), format="PNG")
    print(f"  saved: {master_path}")

    # --- 3. Generate frames ---
    print("\n[Frames]")
    frames = {}
    for s in SIZES:
        frames[s] = make_frame(master, s)
        print(f"  {s}x{s}")

    # --- 4. Preview PNGs ---
    print("\n[Preview PNGs]")
    save_pngs(frames, out_dir, prefix="preview")

    # --- 5. Windows .ico (PNG-compressed, no DIB) ---
    print("\n[Windows .ico — PNG-compressed frames]")
    ico_path = out_dir / args.ico_name
    save_ico(frames, ico_path)

    # --- 6. Readback verification ---
    print("\n[Readback verification]")
    readback = readback_ico(ico_path, out_dir)

    # --- 7. Direction check ---
    print("\n[Direction check: preview vs ICO readback]")
    ok = verify_direction(frames, readback)
    if ok:
        print("  RESULT: All frames identical — no flip, no mirror.")
    else:
        print("  RESULT: MISMATCH detected — review the diffs above.")

    # --- 8. macOS .icns ---
    print("\n[macOS .icns]")
    iconset_dir = out_dir / "app.iconset"
    icns_path = out_dir / args.icns_name
    save_icns(master, iconset_dir, icns_path)

    print(f"\nDone. All icons in: {out_dir}")


if __name__ == "__main__":
    main()
