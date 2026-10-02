# LDN Relay icon

`icon.jpg` is the approved 256 × 256 RGB JPEG embedded in the Switch NRO.
`icon-source.png` is the original generated artwork. The image is covered by
this project's MIT license. No Nintendo or Pokémon marks are used.

Generated with the built-in image-generation tool and approved before inclusion.
The JPEG is resized from the source with macOS `sips`, then metadata is stripped
with Pillow (only pixels are retained):

```sh
sips -z 256 256 -s format jpeg assets/icon-source.png --out assets/icon.jpg
python3 - <<'PY'
from PIL import Image
with Image.open("assets/icon.jpg") as image:
    clean = Image.frombytes("RGB", image.size, image.convert("RGB").tobytes())
clean.save("assets/icon.jpg", quality=95, subsampling=0, optimize=True)
PY
```

Generation prompt:

> Create one finished square app icon proposal for an open-source Nintendo Switch homebrew networking utility named LDN Relay. 1024 by 1024 composition. Flat, exceptionally simple geometric mark: two thick interlocking rounded link shapes, left cyan and right near-white, centered in the upper two thirds. Solid very dark navy background edge to edge, no rounded outer border, no gradients, glow, shadows, textures or 3D. Bold clean sans-serif uppercase text exactly 'LDN' centered underneath the link mark, large enough to read when reduced to 256x256. Spacious balanced margins. The link represents a relay between devices. No Nintendo logo, no Switch logo, no Pokemon, no extra text, no mockup, no watermark. Produce the icon itself only, not a presentation board.
