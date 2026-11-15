#!/usr/bin/env python3
import sys
import zipfile
from pathlib import Path

if len(sys.argv) < 3:
    print("Usage: package-twrp-zip.py <boot.img> <output.zip>")
    sys.exit(1)

boot_img_path = Path(sys.argv[1])
output_zip_path = Path(sys.argv[2])

if not boot_img_path.is_file():
    print(f"Error: {boot_img_path} not found")
    sys.exit(1)

update_binary = """#!/sbin/sh
OUTFD="$2"

ui_print() {
  printf 'ui_print %s\\nui_print\\n' "$1" >> "/proc/self/fd/$OUTFD"
}

ui_print "=========================================="
ui_print "  beyond2lte-ios Boot Flasher (TWRP ZIP)"
ui_print "=========================================="

BOOT_DEV="/dev/block/platform/13d60000.ufs/by-name/boot"
if [ ! -b "$BOOT_DEV" ]; then
  BOOT_DEV="/dev/block/by-name/boot"
fi

ui_print "Target partition: $BOOT_DEV"

ZIP_PATH="$3"
if [ -z "$ZIP_PATH" ]; then
  ZIP_PATH="$1"
fi

DIRNAME=$(dirname "$0")
UNZIP_DIR="/tmp/b2l_boot_flash"
rm -rf "$UNZIP_DIR"
mkdir -p "$UNZIP_DIR"

if [ -f "$DIRNAME/../../../boot.img" ]; then
  SRC_BOOT="$DIRNAME/../../../boot.img"
elif [ -f "/tmp/boot.img" ]; then
  SRC_BOOT="/tmp/boot.img"
else
  unzip -p "$ZIP_PATH" boot.img > "$UNZIP_DIR/boot.img" 2>/dev/null
  SRC_BOOT="$UNZIP_DIR/boot.img"
fi

if [ ! -s "$SRC_BOOT" ]; then
  ui_print "HATA: boot.img zip icinden cikarilamadi!"
  exit 1
fi

BOOT_SZ=$(wc -c < "$SRC_BOOT")
ui_print "Flashing boot.img ($BOOT_SZ bytes)..."

if dd if="$SRC_BOOT" of="$BOOT_DEV" bs=4M; then
  sync
  rm -rf "$UNZIP_DIR"
  ui_print "=========================================="
  ui_print "   BOOT.IMG BASARIYLA FLASHLANDI!"
  ui_print "=========================================="
  exit 0
else
  rm -rf "$UNZIP_DIR"
  ui_print "HATA: dd yazma basarisiz oldu!"
  exit 1
fi
"""

output_zip_path.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output_zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("META-INF/com/google/android/update-binary", update_binary)
    z.writestr("META-INF/com/google/android/updater-script", "")
    z.write(boot_img_path, arcname="boot.img")

print(f"Successfully packaged TWRP flashable zip: {output_zip_path} ({output_zip_path.stat().st_size} bytes)")
