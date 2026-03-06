# Design Note: Save/Load System — `ffe.saveData` / `ffe.loadData`

**Author:** architect
**Date:** 2026-03-06
**Status:** PROPOSED — awaiting security-auditor shift-left review before implementation begins
**Tiers:** All (RETRO, LEGACY, STANDARD, MODERN) — file I/O is tier-independent
**Security Review Required:** YES — this system writes files based on Lua-provided filenames and serializes Lua-provided data

---

## 1. Purpose

Provide a simple, safe save/load API for Lua game scripts. Games need to persist player progress, settings, and state between sessions. The API serializes Lua tables to JSON files in a sandboxed directory, with strict validation to prevent path traversal and resource exhaustion.

---

## 2. Lua API

```lua
-- Save a Lua table to a JSON file.
-- filename: bare name with .json extension (no paths, no traversal)
-- data: a Lua table containing serializable types
-- Returns: true on success, nil + error string on failure
local ok, err = ffe.saveData("progress.json", { level = 3, score = 1500 })
if not ok then
    ffe.log("Save failed: " .. err)
end

-- Load a JSON file back into a Lua table.
-- Returns: table on success, nil + error string on failure
local data, err = ffe.loadData("progress.json")
if data then
    ffe.log("Loaded level " .. data.level)
else
    ffe.log("Load failed: " .. err)
end
```

### C++ binding signatures (inside `registerEcsBindings`)

```
ffe.saveData(filename: string, table: table) -> true | (nil, string)
ffe.loadData(filename: string) -> table | (nil, string)
```

---

## 3. Serialization Format

**JSON via nlohmann-json** (already a vcpkg dependency).

JSON is human-readable, debuggable, and well-understood. Save/load is cold-path — JSON parsing overhead is acceptable. Binary formats are not justified at this stage.

### Supported Lua types

| Lua Type | JSON Mapping |
|----------|-------------|
| `string` | JSON string |
| `number` | JSON number (integer or float, nlohmann handles both) |
| `boolean` | JSON `true` / `false` |
| `nil` | JSON `null` |
| `table` (array-like) | JSON array |
| `table` (map-like) | JSON object (keys coerced to strings) |
| Nested tables | Recursive, up to 32 levels deep |

### Unsupported Lua types

Functions, userdata, threads, and circular references are **skipped with a log warning**. The save still succeeds — unsupported values are omitted, not fatal. This matches the principle of least surprise: a game with a callback stored in its state table should not fail to save because of it.

Rationale for skip-with-warning over error: save operations should be resilient. A failed save due to one unsupported field in a large table is worse for the player than a save that silently drops a non-serializable field. The warning gives the developer visibility during development.

---

## 4. File Location

### Save root

A configurable save root directory, set from C++ via a write-once function:

```cpp
// In engine/scripting/ or engine/core/
void setSaveRoot(const std::string& path);  // write-once, same pattern as setAssetRoot
```

This follows the established `setAssetRoot` / `setScriptRoot` pattern: called once from C++ startup code, immutable after first call, not exposed to Lua.

### Directory structure

```
<save_root>/
  saves/
    progress.json
    settings.json
```

The `saves/` subdirectory is created automatically (equivalent to `mkdir -p`) on the first `saveData` call. The save root itself must already exist.

### Demo integration

Demo `main.cpp` files call `setSaveRoot(".")` (or a suitable path) before script execution, placing saves in `./saves/` relative to the working directory.

---

## 5. Security Constraints

This section is the primary input for `security-auditor` shift-left review.

### S1 — Filename validation (path traversal prevention)

The filename argument from Lua is a **bare filename**, not a path. Reject any filename that contains:

- `..` (parent directory traversal)
- `/` (forward slash — no subdirectories allowed)
- `\` (backslash — no Windows-style paths)
- Null bytes (`\0`)

Additionally:
- Filename must end with `.json`
- Filename must not be empty
- Filename length must not exceed 255 bytes (filesystem portability)

These checks run **before** any filesystem operation.

### S2 — Resolved path validation

After constructing `saveRoot + "/saves/" + filename`, resolve via `realpath()` (for loads, where the file exists) or validate the parent directory via `realpath()` (for saves, where the file may not exist yet). Confirm the resolved path starts with the save root prefix.

This is a defence-in-depth layer. S1 should already prevent traversal, but S2 catches any edge case where the OS normalizes differently than expected.

### S3 — Maximum file size: 1 MB

- On save: if the serialized JSON exceeds 1 MB, abort and return an error. Do not write to disk.
- On load: `stat()` the file before reading. If it exceeds 1 MB, abort and return an error. Do not read.

This prevents disk-filling from scripts and protects against loading maliciously large files.

### S4 — Maximum nesting depth: 32

During recursive table-to-JSON serialization, track depth. If depth exceeds 32, stop recursing into that subtable and log a warning. This prevents stack overflow from deeply nested (or accidentally circular) tables.

Circular reference detection is **not** required. Depth limiting at 32 is sufficient — a circular reference will hit the depth limit and stop. Full cycle detection would require a visited-set with O(n) memory, which is unnecessary when depth limiting already caps the recursion.

### S5 — Save root is not exposed to Lua

Same pattern as asset root. Lua scripts cannot call `setSaveRoot`. The save root is a C++ startup parameter. Scripts can only provide the bare filename.

### S6 — No overwrite of non-JSON files

Because S1 enforces the `.json` extension, scripts cannot overwrite arbitrary files even within the save directory.

---

## 6. Error Handling

All errors return `nil, "error message"` to Lua (standard multi-return pattern).

| Condition | Error string |
|-----------|-------------|
| Filename is not a string | `"filename must be a string"` |
| Filename fails validation (S1) | `"invalid filename"` |
| Second argument is not a table (saveData) | `"data must be a table"` |
| Serialized JSON exceeds 1 MB | `"save data too large"` |
| File exceeds 1 MB on load | `"save file too large"` |
| File not found on load | `"file not found"` |
| JSON parse error on load | `"corrupted save file"` |
| Save root not set | `"save root not configured"` |
| Filesystem write error | `"write failed"` |
| Path escapes save root (S2) | `"invalid filename"` (same as S1 — do not leak path info) |

Error strings are intentionally terse and do not include filesystem paths. Detailed diagnostics go to `FFE_LOG_ERROR` for developer visibility.

---

## 7. Performance

Save/load is **cold-path only**. It is never called per-frame.

- Heap allocation during JSON serialization is acceptable.
- `nlohmann::json` object construction, `dump()`, and `parse()` all allocate on the heap. This is fine.
- No arena allocator needed. No per-frame budget impact.
- File I/O is synchronous and blocking. Async save is not needed — save files are small (< 1 MB by constraint).

---

## 8. Implementation Notes for engine-dev

- Add bindings in `registerEcsBindings()` in `engine/scripting/script_engine.cpp`.
- Add `setSaveRoot()` to the appropriate header (likely `engine/scripting/script_engine.h` or a new `engine/core/save_system.h` — implementer's choice).
- Use `nlohmann/json.hpp` for serialization/deserialization. It is already in `vcpkg.json`.
- Lua table to JSON: iterate with `lua_next()`, recurse for nested tables, track depth.
- JSON to Lua table: walk the `nlohmann::json` object, push values with `lua_push*` functions, create tables with `lua_createtable`.
- Table key handling: JSON object keys are always strings. For Lua tables with integer keys, detect array-like tables (consecutive integer keys starting at 1) and serialize as JSON arrays. Mixed or non-sequential keys serialize as JSON objects with string-coerced keys.
- Use `std::filesystem::create_directories()` for the `saves/` directory creation (C++17, available in C++20).
- Type check with `lua_type()` before `lua_tostring()` — reject coercion from non-string types for the filename argument (matches loadTexture pattern).

---

## 9. Deferred Items

- **Encryption / obfuscation.** Not in scope. Save files are plaintext JSON. If anti-cheat is needed, it is a future feature.
- **Save slots / save management API.** `ffe.listSaves()`, `ffe.deleteSave()` — useful but not required for initial implementation. Add when a game needs them.
- **Binary save format.** JSON is sufficient. Binary (MessagePack, FlatBuffers) deferred unless profiling shows JSON is too slow for realistic save sizes.
- **Cloud saves / sync.** Far future. Requires networking subsystem.

---

*This design note is complete. security-auditor should review Section 5 (Security Constraints) before implementation begins. engine-dev should not proceed until the shift-left review clears.*
