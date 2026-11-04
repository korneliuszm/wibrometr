#!/bin/sh
# Renders all screens on a PC into docs/screens.png. Needs a g++ with C++17,
# Python 3 with Pillow, and the Adafruit_GFX / Adafruit BusIO sources, e.g.
#   git clone --depth 1 --branch 1.12.1 https://github.com/adafruit/Adafruit-GFX-Library
# usage: tools/render_screens.sh <path-to-Adafruit-GFX-Library> <output-dir>
set -e
GFX=${1:?path to Adafruit-GFX-Library}
OUT=${2:-/tmp/wibrometr-render}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT" && cd "$OUT"
g++ -std=c++17 -w -DARDUINO=100 -I"$ROOT/tools/stubs" -I"$GFX" -I"$ROOT/src/waveshare" -I"$ROOT/src" -I"$ROOT/include" \
  "$ROOT/tools/render_screens.cpp" "$ROOT/src/waveshare/ui/Screens.cpp" "$ROOT/src/waveshare/ui/LineScreens.cpp" "$GFX/Adafruit_GFX.cpp" -o render
./render
python3 - <<'PY'
import glob, struct
from PIL import Image
files = sorted(glob.glob('*.raw'))
imgs = []
for f in files:
    data = open(f, 'rb').read()
    im = Image.new('RGB', (320, 240))
    im.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
                for v in struct.unpack('<%dH' % (len(data) // 2), data)])
    imgs.append(im)
cols = 2
rows = (len(imgs) + cols - 1) // cols
sheet = Image.new('RGB', (320 * cols + 10 * (cols - 1), 240 * rows + 10 * (rows - 1)), (40, 40, 40))
for i, im in enumerate(imgs):
    sheet.paste(im, ((i % cols) * 330, (i // cols) * 250))
sheet.save('screens.png')
print(files)
PY
