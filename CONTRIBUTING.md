# Contributing to FastFreeEngine

Thanks for your interest in contributing to FFE! This guide covers the essentials.

## Building

See [README.md](README.md) for full build instructions. The short version:

**Clang-18 (primary compiler):**

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build
ctest --test-dir build --output-on-failure
```

**GCC-13 (secondary compiler):**

```bash
cmake -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build-gcc
ctest --test-dir build-gcc --output-on-failure
```

All code must compile cleanly on **both compilers** with zero warnings (`-Wall -Wextra`).

**Build toolchain notes:**
- **Ninja** is the build backend (faster than Make)
- **mold** is the linker (faster link times). The CMake configuration selects it automatically when available
- **ccache** is used for compilation caching. Install it to speed up rebuilds: `sudo apt install ccache`

## Code Style

### Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Types | PascalCase | `RenderPass`, `PhysicsWorld` |
| Functions/methods | camelCase | `createBuffer()`, `submitDrawCall()` |
| Variables/parameters | camelCase | `frameCount`, `maxEntities` |
| Constants/enums | UPPER_SNAKE_CASE | `MAX_DRAW_CALLS`, `DEFAULT_WIDTH` |
| Private members | `m_` prefix | `m_frameAllocator`, `m_entityCount` |
| Namespaces | lowercase | `ffe::renderer`, `ffe::physics` |
| Files | snake_case | `render_pass.h`, `physics_world.cpp` |
| Header guards | `#pragma once` | |

### Key Rules

- **C++20** throughout the engine
- **No RTTI** (`dynamic_cast`, `typeid`) in engine core
- **No exceptions** in engine core -- use return values or error codes
- **No virtual function calls** in per-frame code
- **No heap allocations** in hot paths (use arena allocators)
- **Const everything** -- parameters, member functions, local variables
- **No raw owning pointers** -- use `std::unique_ptr` or arena allocation
- `#pragma once` for all headers

See [.claude/CLAUDE.md](.claude/CLAUDE.md) for the complete Engine Constitution.

## Commit Messages

We use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add sprite animation system
fix: resolve texture leak on shutdown
docs: update scripting .context.md with new bindings
test: add collision system edge case tests
refactor: simplify render queue sort key packing
```

## Pull Request Process

1. Create a feature branch from `main`
2. Make your changes
3. Ensure all tests pass on both compilers
4. Ensure zero warnings with `-Wall -Wextra`
5. Write or update tests for new functionality
6. Update relevant `.context.md` files if you changed a public API (see "AI-Native Documentation" below)
7. Open a PR with a clear description of what and why

## Tests

Tests use [Catch2](https://github.com/catchorg/Catch2). Test files live in `tests/` mirroring the engine directory structure.

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run specific tests by name pattern
ctest -R "collision"
```

New features should include tests. Bug fixes should include a regression test.

## Performance

FFE is performance-first. Before submitting code:

- No `std::function` in hot paths (hides heap allocations)
- No `new`/`delete` in per-frame code
- No `vector::push_back` without `reserve` in hot paths
- Measure before and after any optimisation

## Dependencies

New dependencies must be:
- Added to `vcpkg.json`
- Flagged explicitly in the commit message
- Justified in the PR description

## AI-Native Documentation

Every engine subsystem has a `.context.md` file in its directory. These files are written for LLMs to consume so developers can use AI assistants to write correct FFE game code. If you change a public API (add a function, change parameters, add a Lua binding), **update the corresponding `.context.md` file** in the same PR. The `.context.md` files must document:

- Every public function/class with parameter types and return types
- Common usage patterns (3-5 code examples)
- What NOT to do (anti-patterns and common mistakes)
- Tier support and dependencies

See [.claude/CLAUDE.md](.claude/CLAUDE.md) Section 9 for the full documentation philosophy.

## Getting Help

- **Tutorial:** [docs/tutorial.md](docs/tutorial.md) walks through building a game from scratch in Lua
- **API reference:** Each `engine/*/` directory has a `.context.md` file with complete API documentation
- **Scripting reference:** [engine/scripting/.context.md](engine/scripting/.context.md) lists all `ffe.*` Lua bindings
- **Architecture:** [docs/architecture/](docs/architecture/) contains design notes for engine subsystems
- **Engine constitution:** [.claude/CLAUDE.md](.claude/CLAUDE.md) defines coding standards, naming conventions, and process rules

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
