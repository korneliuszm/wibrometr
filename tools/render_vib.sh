#!/bin/sh
# Renders the vibration recorder's screen (env waveshare_vibration) on a PC into
# <output-dir>/vib-screens.png. Needs g++ with C++17, Python 3 with Pillow, and
# the Adafruit_GFX sources (PlatformIO has them after a build, e.g.
# .pio/libdeps/waveshare_vibration/Adafruit GFX Library).
# usage: tools/render_vib.sh <path-to-Adafruit-GFX-Library> [output-dir]
set -e
GFX=${1:?path to Adafruit-GFX-Library}
OUT=${2:-/tmp/wibrometr-render}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT" && cd "$OUT"
g++ -std=c++17 -w -DARDUINO=100 "-Dradians(d)=((d)*0.017453292519943295f)" -I"$ROOT/tools/stubs" -I"$GFX" -I"$ROOT/src/vibration" -I"$ROOT/src/waveshare" \
  -I"$ROOT/include" "$ROOT/tools/render_vib.cpp" "$ROOT/src/vibration/VibUi.cpp" "$GFX/Adafruit_GFX.cpp" -o render_vib
./render_vib
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
sheet = Image.new('RGB', (320 * cols + 10 * (cols - 1), 240 * rows + 10 * (rows - 1)), (60, 60, 60))
for i, im in enumerate(imgs):
    sheet.paste(im, ((i % cols) * 330, (i // cols) * 250))
sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
sheet.save('vib-screens.png')
print(files)
PY
