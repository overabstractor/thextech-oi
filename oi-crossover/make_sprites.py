"""
Sprites de los personajes invitados (NPC 293-298 del fork): pixel art propio a 16 px por celda, escalado x2 como
los del juego. Cada hoja lleva primero los fotogramas mirando a la izquierda y luego los mismos a la derecha
(el motor no refleja solo). Uso: python make_sprites.py <carpeta graphics/npc de destino>
"""
import os
import sys
from PIL import Image

PAL = {
    '.': None,
    'K': (24, 24, 24), 'S': (252, 200, 152), 'O': (248, 124, 28), 'B': (36, 76, 208), 'W': (255, 255, 255),
    'C': (130, 206, 255), 'D': (40, 120, 255), 'Y': (252, 222, 40), 'y': (214, 170, 10), 'G': (186, 186, 204),
    'N': (26, 40, 110), 'R': (224, 28, 28), 'b': (24, 92, 236), 'd': (8, 42, 160), 'P': (252, 200, 152),
}


def grid(rows, pal=PAL):
    h = len(rows)
    w = max(len(r) for r in rows)
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            col = pal.get(c)
            if col:
                im.putpixel((x, y), col + (255,))
    return im


def patch(base, rows_from, new_rows):
    out = list(base)
    for i, r in enumerate(new_rows):
        out[rows_from + i] = r
    return out


def sheet(frames_right, cell=(16, 24), scale=2):
    """frames_right: lista de rejillas mirando a la derecha -> hoja [izquierda..., derecha...]."""
    w, h = cell
    ims = [grid(f) for f in frames_right]
    n = len(ims)
    out = Image.new('RGBA', (w * scale, h * scale * n * 2), (0, 0, 0, 0))
    for i, im in enumerate(ims):
        big = im.resize((w * scale, h * scale), Image.NEAREST)
        out.paste(big.transpose(Image.FLIP_LEFT_RIGHT), (0, h * scale * i))       # izquierda
        out.paste(big, (0, h * scale * (n + i)))                                   # derecha
    return out


def strip(frames, cell, scale=2):
    """Sin dirección: los fotogramas uno debajo de otro."""
    w, h = cell
    out = Image.new('RGBA', (w * scale, h * scale * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(grid(f).resize((w * scale, h * scale), Image.NEAREST), (0, h * scale * i))
    return out


# ── Goku ────────────────────────────────────────────────────────────────────────
GOKU = [
    '......K..K.K....',
    '....K.KKKKKK....',
    '...KKKKKKKKKK...',
    '..KKKKKKKKKKKK..',
    '...KKKKKKKKKK...',
    '..KKKSSKKSSKK...',
    '...KSSSSSSSSK...',
    '....SSSSKSSKS...',
    '....SSSSSSSSS...',
    '.....SSSSSS.....',
    '.....SSSSSS.....',
    '....OOOBBOOO....',
    '...OOOOBBOOOO...',
    '..SOOOOOBOOOOS..',
    '..SSOOOOOOOOSS..',
    '..SB.OOOOOO.BS..',
    '.....BBBBBB.....',
    '.....OOOOOO.....',
    '.....OOO.OOO....',
    '.....OOO.OOO....',
    '.....OO...OO....',
    '....BBB...BBB...',
    '....BBB...BBB...',
    '................',
]
GOKU_WALK = patch(GOKU, 17, [
    '.....OOOOOO.....',
    '....OOO..OOO....',
    '...OOO....OOO...',
    '...OO......OO...',
    '..BBB......BBB..',
    '..BBB......BBB..',
])
GOKU_CHARGE = patch(GOKU, 11, [
    '....OOOBBOOO....',
    '...OOOOBBOOOO...',
    '.CSOOOOOBOOOO...',
    'CDSSOOOOOOOO....',
    '.CS..OOOOOO.....',
])
GOKU_FIRE = patch(GOKU, 11, [
    '....OOOBBOOO....',
    '...OOOOBBOOOOSSC',
    '...OOOOOBOOOOSSD',
    '....OOOOOOOO..C.',
    '.....OOOOOO.....',
])

# ── Sonic ───────────────────────────────────────────────────────────────────────
SONIC = [
    '................',
    '................',
    '......bbbb......',
    '....bbbbbbb.....',
    '..bbbbbbbbbb....',
    'dbbbbbbbWWWb....',
    '..bbbbbbWKWWb...',
    '.dbbbbbbWKWPP...',
    '...bbbbbPPPPPK..',
    '.dbbbbbbbPPPP...',
    '...bbbbbbbPP....',
    '.....bbbbbb.....',
    '....W.bPPPb.W...',
    '...WW.bPPPb.WW..',
    '......bPPPb.....',
    '......bbbbb.....',
    '......bb.bb.....',
    '.....bb...bb....',
    '.....bb...bb....',
    '....RRWR..RRWR..',
    '...RRRRR..RRRRR.',
    '................',
    '................',
    '................',
]
SONIC_RUN1 = patch(SONIC, 16, [
    '......bbbb......',
    '....bb....bb....',
    '...bb......bb...',
    '..RRWR....RRWR..',
    '.RRRRR....RRRRR.',
])
SONIC_RUN2 = patch(SONIC, 16, [
    '......bbbb......',
    '......bbb.......',
    '.....bb.bb......',
    '....RRWRRRWR....',
    '...RRRRRRRRR....',
])
BALL = [
    '.....dbbbbd.....',
    '...dbbbbbbbbd...',
    '..dbbbbbbbbbbd..',
    '.dbbbdbbbbdbbbd.',
    '.bbbbbbbbbbbbbb.',
    'dbbbbbbPPbbbbbbd',
    'bbbbbbPPPPbbbbbb',
    'bbbdbbPPPPbbdbbb',
    'bbbbbbPPPPbbbbbb',
    'dbbbbbbPPbbbbbbd',
    '.bbbbbbbbbbbbbb.',
    '.dbbbdbbbbdbbbd.',
    '..dbbbbbbbbbbd..',
    '...dbbbbbbbbd...',
    '.....dbbbbd.....',
    '................',
]
BALL2 = [r.replace('d', 'x').replace('b', 'd').replace('x', 'b') if i % 2 else r for i, r in enumerate(BALL)]
SONIC_BALL1 = ['................'] * 8 + BALL
SONIC_BALL2 = ['................'] * 8 + BALL2

# ── Naruto ──────────────────────────────────────────────────────────────────────
NARUTO = [
    '.....Y..Y.Y.....',
    '...Y.YYYYYYY....',
    '..YYYYYYYYYYY...',
    '.YYYYYYYYYYYY...',
    '..YYYYYYYYYYYY..',
    '...NNNGGGGNNN...',
    '...YSSSSSSSSY...',
    '....SSBSSSBS....',
    '....SKSSSSSKS...',
    '.....SSSSSSS....',
    '......SSSSS.....',
    '....KKOOOOKK....',
    '...OOOOOWOOOO...',
    '..SOOOOOOOOOOS..',
    '..S.OOOOOOOO.S..',
    '....OOOOOOOO....',
    '....KKKKKKKK....',
    '....OOOO.OOOO...',
    '....OOO...OOO...',
    '....OOO...OOO...',
    '....NN.....NN...',
    '...NNN.....NNN..',
    '................',
    '................',
]
NARUTO_HOP = patch(NARUTO, 16, [
    '....KKKKKKKK....',
    '...OOOOOOOOOO...',
    '..OOO......OOO..',
    '..NNN......NNN..',
    '................',
    '................',
])
NARUTO_THROW = patch(NARUTO, 12, [
    '...OOOOOWOOOOSSG',
    '..SOOOOOOOOOO...',
    '..S.OOOOOOOO....',
])

# ── Ataques ─────────────────────────────────────────────────────────────────────
KI1 = [
    '................',
    '......CCCC......',
    '....CCWWWWCC....',
    '...CWWYYYYWWC...',
    '..CWYYYYYYYYWC..',
    '..CWYYWWWWYYWC..',
    '.CWYYWWWWWWYYWC.',
    '.CWYYWWWWWWYYWC.',
    '.CWYYWWWWWWYYWC.',
    '.CWYYWWWWWWYYWC.',
    '..CWYYWWWWYYWC..',
    '..CWYYYYYYYYWC..',
    '...CWWYYYYWWC...',
    '....CCWWWWCC....',
    '......CCCC......',
    '................',
]
KI2 = [r.replace('Y', 'x').replace('W', 'Y').replace('x', 'W') for r in KI1]
BEAM1 = ['................', '................'] + ['DDDDDDDDDDDDDDDD', 'CCCCCCCCCCCCCCCC', 'CCCCCCCCCCCCCCCC'] + \
        ['WWWWWWWWWWWWWWWW'] * 6 + ['CCCCCCCCCCCCCCCC', 'CCCCCCCCCCCCCCCC', 'DDDDDDDDDDDDDDDD', '................',
                                     '................']
BEAM2 = ['................', '................', '................'] + ['CCCCCCCCCCCCCCCC', 'CWCCWCCWCCWCCWCC'] + \
        ['WWWWWWWWWWWWWWWW'] * 6 + ['CCWCCWCCWCCWCCWC', 'CCCCCCCCCCCCCCCC', '................', '................',
                                     '................']
KUNAI = [
    '................',
    '.K..............',
    'KOK.....GGGG....',
    '.KKKKKGGGGGGGGW.',
    'KOK.....GGGG....',
    '.K..............',
    '................',
    '................',
]


def main(out):
    os.makedirs(out, exist_ok=True)
    sheet([GOKU, GOKU_WALK, GOKU_CHARGE, GOKU_FIRE]).save(os.path.join(out, 'npc-293.png'))
    strip([KI1, KI2], (16, 16)).save(os.path.join(out, 'npc-294.png'))
    strip([BEAM1, BEAM2], (16, 16)).save(os.path.join(out, 'npc-295.png'))
    sheet([SONIC, SONIC_RUN1, SONIC_RUN2, SONIC_BALL1, SONIC_BALL2]).save(os.path.join(out, 'npc-296.png'))
    sheet([NARUTO, NARUTO_HOP, NARUTO_THROW]).save(os.path.join(out, 'npc-297.png'))
    sheet([KUNAI], cell=(16, 8)).save(os.path.join(out, 'npc-298.png'))
    print('sprites en', out)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'out')
