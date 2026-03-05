# FastFreeEngine

**A free, open-source C++ game engine that runs on the hardware you already have.**

---

## What is FFE?

FastFreeEngine exists to unlock creativity. A student with a ten-year-old laptop should be able to build a game that runs beautifully and share it with the world. FFE is designed to be learned from, not just used — if you came here because you want to make games, you are exactly who this engine is for.

The engine is free and open source forever, MIT licensed. Performance is how we deliver on that promise: by running well on old hardware, we remove the barrier between your idea and your ability to build it.

---

## Hardware Tiers

Every FFE system declares which hardware tiers it supports. You pick the tier that matches your machine, and the engine guarantees 60 fps on that tier or the feature does not ship.

| Tier | Era | GPU API | Min VRAM | Threading | Default |
|------|-----|---------|----------|-----------|---------|
| **RETRO** | ~2005 | OpenGL 2.1 | 512 MB | Single-core safe | No |
| **LEGACY** | ~2012 | OpenGL 3.3 | 1 GB | Single-core safe | **Yes** |
| **STANDARD** | ~2016 | OpenGL 4.5 / Vulkan | 2 GB | Multi-threaded | No |
| **MODERN** | ~2022 | Vulkan | 4 GB+ | Multi-threaded | No |

**LEGACY is the default tier.** If you are unsure which to pick, use LEGACY. It targets hardware from around 2012 — most laptops and desktops from the last decade will run it comfortably.

---

## Project Status

Early development — core engine skeleton complete, renderer RHI in design. The project builds and tests pass, but there is no playable output yet. Now is a great time to explore the codebase, read the design docs, and follow along as systems come online.

---

## Building from Source

### Prerequisites

**Ubuntu 24.04** is the recommended development platform. Install the following packages:

```bash
sudo apt-get install \
    build-essential gcc-13 g++-13 \
    clang-18 clangd-18 clang-tidy-18 clang-format-18 \
    cmake ninja-build \
    mold ccache \
    libgl1-mesa-dev libvulkan-dev \
    libglfw3-dev libasound2-dev libpulse-dev \
    luajit libluajit-5.1-dev \
    pkg-config curl unzip
```

### vcpkg Setup

FFE uses [vcpkg](https://github.com/microsoft/vcpkg) for C++ dependency management. If you do not have it installed:

```bash
cd ~
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
```

Add these lines to your `~/.bashrc` (or equivalent):

```bash
export VCPKG_ROOT="$HOME/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"
```

Then reload your shell: `source ~/.bashrc`

### Configure and Build

FFE uses CMake with the Ninja backend. The build system automatically picks up vcpkg, mold (linker), and ccache if they are available.

**Build with Clang-18 (primary compiler):**

```bash
cmake -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build build
```

**Build with GCC-13 (secondary compiler):**

```bash
cmake -B build-gcc -G Ninja \
    -DCMAKE_CXX_COMPILER=g++-13 \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build build-gcc
```

### Selecting a Hardware Tier

Pass `-DFFE_TIER=<TIER>` during configuration. The default is `LEGACY`.

```bash
# Target older hardware
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DFFE_TIER=RETRO

# Target modern GPUs with Vulkan
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DFFE_TIER=MODERN
```

### Running Tests

Tests are built by default. After building, run them with CTest:

```bash
cd build
ctest --output-on-failure
```

Or to disable tests entirely, configure with `-DFFE_BUILD_TESTS=OFF`.

---

## Architecture

Design documents for every engine subsystem live in `docs/architecture/`. If you want to understand how a system works or why it was designed a certain way, start there.

---

## AI-Native Documentation

FFE ships with machine-readable documentation designed for AI assistants. Every engine subsystem includes a `.context.md` file in its directory. These files describe the public API, common usage patterns, anti-patterns to avoid, and which hardware tiers are supported.

The idea is simple: point your AI coding assistant at your FFE project directory and it should be able to help you write correct game code immediately. If it cannot, the `.context.md` file is the thing we fix.

If you use an LLM to help you code (and we encourage it), these files are there to make that experience great.

---

## License

FastFreeEngine is licensed under the [MIT License](LICENSE). Free and open source, forever.

---

## Contributing

FFE is in early development. The architecture is still taking shape, and things will change. If you are interested in contributing, the best way to start is to read the design docs in `docs/architecture/`, build the project, and explore the codebase. We are glad you are here.
