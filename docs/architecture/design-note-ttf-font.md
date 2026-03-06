# Design Note: TTF Font Rendering

**Status:** Proposed
**Author:** architect
**Tier:** LEGACY (OpenGL 3.3)

---

## Summary

Add TrueType font loading and rendering alongside the existing 8x8 bitmap font. TTF fonts are rasterized to a glyph atlas at load time using `stb_truetype.h`. The existing `GlyphQuad` buffer and `flushText` rendering path are reused with no shader changes.

---

## 1. Library

**stb_truetype.h** — header-only, already available via the `stb` vcpkg package (same package that provides `stb_image.h`). No new dependency required.

Use `stb_rect_pack.h` (also in the `stb` package) for atlas packing. It produces tighter layouts than a simple grid, which matters when supporting multiple font sizes.

---

## 2. Font Atlas

- Rasterize ASCII 32-127 (96 glyphs) at load time into a grayscale bitmap.
- Upload as an OpenGL texture with `GL_RED` internal format (`TextureFormat::R8`).
- Initial atlas size: **512x512**. If glyphs do not fit (large font sizes), double to 1024x1024, then to a hard maximum of **2048x2048**. If glyphs still do not fit at 2048x2048, the load fails.
- One texture per font+size combination. Loading the same TTF at two sizes produces two separate atlases.
- Use `stbtt_PackBegin` / `stbtt_PackSetOversampling` / `stbtt_PackFontRange` / `stbtt_PackEnd` for rasterization and packing. Oversampling 2x horizontal improves quality at small sizes with negligible atlas cost.

---

## 3. Glyph Metrics

Per-glyph, store:

```cpp
struct TtfGlyph {
    f32 advanceX;    // Horizontal advance to next glyph (pixels)
    f32 offsetX;     // Bearing X — left edge offset from cursor
    f32 offsetY;     // Bearing Y — top edge offset from baseline
    f32 width;       // Glyph quad width in pixels
    f32 height;      // Glyph quad height in pixels
    f32 uvMinX, uvMinY;
    f32 uvMaxX, uvMaxY;
};
```

Metrics come from `stbtt_GetPackedQuad`, which returns positioned quads with correct bearing offsets.

Font-level metrics (from `stbtt_GetFontVMetrics`, scaled by `stbtt_ScaleForPixelHeight`):

- **ascent** — distance from baseline to top of tallest glyph
- **descent** — distance from baseline to bottom of lowest glyph (negative)
- **lineGap** — extra spacing between lines
- **lineHeight** = ascent - descent + lineGap

These are stored on the `FontHandle` and used for baseline alignment and multi-line text layout.

---

## 4. Rendering Path

### Texture format and the shader problem

The existing sprite shader samples RGBA textures. A TTF atlas is single-channel (`GL_RED`). Two options:

| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| (a) Separate text shader | Fragment shader reads `.r` as alpha | Explicit | Extra shader, extra draw state change |
| (b) GL texture swizzle | Set `GL_TEXTURE_SWIZZLE_RGBA` to map R channel to alpha, fill RGB with 1.0 | No shader change, one-time setup per texture | Requires OpenGL 3.3 (we have it) |

**Decision: Option (b) — swizzle mask.**

At atlas texture creation, call:

```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
```

The sprite shader sees RGBA = (1, 1, 1, coverage) and tint color multiplication works as expected. No shader changes. The swizzle is set once at texture creation time and persists on the texture object. `GL_TEXTURE_SWIZZLE` is core in OpenGL 3.3.

**Implementation note:** The RHI `createTexture` path currently does not set swizzle parameters. Add an optional `swizzleMask` field to `TextureDesc` (default: identity / no swizzle) so this is handled inside the RHI, not by the caller.

### Buffer and draw path

- TTF glyph quads go into the **same `GlyphQuad` buffer** as bitmap glyphs. The `drawFontText` function writes `GlyphQuad` entries exactly like `drawText` does.
- `flushText` already iterates all queued `GlyphQuad` entries and submits them as sprites via `addSprite`. Because TTF glyphs reference a different texture handle than the bitmap atlas, the sprite batcher will break the batch at the texture change. This is acceptable — text is not the bottleneck and the batch break happens at most once per font switch.
- No changes to `flushText`, `SpriteBatch`, or the sprite shader.

### Filter mode

TTF atlas textures use `TextureFilter::LINEAR` (not `NEAREST`). The oversampled anti-aliased glyphs look correct with bilinear filtering. The bitmap font continues to use `NEAREST` for crisp pixel rendering.

---

## 5. Lua API

### Loading and unloading

```lua
local fontId = ffe.loadFont("fonts/opensans.ttf", 24)  -- returns integer > 0, or nil + error
ffe.unloadFont(fontId)
```

- `ffe.loadFont(path, size)` -> fontId (integer 1-8), or `nil` + error string.
- `ffe.unloadFont(fontId)` -> nil. Destroys atlas texture, frees slot.
- Font file path is validated against the asset root using the existing path traversal check (same as `loadTexture` / `loadSound`).

### Drawing

```lua
ffe.drawFontText(fontId, "Score: 42", 10, 10)
ffe.drawFontText(fontId, "Game Over", 200, 300, 2.0, 1.0, 0.0, 0.0, 1.0)
```

Signature: `ffe.drawFontText(fontId, text, x, y [, scale, r, g, b, a])`

**Why a separate function instead of extending `ffe.drawText`?**

`ffe.drawText` currently takes `(text, x, y [, scale, r, g, b, a])`. Adding an optional `fontId` at the end creates ambiguity — is the 5th argument `scale` or `fontId`? A separate `drawFontText` function with `fontId` as the first argument is unambiguous and does not break backward compatibility of `drawText`. The bitmap font remains the default for quick debug/HUD text with zero setup.

### Query

```lua
local w, h = ffe.measureText(fontId, "Hello", scale)
```

`ffe.measureText(fontId, text [, scale])` -> width, height in pixels. Needed for centering text, fitting text in boxes, etc. Uses glyph advance widths and font line height. No rendering, no GPU work.

---

## 6. Resource Limits

- **Max 8 loaded fonts.** Fixed array of `FontHandle` slots in `TextRenderer`. No dynamic allocation.
- **Max atlas size: 2048x2048.** Single `GL_RED` byte per pixel = 4 MB worst case per atlas. 8 atlases = 32 MB maximum VRAM for fonts. Well within the 1 GB LEGACY budget.
- **Path traversal prevention** on font file paths — reuse existing `isAudioPathSafe` / asset root validation pattern.
- **Font files must be under the asset root** (`assets/` directory).
- **Font file data** is loaded into a temporary buffer, passed to stb_truetype for rasterization, then freed. The raw TTF data is not kept in memory after atlas creation.

---

## 7. Font Storage

```cpp
struct FontHandle {
    rhi::TextureHandle atlas;       // GPU texture (GL_RED + swizzle)
    TtfGlyph glyphs[96];           // ASCII 32-127, indexed by (char - 32)
    f32 fontSize;                   // Requested pixel height
    f32 ascent;                     // Baseline to top
    f32 descent;                    // Baseline to bottom (negative)
    f32 lineHeight;                 // ascent - descent + lineGap
    bool occupied;                  // Slot in use?
};
```

Storage options:

| Option | Description |
|--------|-------------|
| (a) Add to TextRenderer | `FontHandle m_fonts[8]` alongside existing bitmap font state |
| (b) Separate FontManager | New struct, separate lifecycle |

**Decision: Option (a) — add to TextRenderer.** The TTF fonts share the same glyph buffer and flush path. A separate manager would just add indirection. The `TextRenderer` struct gains a `FontHandle fonts[8]` array and `loadFont` / `unloadFont` functions.

---

## 8. Tier Support

**LEGACY tier (OpenGL 3.3).** All features used are core in OpenGL 3.3:

- `GL_RED` texture format (core since 3.0)
- `GL_TEXTURE_SWIZZLE_*` (core since 3.3)
- Everything else is existing sprite batch rendering

No tier-specific behavior. No fallback needed.

---

## 9. Bundled Font

Ship one permissively-licensed font in `assets/fonts/` for demos and testing.

Candidates (all SIL Open Font License or equivalent):

| Font | Style | Size | License |
|------|-------|------|---------|
| **Inter** | Clean sans-serif | ~300 KB | SIL OFL |
| **Press Start 2P** | Pixel art style | ~30 KB | SIL OFL |
| **JetBrains Mono** | Monospace, good for debug | ~100 KB | SIL OFL |

Recommendation: ship **Inter** (regular weight only) as the default TTF. It is highly legible at small sizes, widely used, and the SIL OFL license is compatible with MIT. Include the license file as `assets/fonts/Inter-LICENSE.txt`.

Optionally also ship **Press Start 2P** — it is tiny and suits the retro/indie aesthetic of many FFE games.

---

## 10. Implementation Sequence

1. Add `swizzleMask` field to `rhi::TextureDesc`. Update `rhi::createTexture` to apply swizzle parameters when set.
2. Add `FontHandle` struct and `FontHandle fonts[8]` array to `TextRenderer`.
3. Implement `loadFont` / `unloadFont` in `text_renderer.cpp` using stb_truetype + stb_rect_pack.
4. Implement `drawFontText` — writes `GlyphQuad` entries using TTF glyph metrics and the TTF atlas handle.
5. Implement `measureText`.
6. Add Lua bindings: `ffe.loadFont`, `ffe.unloadFont`, `ffe.drawFontText`, `ffe.measureText`.
7. Add bundled font to `assets/fonts/`.
8. Update `.context.md` for the renderer.
9. Write tests.

---

## 11. What This Does NOT Cover

- **Unicode / UTF-8 beyond ASCII.** Future work. The atlas and glyph table would need to grow, and we would want a glyph cache instead of pre-baking everything. Not needed for the initial implementation.
- **Kerning.** stb_truetype supports it (`stbtt_GetGlyphKernAdvance`), but it is not included in the first pass. Can be added later by adjusting advance width between glyph pairs.
- **Text wrapping / layout engine.** `drawFontText` draws a single line (or respects `\n`). Higher-level layout (word wrap, alignment, rich text) is game-side or a future engine feature.
- **Signed Distance Field (SDF) fonts.** SDF rendering enables resolution-independent scaling and outlines/shadows in the shader. It is a natural upgrade path but requires a dedicated SDF shader. Not needed at LEGACY tier where font sizes are known at load time.
