# ADR: Documentation and Training Website

**Status:** PROPOSED
**Author:** architect
**Date:** 2026-03-07
**Tiers:** N/A (website, not engine runtime)
**Security Review Required:** NO — the website is a static site with no user input processing, no server-side code, and no authentication. If interactive features (embedded code editors, user accounts, forum integration) are added in future milestones, a separate security review will be required at that time.

---

## 1. Problem Statement

FFE has a complete engine runtime (2D, 3D, networking, editor, scripting, physics, audio) with AI-native `.context.md` files in every subsystem directory and architectural decision records in `docs/architecture/`. But none of this documentation is accessible to someone discovering FFE for the first time. There is no website, no getting-started guide on the web, no searchable API reference, and no tutorial series that takes a student from zero to building a game.

The website is not a nice-to-have. It is a core part of the FFE platform (CLAUDE.md Section 1: "The Website — documentation, tutorials, and training content aimed at students and young developers"). The mission statement (Section 10) says FFE exists to "unlock creativity and get young people into engineering." A student with a ten-year-old laptop needs a website that loads fast, is readable, and teaches them to build games — not just an engine binary and a GitHub repo.

This ADR defines the technology stack, site structure, content pipeline, design principles, hosting strategy, and implementation plan for the FFE documentation and training website.

---

## 2. Scope

**In scope:**
- Static site generator selection and justification
- Site structure (pages, sections, navigation)
- Content pipeline (how `.context.md` and ADR files become web pages)
- CI integration (GitHub Actions builds the site on push to main)
- Design principles (accessibility, performance, readability)
- Hosting on GitHub Pages
- Dark/light theme toggle
- Built-in search
- Mobile-friendly responsive layout

**Out of scope (future milestones — architecture must not block these):**
- Embedded interactive code editors (e.g., Lua playground in the browser)
- Video content hosting
- Community forum or Discord bot integration
- Asset library / asset store
- User accounts or authentication
- Analytics beyond privacy-respecting page view counts
- "Build Your Own Engine" learning track (content, not infrastructure)
- Community showcase (games built with FFE)

---

## 3. Static Site Generator Selection

### 3.1 Decision: MkDocs with Material for MkDocs

**MkDocs Material** is the static site generator for the FFE website.

### 3.2 Evaluation Criteria

The website must:
1. Work on old browsers and slow connections (no heavy JS frameworks)
2. Build fast in CI (rebuild on every push to main)
3. Use Markdown for all content authoring (the engine team writes Markdown, not HTML)
4. Support syntax highlighting for Lua, C++, and GLSL
5. Include built-in full-text search (no external service)
6. Be mobile-friendly and responsive out of the box
7. Deploy to GitHub Pages for free

### 3.3 Candidates Evaluated

| Criterion | MkDocs Material | Hugo | Docusaurus | Astro | Jekyll |
|-----------|----------------|------|------------|-------|--------|
| **JS shipped to client** | Minimal (~50 KB gzipped) | Zero (pure HTML) | Heavy (~300 KB+, React runtime) | Configurable (zero to heavy) | Zero (pure HTML) |
| **Build speed (500 pages)** | ~10s | ~2s | ~30s+ | ~15s | ~60s+ |
| **Markdown-native** | YES — pure Markdown, no JSX | YES | Markdown + MDX (JSX in Markdown) | Markdown + components | YES |
| **Syntax highlighting** | Pygments (300+ languages including Lua, C++, GLSL) | Chroma (good coverage) | Prism (good coverage) | Shiki or Prism | Rouge (good coverage) |
| **Built-in search** | YES — lunr.js client-side, zero external deps | No built-in (needs plugin or Algolia) | YES — Algolia or local | Plugin-dependent | No built-in |
| **Mobile-responsive** | YES — Material Design responsive grid | Theme-dependent | YES | Theme-dependent | Theme-dependent |
| **GitHub Pages deploy** | YES — `mkdocs gh-deploy` or Actions | YES | YES | YES | YES (native) |
| **Dark/light toggle** | YES — built-in, one config line | Theme-dependent | YES | Theme-dependent | Requires custom work |
| **Admonitions (callouts)** | YES — note, warning, tip, danger, etc. | Shortcodes (different syntax) | MDX components | Components | Requires plugin |
| **Tabs in code blocks** | YES — content tabs (show Lua/C++ side by side) | Shortcodes | MDX | Components | No |
| **Versioned docs** | YES — mike plugin | No built-in | YES | No built-in | No |
| **Learning curve** | Low — YAML config + Markdown | Low — TOML/YAML + Markdown | Medium — React ecosystem knowledge helps | Medium — component model | Low — Markdown + Liquid |
| **Python dependency** | YES (pip install) | No (single Go binary) | Node.js | Node.js | Ruby |

### 3.4 Justification

**MkDocs Material wins because it is purpose-built for technical documentation with zero compromise on the constraints that matter to FFE.**

1. **Minimal client-side JS.** The Material theme ships ~50 KB gzipped of JS for search, navigation, and theme toggle. No React runtime, no hydration, no SPA router. Pages are server-rendered HTML with progressive enhancement. This loads fast on old hardware and slow connections. Hugo ships less JS (zero), but lacks built-in search and requires significant theme work to match Material's feature set.

2. **Built-in search with no external service.** Material's search uses lunr.js to build a client-side search index at build time. No Algolia account, no API keys, no external requests. The search index is a static JSON file served alongside the site. This matters for privacy (no tracking) and reliability (no third-party dependency).

3. **Markdown-native with powerful extensions.** Content is pure Markdown — no JSX, no shortcodes, no template syntax. The team already writes Markdown (`.context.md` files, ADRs, devlog). Material adds admonitions, content tabs, code annotations, and mermaid diagrams via standard Markdown extensions. This means `.context.md` files can be included in the site with minimal transformation.

4. **Dark/light theme toggle is one config line.** `palette.toggle` in `mkdocs.yml`. No custom CSS, no JavaScript to write. This is important for accessibility and student preference.

5. **Syntax highlighting covers all FFE languages.** Pygments supports Lua, C++, GLSL, CMake, JSON, YAML, Bash, and 300+ other languages. Code blocks are highlighted at build time (no client-side JS for highlighting).

6. **Build speed is excellent for CI.** MkDocs builds ~500 pages in ~10 seconds. Hugo is faster (~2s), but 10 seconds is well within CI budget and the feature gap (search, dark mode, admonitions, tabs) makes MkDocs Material the better trade-off.

7. **GitHub Pages deployment is a single command.** `mkdocs gh-deploy` or a 10-line GitHub Actions workflow. No build artifact management, no custom deploy scripts.

**Why not Hugo?** Hugo is faster and ships zero JS, but it lacks built-in search, built-in dark/light toggle, and the rich admonition/tab/annotation system that Material provides. For a documentation site aimed at students, these features matter more than the ~8 second build time difference. Hugo would require significant theme development to match Material's documentation UX.

**Why not Docusaurus?** Docusaurus ships a React runtime (~300 KB+) to every visitor. This violates the constraint that the site must work well on old browsers and slow connections. A student on a ten-year-old laptop with a 3G connection should not download a JavaScript framework to read documentation.

**Why not Astro?** Astro can ship zero JS ("islands architecture"), but its Markdown handling requires component syntax for advanced features. The team writes pure Markdown. Astro is a better fit for marketing sites than documentation sites.

**Why not Jekyll?** Jekyll is slow to build (Ruby), has no built-in search, and requires plugins for features that MkDocs Material provides out of the box. Its main advantage (native GitHub Pages support) is nullified by the fact that GitHub Actions can deploy any static site to Pages.

### 3.5 Python Dependency

MkDocs requires Python (3.8+). This is the only new toolchain dependency the website introduces. Installation:

```bash
pip install mkdocs-material
```

This pulls in MkDocs, the Material theme, Pygments, and all Markdown extensions. The CI workflow installs this in a virtual environment. Engine developers who only work on C++ code never need to install Python — the site builds in CI.

---

## 4. Site Structure

### 4.1 Navigation Map

```
Home                          Landing page: what FFE is, why it exists, one-click links to Getting Started
|
+-- Getting Started
|   +-- Building from Source  Linux, macOS, Windows build instructions
|   +-- Your First Game       "Hello World" game in 15 minutes (Lua)
|   +-- Project Structure     What the directories mean, where to put your code
|
+-- Tutorials
|   +-- 2D
|   |   +-- Pong              Classic Pong, teaches input + collision + scoring
|   |   +-- Platformer        Side-scroller, teaches tilemap + animation + camera
|   |   +-- Breakout          Teaches particles + sound + scene management
|   |   +-- (more added over time)
|   |
|   +-- 3D
|   |   +-- First 3D Scene    Load a mesh, set up camera and lighting
|   |   +-- Materials          Textures, specular, normal maps
|   |   +-- Skeletal Animation Animated characters
|   |   +-- (more added over time)
|   |
|   +-- Multiplayer
|       +-- Local Multiplayer  Two players, one server, same machine
|       +-- Online Arena       Client-server over the network
|       +-- (more added over time)
|
+-- API Reference
|   +-- Core                   ECS, Application, Input, Timers
|   +-- Renderer               Sprites, Text, Particles, Tilemap, 3D Mesh, Materials, Camera
|   +-- Audio                  Sound effects, music, 3D audio
|   +-- Physics                Collision, rigid bodies
|   +-- Scripting              Lua API (all ffe.* bindings)
|   +-- Networking             Client, server, replication, messages
|
+-- How It Works
|   +-- ECS Architecture       How the entity-component-system works internally
|   +-- Renderer Pipeline      How frames get drawn (batching, render queue, OpenGL)
|   +-- Networking Internals   Packets, replication, interpolation
|   +-- Editor Architecture    How the standalone editor is built
|   +-- (one page per ADR, adapted for a general audience)
|
+-- Community
    +-- Contributing           How to contribute (links to CONTRIBUTING.md)
    +-- GitHub                 Link to the repository
    +-- Discord                Link (when available)
    +-- Code of Conduct        Link
```

### 4.2 Home Page

The home page is not a wall of text. It contains:

1. **One sentence:** "FastFreeEngine is a free, open-source game engine that runs on old hardware."
2. **Three call-to-action cards:** "Build from Source" / "Make Your First Game" / "Read the API"
3. **Feature grid:** 6 tiles with icons — 2D, 3D, Multiplayer, Lua Scripting, Editor, AI-Native Docs
4. **Mission statement:** One paragraph from CLAUDE.md Section 10 (the version aimed at students, not the internal engineering version).
5. **Footer:** MIT license, GitHub link, version number.

No hero images, no animations, no carousel. Clean, fast, informative.

### 4.3 Page Conventions

Every page follows a consistent structure:

1. **Title** — what this page covers
2. **Prerequisites** — what the reader should know or have done first (with links)
3. **Content** — the actual material, with code examples
4. **Next Steps** — links to the logical next page(s)

Tutorials additionally include:

5. **What You Will Build** — screenshot or description of the end result
6. **Complete Code** — full Lua listing at the bottom (so readers can copy-paste and run)

---

## 5. Content Pipeline

### 5.1 Source of Truth

| Content type | Source | Transformation |
|---|---|---|
| Getting Started guides | `website/docs/getting-started/*.md` | None — hand-written Markdown, authored directly for the website |
| Tutorials | `website/docs/tutorials/**/*.md` | None — hand-written Markdown |
| API Reference | `engine/**/.context.md` | Script extracts and reformats (see 5.2) |
| How It Works | `docs/architecture/adr-*.md` | Script adapts for general audience (see 5.3) |
| Home page | `website/docs/index.md` | None — hand-written Markdown with Material extensions |
| Community | `website/docs/community/*.md` | None — hand-written, links to external resources |

### 5.2 .context.md to API Reference Pipeline

Each engine subsystem has a `.context.md` file written for LLMs (CLAUDE.md Section 9). These files contain the public API, usage patterns, anti-patterns, tier support, and dependencies — exactly what an API reference page needs.

A build-time script (`website/scripts/generate_api_docs.py`) transforms `.context.md` files into API reference pages:

```
Input:  engine/renderer/.context.md
Output: website/docs/api/renderer.md
```

**Transformation steps:**

1. **Copy the file** to `website/docs/api/` with a new filename derived from the directory name.
2. **Add front matter** — title, nav position, breadcrumb path.
3. **Wrap code blocks** — ensure all code examples have language tags for syntax highlighting (most already do).
4. **Add navigation links** — "Back to API Reference index" header and "Related subsystems" footer.
5. **Add a banner** — "This page is auto-generated from the engine source. To suggest changes, edit `engine/renderer/.context.md`."

The script is intentionally simple. `.context.md` files are already well-structured Markdown (they are written to a strict format per CLAUDE.md Section 9). The transformation is mostly copying with light decoration, not parsing and restructuring.

**Why not use mkdocs-include or symlinks?** Because `.context.md` files are written for LLMs (terse, structured, no narrative flow) and API reference pages are written for humans (need navigation, breadcrumbs, edit links). The transformation script bridges this gap while keeping `.context.md` files optimised for their primary audience (AI assistants).

### 5.3 ADR to "How It Works" Pipeline

ADRs in `docs/architecture/` are internal engineering documents with implementation details, security threat models, and agent dispatch plans. The "How It Works" pages are for a general audience — students and developers who want to understand how a game engine works, not how the agent team built it.

A build-time script (`website/scripts/generate_internals_docs.py`) adapts ADRs:

1. **Extract relevant sections** — Problem Statement, architecture decisions, diagrams, data structures. Skip agent-specific sections (test plans, file ownership, session numbers).
2. **Simplify language** — the script adds a front-matter flag; the actual simplification is done by a human author who reviews and edits the generated draft. The script provides the starting point, not the final output.
3. **Add educational framing** — "Why does a game engine need this?" context before diving into the how.

Unlike the API pipeline, the "How It Works" pipeline produces **drafts that require human editing**. ADRs are too detailed and internal-facing to be published directly. The script saves time by extracting the relevant content; a human author shapes it into a teaching document.

### 5.4 Build Pipeline

```
website/
    mkdocs.yml                 MkDocs configuration (theme, nav, plugins, extensions)
    docs/                      All Markdown content (hand-written + generated)
        index.md               Home page
        getting-started/       Build, first game, project structure
        tutorials/             2D, 3D, multiplayer tutorials
        api/                   Auto-generated from .context.md
        internals/             Drafts generated from ADRs, then human-edited
        community/             Contributing, links
    scripts/
        generate_api_docs.py   .context.md -> API reference pages
        generate_internals_docs.py  ADR -> "How It Works" drafts
    overrides/                 Custom theme overrides (logo, favicon, CSS)
```

The build sequence:

```bash
# 1. Generate API docs from .context.md files
python website/scripts/generate_api_docs.py

# 2. Build the static site
cd website && mkdocs build

# Output: website/site/ (static HTML, ready to deploy)
```

### 5.5 CI Integration

A GitHub Actions workflow builds and deploys the site on every push to `main`:

```yaml
# .github/workflows/website.yml
name: Deploy Website
on:
  push:
    branches: [main]

permissions:
  pages: write
  id-token: write

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'

      - name: Install MkDocs Material
        run: pip install mkdocs-material

      - name: Generate API docs
        run: python website/scripts/generate_api_docs.py

      - name: Build site
        run: cd website && mkdocs build --strict
        # --strict fails on warnings (broken links, missing pages)

      - name: Deploy to GitHub Pages
        uses: actions/deploy-pages@v4
        with:
          path: website/site
```

**Build time in CI:** Under 30 seconds (Python install cached, pip install cached, MkDocs build ~10s).

The `--strict` flag causes the build to fail on any warning — broken internal links, missing referenced pages, or invalid Markdown. This catches documentation rot in CI, the same way `-Wall -Wextra` catches code rot.

---

## 6. Design Principles

### 6.1 Accessible on Old Hardware and Slow Connections

- **Total page weight target: under 200 KB per page** (HTML + CSS + JS + fonts, gzipped). Material theme meets this by default.
- **No web fonts by default.** Use the system font stack (`-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif`). This eliminates font download latency entirely. A web font can be optionally loaded for branding if the connection is fast (via `font-display: optional`), but the site must be fully readable without it.
- **No images in navigation or layout.** Images appear only in tutorial content (screenshots of games being built). All images use `loading="lazy"` and have explicit `width`/`height` attributes to prevent layout shift.
- **No JavaScript required for core reading.** The site is readable with JavaScript disabled. Search and theme toggle are progressive enhancements — nice to have, not required to read documentation.
- **Serve pre-compressed assets.** GitHub Pages serves gzip-compressed responses automatically. The MkDocs build can also pre-compress with Brotli for CDN deployments in the future.

### 6.2 Clean, Readable, No Visual Clutter

- **Maximum content width: 800px.** Long lines are hard to read. Technical documentation benefits from a narrow column.
- **Generous line height: 1.6.** Dense text is hard to scan. Code blocks use 1.4.
- **Minimal colour palette.** Two accent colours (one for links, one for code/highlights), plus the standard admonition colours (blue for note, yellow for warning, red for danger, green for tip). No gradients, no decorative borders, no background patterns.
- **Navigation is a sidebar, not a hamburger menu.** On desktop, the left sidebar is always visible. On mobile, it collapses to a hamburger. Material theme handles this by default.
- **No pop-ups, no banners, no cookie notices.** The site collects no data. There is nothing to consent to.

### 6.3 Code Examples Front and Centre

- **Every API reference page starts with a code example.** Not a description, not a parameter table — a working code example that shows the most common usage. The description and parameter table follow.
- **Code blocks use content tabs** where applicable — show the same operation in Lua and C++ side by side:

```markdown
=== "Lua"
    ```lua
    local entity = ffe.createEntity()
    ffe.setPosition(entity, 100, 200)
    ffe.setSprite(entity, "player.png", 32, 32)
    ```

=== "C++"
    ```cpp
    auto entity = world.createEntity();
    world.addComponent<Transform>(entity, {100.0f, 200.0f});
    world.addComponent<Sprite>(entity, {"player.png", 32, 32});
    ```
```

- **Copy button on every code block.** Material theme includes this by default.
- **Line numbers on code blocks longer than 10 lines.** Helps when tutorials reference specific lines.

### 6.4 Dark/Light Theme Toggle

Material for MkDocs supports a theme toggle with one configuration block:

```yaml
# mkdocs.yml
theme:
  palette:
    - scheme: default
      toggle:
        icon: material/brightness-7
        name: Switch to dark mode
    - scheme: slate
      toggle:
        icon: material/brightness-4
        name: Switch to light mode
```

The user's preference is stored in `localStorage` and persists across visits. The default follows the OS preference (`prefers-color-scheme` media query).

### 6.5 Print-Friendly for Classroom Use

- **Print stylesheet included.** Material theme includes a print stylesheet that hides navigation, footer, and theme toggle, leaving only the content.
- **Tutorial pages can be printed as handouts.** A teacher can print "Build Pong in 15 Minutes" and hand it to a class.
- **No essential information is conveyed only through colour.** Admonitions use icons and labels in addition to colour. Code highlighting uses bold/italic in addition to colour for the print case.
- **Page breaks before major headings in print.** Prevents awkward splits in the middle of a code example.

---

## 7. Hosting

### 7.1 GitHub Pages

The site is hosted on **GitHub Pages** (free, integrated with the repository, HTTPS by default).

**Deployment method:** GitHub Actions (see Section 5.5). The `actions/deploy-pages@v4` action deploys the built static site to the `gh-pages` environment. No `gh-pages` branch is needed — the action uses the newer GitHub Pages deployment API.

**URL:** `https://fastfreeengine.github.io/FastFreeEngine/` (or the organisation-level `https://fastfreeengine.github.io/` if a GitHub organisation is created).

### 7.2 Custom Domain (Future)

GitHub Pages supports custom domains with HTTPS (via Let's Encrypt). When FFE acquires a domain (e.g., `fastfreeengine.org`):

1. Add a `CNAME` file to the site root: `website/docs/CNAME` containing `fastfreeengine.org`.
2. Configure DNS: `A` records pointing to GitHub Pages IPs, `CNAME` for `www` subdomain.
3. Enable "Enforce HTTPS" in the repository's Pages settings.

No architecture changes are needed. MkDocs generates relative URLs by default, so the site works at any base path.

### 7.3 CDN (Future)

If traffic exceeds GitHub Pages limits (100 GB/month bandwidth, 10 builds/hour), the static site can be deployed to Cloudflare Pages or Netlify with zero changes to the build pipeline. The output is a directory of static HTML/CSS/JS files — any static hosting provider works.

---

## 8. MkDocs Configuration

The full `mkdocs.yml` configuration:

```yaml
site_name: FastFreeEngine
site_description: A free, open-source game engine that runs on old hardware
site_url: https://fastfreeengine.github.io/FastFreeEngine/
repo_url: https://github.com/FastFreeEngine/FastFreeEngine
repo_name: FastFreeEngine

theme:
  name: material
  palette:
    - scheme: default
      primary: indigo
      accent: amber
      toggle:
        icon: material/brightness-7
        name: Switch to dark mode
    - scheme: slate
      primary: indigo
      accent: amber
      toggle:
        icon: material/brightness-4
        name: Switch to light mode
  features:
    - navigation.tabs           # Top-level sections as tabs
    - navigation.sections       # Collapsible sidebar sections
    - navigation.expand         # Expand sidebar by default
    - navigation.top            # "Back to top" button
    - search.suggest            # Search autocomplete
    - search.highlight          # Highlight search terms on page
    - content.code.copy         # Copy button on code blocks
    - content.tabs.link         # Linked content tabs (Lua/C++ sync across page)

plugins:
  - search                      # Built-in lunr.js search

markdown_extensions:
  - admonition                  # Note, warning, tip, danger callouts
  - pymdownx.details            # Collapsible admonitions
  - pymdownx.superfences        # Fenced code blocks with syntax highlighting
  - pymdownx.tabbed:            # Content tabs (Lua / C++ side by side)
      alternate_style: true
  - pymdownx.highlight:         # Code block highlighting
      anchor_linenums: true
  - pymdownx.inlinehilite       # Inline code highlighting
  - attr_list                   # Add HTML attributes to Markdown elements
  - md_in_html                  # Markdown inside HTML blocks
  - toc:                        # Table of contents
      permalink: true           # Anchor links on headings

nav:
  - Home: index.md
  - Getting Started:
    - Building from Source: getting-started/build.md
    - Your First Game: getting-started/first-game.md
    - Project Structure: getting-started/project-structure.md
  - Tutorials:
    - 2D:
      - Pong: tutorials/2d/pong.md
      - Platformer: tutorials/2d/platformer.md
      - Breakout: tutorials/2d/breakout.md
    - 3D:
      - First 3D Scene: tutorials/3d/first-scene.md
      - Materials: tutorials/3d/materials.md
      - Skeletal Animation: tutorials/3d/skeletal-animation.md
    - Multiplayer:
      - Local Multiplayer: tutorials/multiplayer/local.md
      - Online Arena: tutorials/multiplayer/online-arena.md
  - API Reference:
    - Core: api/core.md
    - Renderer: api/renderer.md
    - Audio: api/audio.md
    - Physics: api/physics.md
    - Scripting: api/scripting.md
    - Networking: api/networking.md
  - How It Works:
    - ECS Architecture: internals/ecs.md
    - Renderer Pipeline: internals/renderer.md
    - Networking Internals: internals/networking.md
    - Editor Architecture: internals/editor.md
  - Community:
    - Contributing: community/contributing.md
    - GitHub: community/github.md
```

---

## 9. Directory Structure

```
website/                             Website root (not deployed — contains source + config)
    mkdocs.yml                       MkDocs configuration
    requirements.txt                 Python dependencies (mkdocs-material pinned version)
    docs/                            Markdown source files
        index.md                     Home page
        getting-started/
            build.md                 Building from source (Linux, macOS, Windows)
            first-game.md            Your first game in 15 minutes
            project-structure.md     Directory layout and where to put your code
        tutorials/
            2d/
                pong.md
                platformer.md
                breakout.md
            3d/
                first-scene.md
                materials.md
                skeletal-animation.md
            multiplayer/
                local.md
                online-arena.md
        api/                         Auto-generated from .context.md (see Section 5.2)
            core.md
            renderer.md
            audio.md
            physics.md
            scripting.md
            networking.md
        internals/                   Adapted from ADRs (see Section 5.3)
            ecs.md
            renderer.md
            networking.md
            editor.md
        community/
            contributing.md
            github.md
    scripts/
        generate_api_docs.py         .context.md -> API reference pages
        generate_internals_docs.py   ADR -> "How It Works" drafts
    overrides/                       Custom theme overrides
        main.html                    Base template override (if needed)
        assets/
            stylesheets/
                extra.css            Custom CSS (font stack, print styles)
            images/
                logo.svg             FFE logo
                favicon.ico          Browser tab icon
    site/                            Build output (git-ignored, deployed by CI)
```

### 9.1 What Goes Where

- **`website/docs/`** — All content that becomes web pages. Hand-written and generated Markdown.
- **`website/scripts/`** — Build-time scripts that generate content from engine source. Run before `mkdocs build`.
- **`website/overrides/`** — Material theme overrides. Minimal — only what the default theme does not provide.
- **`website/site/`** — Build output. Git-ignored. Produced by `mkdocs build`, deployed by CI.

### 9.2 Git Considerations

- `website/site/` is added to `.gitignore`. Build artifacts are never committed.
- `website/docs/api/` contains generated files. These ARE committed so that the site can be built without running the generation scripts (useful for local preview). The generation scripts overwrite them — the committed versions are a cache, not the source of truth.
- `website/docs/internals/` contains human-edited files. The generation script produces drafts; a human edits them. Once edited, the human-authored version is the source of truth. The script will not overwrite files that already exist (it only generates missing pages).

---

## 10. Alternatives Considered

### 10.1 Hugo

Hugo is the fastest static site generator (single Go binary, ~2s build for 500 pages). It ships zero client-side JavaScript by default.

**Rejected because:** Hugo lacks built-in search, built-in dark/light toggle, and the rich Markdown extensions (admonitions, content tabs, code annotations) that MkDocs Material provides. Building a Hugo theme with equivalent documentation UX would require significant custom development. The ~8 second build time difference does not justify the feature gap for a documentation site.

### 10.2 Docusaurus

Docusaurus (Facebook/Meta) is a popular documentation framework with built-in search, versioning, and i18n.

**Rejected because:** Docusaurus ships a React runtime (~300 KB+) to every page visitor. Pages hydrate on the client, adding load time and CPU usage. This violates the core constraint that the site must work well on old hardware and slow connections. Docusaurus also uses MDX (JSX in Markdown), which is a different authoring experience from the pure Markdown the engine team already writes.

### 10.3 Astro

Astro is a modern static site framework with an "islands architecture" that can ship zero JS for static pages.

**Rejected because:** Astro's documentation capabilities require building or adopting a theme (Starlight is the official docs theme, but it is newer and less battle-tested than MkDocs Material). Astro's component model is powerful but unnecessary for a pure Markdown documentation site. It introduces Node.js as a build dependency and a steeper learning curve for contributors who just want to write Markdown.

### 10.4 Jekyll

Jekyll is the original GitHub Pages static site generator, with native deployment support.

**Rejected because:** Jekyll is slow to build (Ruby, ~60s for 500 pages), has no built-in search, and requires plugins for features that MkDocs Material provides out of the box. Its native GitHub Pages support is nullified by GitHub Actions, which can deploy any static site generator's output.

### 10.5 Custom React/Next.js Site

A custom-built site with a modern JavaScript framework.

**Rejected immediately.** This violates the "no heavy JS frameworks" constraint, requires frontend engineering expertise the team may not have, and adds an ongoing maintenance burden for a problem that MkDocs Material solves out of the box.

---

## 11. Implementation Phases

The website is Phase 5 in the FFE roadmap. Within Phase 5, the website itself is delivered incrementally:

### 11.1 Milestone 1 — Skeleton and Getting Started (1-2 sessions)

- Create `website/` directory structure
- Write `mkdocs.yml` configuration
- Write `website/docs/index.md` (home page)
- Write Getting Started guides (build from source, first game, project structure)
- Set up GitHub Actions workflow for deployment
- Deploy to GitHub Pages
- **Exit criterion:** A visitor can go from the home page to building and running their first FFE game.

### 11.2 Milestone 2 — API Reference Pipeline (1 session)

- Write `website/scripts/generate_api_docs.py`
- Generate API reference pages from all `.context.md` files
- Review and fix any `.context.md` formatting issues exposed by web rendering
- **Exit criterion:** Every engine subsystem has a published API reference page.

### 11.3 Milestone 3 — Tutorials (2-3 sessions)

- Write 2D tutorials (Pong, Platformer, Breakout)
- Write 3D tutorials (First 3D Scene, Materials)
- Write Multiplayer tutorials (Local, Online Arena)
- **Exit criterion:** A beginner can follow tutorials from "never used FFE" to "built three games."

### 11.4 Milestone 4 — How It Works (1-2 sessions)

- Write `website/scripts/generate_internals_docs.py`
- Generate and edit "How It Works" pages from ADRs
- Add educational framing and diagrams
- **Exit criterion:** A curious student can understand how the ECS, renderer, and networking work internally.

### 11.5 Milestone 5 — Polish and Community (1 session)

- Custom CSS refinements (font stack, print styles, responsive tweaks)
- Community pages (contributing, links)
- Logo and favicon
- Custom domain setup (if domain acquired)
- **Exit criterion:** The site is visually polished and ready to share publicly.

---

## 12. Open Questions

These do not block initial implementation but should be resolved during Phase 5:

1. **Versioned documentation:** Should the site support multiple engine versions (e.g., v1.0 docs alongside v2.0 docs)? MkDocs Material supports this via the `mike` plugin. Deferred until FFE has a formal release versioning scheme.

2. **Interactive code examples:** The roadmap mentions "embedded code editors, live examples." This likely requires a WASM build of the Lua interpreter or a server-side execution sandbox. This is a significant engineering effort and should be a separate ADR if pursued.

3. **Automated tutorial testing:** The roadmap constraint says "Tutorials must be tested against the current engine version (CI validates examples)." This requires extracting code blocks from tutorial Markdown and running them as Lua scripts in CI. The mechanism needs design — likely a custom MkDocs plugin or a standalone script that parses fenced code blocks tagged with `lua test`.

4. **Search index size:** For a large site (500+ pages), the lunr.js search index can grow to several hundred KB. If this becomes a performance concern, options include: splitting the index by section, lazy-loading the index on first search interaction, or switching to pagefind (a Rust-based static search tool with smaller indexes).

5. **Internationalisation:** Should the site support multiple languages? MkDocs Material supports i18n via the `i18n` plugin. Deferred unless there is community demand.
