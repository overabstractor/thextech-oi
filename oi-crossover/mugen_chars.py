"""
Personajes invitados a partir de personajes de MUGEN que aporta el usuario: sus sprites se convierten en las hojas de
los NPC 293-299 (sustituyen al pixel art de make_sprites.py).

  goku    Dragon Ball Advanced Adventure, por Nozywolf          -> 293 Goku, 294 bola de ki, 295 Kamehameha
  sonic   Modern Sonic, por xXPGlitz236                         -> 296 Sonic
  madara  Uchiha Madara, por Valodick (sprites de Nick Hyuuga)  -> 297 Madara, 298 kunai, 299 Katon (bola de fuego)

  - Cada fotograma se coloca con el eje del personaje (sus pies) en el mismo punto de la celda, así la animación no
    baila; la celda se ajusta a lo que ocupan todos y el eje queda centrado en horizontal (el motor centra el gráfico
    en la caja y lo refleja al mirar a la izquierda).
  - Los sprites de MUGEN miran a la derecha; la hoja lleva primero los reflejos (izquierda) y luego los originales.
  - El orden de fotogramas de cada personaje es el que espera oi_crossover.cpp (OI_CrossoverFrames).
  - Imprime el tamaño de celda y los píxeles que quedan bajo los pies (FrameOffsetY en OI_CrossoverSetup).

Uso: python mugen_chars.py <goku|sonic|madara> <carpeta del personaje MUGEN> <carpeta graphics/npc de destino>
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mugen_sff import read_act, read_sff  # noqa: E402

CHARS = {
    'goku': {
        'sff': 'goku.sff', 'act': 'goku.act', 'npc': 293,
        # 0-1 quieto, 2-5 andar, 6 en el aire, 7-8 cargar, 9-10 lanzar
        'frames': [(0, 0), (0, 4), (20, 0), (20, 2), (20, 4), (20, 6), (40, 4), (1000, 2), (1000, 4), (1000, 7),
                   (1000, 9)],
        'fixed': ((48, 56), (24, 55)),          # celda y eje de siempre (ya ajustados en el juego)
        'shots': [(294, [(902, 0), (902, 1)], (32, 32), 2, False),
                  (295, [(901, 1), (901, 2), (901, 3)], (72, 64), 2, True)],
    },
    'sonic': {
        'sff': 'sprite.sff', 'act': 'palette/Default.act', 'npc': 296,
        # 0 quieto, 1-4 correr, 5 en el aire, 6-7 bola cargando, 8-9 bola rodando
        'frames': [(0, 6), (100, 1), (100, 3), (100, 5), (100, 7), (41, 1), (60, 0), (60, 2), (62, 5), (62, 7)],
        'shots': [],
    },
    'madara': {
        'sff': 'vdMadara.sff', 'act': 'vdMadara.act', 'npc': 297,
        # 0-1 quieto, 2-5 andar, 6 en el aire, 7-8 sellos de mano, 9 lanzar kunai, 10 escupir fuego
        'frames': [(1, 0), (1, 2), (2, 1), (2, 3), (2, 5), (2, 7), (4, 0), (1320, 6), (1320, 8), (50, 1),
                   ((1320, 11), (0, 0, 44, 999))],   # la pose del Katon sin la bola (va aparte, en el 299)
        'shots': [(298, [(52, 1)], (32, 16), 1, True),
                  (299, [(1311, i) for i in range(6)], (88, 64), 1, True)],
    },
}


def frame(sp, key):
    """(imagen, eje_x, eje_y); key = (grupo, imagen) o ((grupo, imagen), recorte)."""
    if isinstance(key[0], tuple):
        (im, ax, ay), box = sp[key[0]], key[1]
        return im.crop((box[0], box[1], min(box[2], im.width), min(box[3], im.height))), ax - box[0], ay - box[1]
    return sp[key]


def char_sheet(sp, keys, fixed=None):
    fr = [frame(sp, k) for k in keys]
    if fixed:
        (cw, ch), (axc, ayc) = fixed
    else:
        # Lo que ocupa cada fotograma (píxeles visibles) respecto a su eje.
        left = right = up = down = 0
        for im, ax, ay in fr:
            bb = im.getbbox() or (0, 0, 1, 1)
            left = max(left, ax - bb[0])
            right = max(right, bb[2] - ax)
            up = max(up, ay - bb[1])
            down = max(down, bb[3] - ay)
        half = max(left, right)
        cw, ch = 2 * half, up + down
        cw += cw % 2
        ch += ch % 2
        axc, ayc = cw // 2, ch - down
    cells = []
    for im, ax, ay in fr:
        c = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
        c.paste(im, (axc - ax, ayc - ay), im)
        cells.append(c)
    return cells, (cw, ch), ch - ayc


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


def stack(cells, directional):
    """Hoja vertical; si es direccional, los reflejos (izquierda) primero y luego los originales."""
    w, h = cells[0].size
    seq = ([c.transpose(Image.FLIP_LEFT_RIGHT) for c in cells] if directional else []) + cells
    sheet = Image.new('RGBA', (w, h * len(seq)), (0, 0, 0, 0))
    for i, c in enumerate(seq):
        sheet.paste(c, (0, h * i))
    return sheet


def main(name, char_dir, out):
    spec = CHARS[name]
    sp = read_sff(os.path.join(char_dir, spec['sff']), read_act(os.path.join(char_dir, spec['act'])))
    cells, size, below = char_sheet(sp, spec['frames'], spec.get('fixed'))
    stack(cells, True).save(os.path.join(out, f"npc-{spec['npc']}.png"))
    print(f"{name}: npc-{spec['npc']} celda {size[0]}x{size[1]}, {len(cells)} fotogramas, {below} px bajo los pies")
    for npc, keys, cell, scale, directional in spec['shots']:
        stack([center(sp, k, cell, scale) for k in keys], directional).save(os.path.join(out, f'npc-{npc}.png'))
        print(f'  npc-{npc} celda {cell[0]}x{cell[1]}, {len(keys)} fotogramas')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
