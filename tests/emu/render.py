"""PNG snapshots of the emulated OSD composited over a backdrop (--render)."""
import io
import pathlib

import numpy as np
from PIL import Image

import machine

W, H = 640, 480


def palette_rgba(entries):
    """256 x 0x0AYYUUVV -> (rgb uint8 [256,3], alpha float [256]); conversion as in src/info.c."""
    rgb = np.array([machine.yuv_rgb((e >> 16) & 0xFF, (e >> 8) & 0xFF, e & 0xFF) for e in entries], np.uint8)
    alpha = np.array([((e >> 24) & 0xF) / 15.0 for e in entries], np.float32)
    return rgb, alpha


def backdrop():
    """Stand-in for live view: hue sweep left to right, darker towards the bottom."""
    x = np.linspace(0, 1, W, dtype=np.float32)[None, :]
    y = np.linspace(0, 1, H, dtype=np.float32)[:, None]
    hue = x * 6
    r = np.clip(np.abs(hue - 3) - 1, 0, 1)
    g = np.clip(2 - np.abs(hue - 2), 0, 1)
    b = np.clip(2 - np.abs(hue - 4), 0, 1)
    img = np.stack([r, g, b], axis=-1) * 0.55 + 0.25
    img = img * (1 - 0.6 * y[..., None])
    return (img * 255).astype(np.float32)


class Renderer:
    def __init__(self, out_dir):
        self.out = pathlib.Path(out_dir)
        self.out.mkdir(parents=True, exist_ok=True)
        self.bg = backdrop()
        self.written = []

    def osd(self, m, name):
        """Backdrop, the shown full plane (layer 2, RGBA4444) and the OSD layer currently shown
        (the overlay's buffer while it is open)."""
        bg = self.bg
        if m.layers_on.get(2):
            full = m.u32(machine.A_BITMAPS + 4 * m.u8(machine.A_GSTATE + 1))
            px = np.frombuffer(m.rd(full, W * H * 2), '>u2').reshape(H, W).astype(np.uint32)
            rgb = np.stack([(px >> 12) & 0xF, (px >> 8) & 0xF, (px >> 4) & 0xF], axis=-1) * 17.0
            a = ((px & 0xF) / 15.0)[..., None]
            bg = rgb * a + bg * (1 - a)
        layer = m.layer.get(3) or m.u32(machine.A_BITMAPS + 4 * m.u8(machine.A_GSTATE))
        idx = np.frombuffer(m.rd(layer, W * H), np.uint8).reshape(H, W)
        rgb, alpha = palette_rgba(m.pal_shadow)
        a = alpha[idx][..., None]
        out = rgb[idx].astype(np.float32) * a + bg * (1 - a)
        self._save(Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)), name)

    def save_bmp(self, data, name):
        self._save(Image.open(io.BytesIO(data)).convert('RGB'), name)

    def _save(self, img, name):
        p = self.out / f'{name}.png'
        img.save(p)
        self.written.append(p)
