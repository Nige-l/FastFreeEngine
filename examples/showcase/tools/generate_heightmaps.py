#!/usr/bin/env python3
"""Generate heightmap PNGs for the Echoes of the Ancients showcase."""
from PIL import Image
import math
import os

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), '..', 'assets', 'terrain')

def generate_courtyard(size=256):
    """Level 1 - The Courtyard: gently rolling terrain with a flat center."""
    img = Image.new('L', (size, size))
    pixels = img.load()
    cx, cz = size // 2, size // 2
    for z in range(size):
        for x in range(size):
            # Base: gentle rolling hills
            h = 128  # middle grey = base height
            # Add gentle sine hills
            h += 15 * math.sin(x * 0.03) * math.cos(z * 0.04)
            h += 10 * math.sin(x * 0.07 + 1.5) * math.sin(z * 0.05 + 0.8)
            # Flatten the center (gameplay area)
            dx = (x - cx) / (size * 0.3)
            dz = (z - cz) / (size * 0.3)
            dist = math.sqrt(dx*dx + dz*dz)
            if dist < 1.0:
                # Blend toward flat center
                flatness = max(0, 1.0 - dist)
                h = h * (1 - flatness) + 128 * flatness
            # Raise edges for arena feel
            edge_dist = max(abs(x - cx), abs(z - cz)) / (size * 0.5)
            if edge_dist > 0.7:
                h += 30 * (edge_dist - 0.7) / 0.3
            pixels[x, z] = max(0, min(255, int(h)))
    img.save(os.path.join(OUTPUT_DIR, 'courtyard_height.png'))
    print("Generated courtyard_height.png")

def generate_summit(size=256):
    """Level 3 - The Summit: dramatic peaks around a flat central arena."""
    img = Image.new('L', (size, size))
    pixels = img.load()
    cx, cz = size // 2, size // 2
    for z in range(size):
        for x in range(size):
            # Base height
            h = 100
            # Dramatic peaks around edges
            dx = (x - cx) / (size * 0.35)
            dz = (z - cz) / (size * 0.35)
            dist = math.sqrt(dx*dx + dz*dz)
            # Mountain ring
            if dist > 0.6:
                ring_factor = (dist - 0.6) / 0.4
                h += 80 * ring_factor
                h += 20 * math.sin(math.atan2(dz, dx) * 6) * ring_factor
            # Flat arena center
            if dist < 0.4:
                flatness = max(0, 1.0 - dist / 0.4)
                h = h * (1 - flatness) + 100 * flatness
            # Small bumps for texture
            h += 5 * math.sin(x * 0.1) * math.cos(z * 0.12)
            pixels[x, z] = max(0, min(255, int(h)))
    img.save(os.path.join(OUTPUT_DIR, 'summit_height.png'))
    print("Generated summit_height.png")

if __name__ == '__main__':
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    generate_courtyard()
    generate_summit()
    print("Done!")
