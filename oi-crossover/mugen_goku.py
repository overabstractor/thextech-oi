"""
Goku a partir de un personaje de MUGEN (el de "Dragon Ball Advanced Adventure", autor Nozywolf) que aporta el
usuario: sus sprites sustituyen al pixel art de make_sprites.py en los NPC 293 (Goku), 294 (bola de energía) y 295
(Kamehameha, la bola grande).

  - Cada fotograma se coloca en una celda fija con el eje del personaje (sus pies) en el mismo punto, así la
    animación no baila. Los sprites de MUGEN miran a la derecha; los de la izquierda son el reflejo.
  - Orden de fotogramas del 293 (oi_crossover.cpp): 0-1 quieto, 2-5 andar, 6 en el aire, 7-8 cargar, 9-10 lanzar.

Uso: python mugen_goku.py <carpeta del personaje MUGEN> <carpeta graphics/npc de destino>
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mugen_sff import read_act, read_sff  # noqa: E402

GOKU_FRAMES = [(0, 0), (0, 4), (20, 0), (20, 2), (20, 4), (20, 6), (40, 4), (1000, 2), (1000, 4), (1000, 7), (1000, 9)]
CELL = (48, 56)
ANCHOR = (24, 55)           # eje (pies) dentro de la celda


def place(sp, key, cell, anchor, scale=1):
    im, ax, ay = sp[key]
    if scale != 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        ax, ay = ax * scale, ay * scale
    out = Image.new('RGBA', cell, (0, 0, 0, 0))
    out.paste(im, (anchor[0] - ax, anchor[1] - ay), im)
    return out


def center(sp, key, cell, scale=1):
    """Proyectiles: el dibujo centrado en la celda (su eje de MUGEN es el de la mano que lo lanza)."""
    im = sp[key][0]
    bb = im.getbbox()
    if bb:
        im = im.crop(bb)
    if scale != 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.thumbnail(cell, Image.NEAREST)
    out = Image.new('RGBA', cell, (0, 0, 0, 0))
    out.paste(im, ((cell[0] - im.width) // 2, (cell[1] - im.height) // 2), im)
    return out


def directional(cells):
    """[derecha...] -> hoja con los reflejos (izquierda) primero y luego los originales."""
    w, h = cells[0].size
    n = len(cells)
    sheet = Image.new('RGBA', (w, h * n * 2), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        sheet.paste(c.transpose(Image.FLIP_LEFT_RIGHT), (0, h * i))
        sheet.paste(c, (0, h * (n + i)))
    return sheet


def main(char_dir, out):
    sp = read_sff(os.path.join(char_dir, 'goku.sff'), read_act(os.path.join(char_dir, 'goku.act')))

    goku = [place(sp, k, CELL, ANCHOR) for k in GOKU_FRAMES]
    directional(goku).save(os.path.join(out, 'npc-293.png'))

    # Bola de energía pequeña (902): centrada en 32x32, al doble.
    ki = [center(sp, (902, i), (32, 32), scale=2) for i in (0, 1)]
    strip = Image.new('RGBA', (32, 64), (0, 0, 0, 0))
    for i, c in enumerate(ki):
        strip.paste(c, (0, 32 * i))
    strip.save(os.path.join(out, 'npc-294.png'))

    # Kamehameha: la bola grande con su estela (901, 1-3) al doble, mirando a donde va.
    big = [center(sp, (901, i), (72, 64), scale=2) for i in (1, 2, 3)]
    directional(big).save(os.path.join(out, 'npc-295.png'))
    print('Goku de MUGEN en', out)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
