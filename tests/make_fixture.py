"""Emit a synthetic periodic 3-band FRMSF fixture to stdout (not research data)."""
import math

n, bands = 12, 3
print(n, n, n)
print(1)
print(bands)
print("1 0 0\n0.2 1 0\n0.1 0.15 1.2")
for band in range(bands):
    for i in range(n):
        for j in range(n):
            for k in range(n):
                x, y, z = (2 * math.pi * (q / n - 0.32) for q in (i, j, k))
                energy = math.cos(x) + 0.8 * math.cos(y) + 0.6 * math.cos(z) - (1.1 + 0.3 * band)
                print(f"{energy:.9g}")
for band in range(bands):
    for i in range(n):
        for j in range(n):
            for k in range(n):
                print(f"{1 + 0.25 * math.sin(2 * math.pi * i / n) + 0.1 * band:.9g}")
