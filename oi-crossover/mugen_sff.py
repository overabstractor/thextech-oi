"""
Lector de personajes MUGEN (SFF v1 + ACT + AIR) para sacar sus sprites como imágenes RGBA.

SFF v1: cabecera de 512 bytes y subarchivos enlazados; cada uno trae su offset (eje), grupo e imagen y un PCX de
256 colores. Los que reutilizan la paleta ("same palette") usan la del personaje: el .act, que en MUGEN está
guardado al revés (el color 0 es el último). El índice 0 es transparente.
"""
import io
import re
import struct

from PIL import Image


def read_act(path):
    raw = open(path, 'rb').read()[:768]
    cols = [tuple(raw[i:i + 3]) for i in range(0, 768, 3)]
    return list(reversed(cols))


def read_sff(path, act=None):
    """-> dict {(grupo, imagen): (Image RGBA, eje_x, eje_y)}"""
    data = open(path, 'rb').read()
    assert data[:11] == b'ElecbyteSpr', 'no es un SFF'
    ver = data[12:16]
    assert ver[3] == 1 or ver[2] == 1 or ver == b'\x00\x01\x00\x01', f'versión SFF no soportada {ver}'
    n_images = struct.unpack_from('<I', data, 20)[0]
    off = struct.unpack_from('<I', data, 24)[0]
    sprites = {}
    order = []
    prev_pal = None
    act_pal = act
    for idx in range(n_images):
        nxt, length, ax, ay, grp, img, linked, same_pal = struct.unpack_from('<IIhhHHHB', data, off)
        if length == 0:
            # enlazado: mismo gráfico que otro subarchivo
            src = order[linked] if linked < len(order) else None
            if src and src in sprites:
                im, _, _ = sprites[src]
                sprites[(grp, img)] = (im, ax, ay)
            order.append((grp, img))
            off = nxt
            continue
        pcx = data[off + 32: off + 32 + length]
        im = Image.open(io.BytesIO(pcx))
        im.load()
        if im.mode == 'L':
            # PCX sin paleta propia (comparte la del personaje): sus valores son índices de paleta.
            p_im = Image.frombytes('P', im.size, im.tobytes())
            cols = act_pal if act_pal else (prev_pal or [(i, i, i) for i in range(256)])
            p_im.putpalette([c for rgb in cols for c in rgb])
            im = p_im
            same_pal = 0
            prev_pal = cols
        if im.mode == 'P':
            pal = im.getpalette()[:768]
            # La paleta del PCX va al final (769 bytes); con "same palette" se usa la anterior / la del .act.
            if same_pal and (act_pal or prev_pal):
                cols = act_pal if act_pal else prev_pal
                flat = [c for rgb in cols for c in rgb]
                im.putpalette(flat)
            else:
                prev_pal = [tuple(pal[i:i + 3]) for i in range(0, 768, 3)]
                if act_pal is None:
                    act_pal = None
            idxs = im.copy()
            rgba = im.convert('RGBA')
            px = rgba.load()
            ip = idxs.load()
            for y in range(rgba.height):
                for x in range(rgba.width):
                    if ip[x, y] == 0:
                        px[x, y] = (0, 0, 0, 0)
        else:
            rgba = im.convert('RGBA')
        sprites[(grp, img)] = (rgba, ax, ay)
        order.append((grp, img))
        off = nxt
        if off == 0:
            break
    return sprites


def read_air(path):
    """-> dict {acción: [(grupo, imagen, dx, dy, ticks, flip)]}"""
    actions = {}
    cur = None
    for line in open(path, encoding='latin-1'):
        line = line.split(';')[0].strip()
        if not line:
            continue
        m = re.match(r'\[\s*Begin\s+Action\s+(-?\d+)\s*\]', line, re.I)
        if m:
            cur = int(m.group(1))
            actions[cur] = []
            continue
        if cur is None or ':' in line.split(',')[0] or line.lower().startswith(('clsn', 'loopstart', 'interpolate')):
            continue
        parts = [p.strip() for p in line.split(',')]
        if len(parts) >= 5 and re.match(r'^-?\d+$', parts[0]) and re.match(r'^-?\d+$', parts[1]):
            try:
                actions[cur].append((int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4]),
                                     parts[5] if len(parts) > 5 else ''))
            except ValueError:
                pass
    return actions
