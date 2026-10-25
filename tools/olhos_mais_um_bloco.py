"""Estende para baixo, em um bloco, cada um dos dois quadrados verdes (olhos)
do mascote do Delta Agent, a partir do estado atual (screenshot).

Uso: python tools/olhos_mais_um_bloco.py
"""
from PIL import Image
import os
from collections import deque, Counter

TEMP = os.environ["TEMP"]
SHOT = os.path.join(TEMP, "pi-clipboard-1988bc49-f89c-4d0c-b5fa-c0d9fa1c1fa8.png")
OUT = os.path.join(TEMP, "delta-agent-olhos-mais-um-bloco.png")

# regiao do mascote dentro do screenshot
OX, OY, W, H = 6, 169, 72, 57
BLOCO = 9  # altura de um bloco (celula) da grade da arte


def is_green(c):
    r, g, b = c
    return g > 150 and r < 130 and b < 90


im = Image.open(SHOT).convert("RGB")
px = im.load()
art = [[px[OX + x, OY + y] for x in range(W)] for y in range(H)]

mask = [[is_green(art[y][x]) for x in range(W)] for y in range(H)]

# componentes conectados dos pixels verdes -> os dois olhos
vistos = [[False] * W for _ in range(H)]
comps = []
for y in range(H):
    for x in range(W):
        if not mask[y][x] or vistos[y][x]:
            continue
        q = deque([(x, y)])
        vistos[y][x] = True
        cel = []
        while q:
            cx, cy = q.popleft()
            cel.append((cx, cy))
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = cx + dx, cy + dy
                if 0 <= nx < W and 0 <= ny < H and mask[ny][nx] and not vistos[ny][nx]:
                    vistos[ny][nx] = True
                    q.append((nx, ny))
        comps.append(cel)

print("componentes verdes:", [len(c) for c in comps])

for cel in comps:
    xs = [x for x, _ in cel]
    ys = [y for _, y in cel]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    largura = x1 - x0 + 1

    # linhas cheias (barra horizontal) do componente
    cheias = []
    for y in range(y0, y1 + 1):
        n = sum(1 for x in range(x0, x1 + 1) if mask[y][x])
        if n >= 0.8 * largura:
            cheias.append(y)

    # barra de baixo: bloco de linhas cheias encostado na base
    base = []
    y = y1
    while y >= y0 and y in cheias:
        base.append(y)
        y -= 1
    base.sort()
    topo_base = base[0]

    # padrao dos lados: ultima linha que nao e barra cheia
    padrao_lados = [mask[topo_base - 1][x] for x in range(x0, x1 + 1)]

    # apaga a barra de baixo e estica os lados BLOCO pixels para baixo
    for yy in base:
        for x in range(x0, x1 + 1):
            mask[yy][x] = False
    for yy in range(topo_base, topo_base + BLOCO):
        for i, v in enumerate(padrao_lados):
            mask[yy][x0 + i] = v
    # redesenha a barra de baixo deslocada
    for k, yy in enumerate(base):
        for x in range(x0, x1 + 1):
            mask[yy + BLOCO][x] = True

    print(f"olho x{x0}-{x1} y{y0}-{y1}: barra {base[0]}-{base[-1]} -> {base[0]+BLOCO}-{base[-1]+BLOCO}")

# pinta de volta na imagem
for y in range(H):
    for x in range(W):
        if mask[y][x] and not is_green(art[y][x]):
            px[OX + x, OY + y] = (38, 230, 0)

im.save(OUT)
print("salvo:", OUT, im.size)
