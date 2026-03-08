# ADR — Phase 10 M3: LLM Integration Panel

**Status:** Proposed
**Author:** architect
**Date:** 2026-03-08
**Phase:** 10 (Advanced Editor)
**Milestone:** M3 — LLM Integration Panel
**Related ADRs:** adr-phase10-m1-prefab-system.md, adr-phase10-m2-visual-scripting.md

---

## 1. Context and Motivation

Phase 10 is extending the FFE standalone editor with advanced authoring features. M1 added the Prefab System; M2 added Visual Scripting. M3 closes the AI-native authoring loop: the editor gains an embedded LLM chat panel so developers can query an AI assistant without leaving the editor, receiving code suggestions grounded in FFE's own `.context.md` documentation.

FFE has shipped `.context.md` files in every subsystem since Phase 1. Those files were designed specifically for LLM consumption (see CLAUDE.md §9). M3 makes that intent real at runtime: the editor assembles relevant context automatically, sends it alongside the developer's question, and lets them inject the result directly into the active script slot.

This feature is editor-only. It adds no runtime cost to shipped games. The LLM is always external (cloud API, e.g. OpenAI-compatible endpoint); local inference is explicitly deferred.

---

## 2. Scope

### 2.1 In Scope — M3

- **LLM Chat Panel:** Docked ImGui panel (`ffe::editor::LLMPanel`) with a text input field for the user's message, a scrollable response area, and an "Insert" button.
- **Context-Aware System Prompt Assembly:** Loads `.context.md` files from the relevant engine subdirectories based on what the user has selected in the editor hierarchy. Assembles them into the system prompt up to a 4096-character budget.
- **Async HTTP Transport:** Each LLM request fires in a background `std::thread`. The render loop never blocks.
- **Lua Snippet Injection:** "Insert" button appends the LLM's response into the active `ScriptEditorSlot` text buffer. Injection is always explicit — never automatic.
- **Credential Management:** API key and base URL stored in `~/.ffe/preferences.json`, outside the project directory. File permissions enforced to 0600 on Linux/macOS.
- **Response Display:** Full response text (up to 32 KB) displayed in a scrollable ImGui child region with word-wrap.

### 2.2 Out of Scope — Deferred to Future Milestones

- **Streaming token responses:** The panel receives the complete response in one read. Streaming (SSE) is deferred.
- **Persistent conversation history across sessions:** No conversation memory is written to disk. Each panel open starts a fresh context.
- **In-session multi-turn conversation:** M3 supports single-shot Q&A. The context window is rebuilt fresh for each request. Full conversation threading is deferred.
- **Local model inference:** No llama.cpp, GGUF, or ONNX integration. External API only.
- **Plugin distribution / marketplace:** No mechanism for third-party LLM backend plugins.
- **Fine-tuning / RLHF:** No training data collection or feedback loop.
- **Model selection UI:** Model ID is hardcoded per endpoint configuration. A model picker is deferred.

---

## 3. HTTP Transport

### 3.1 Option Evaluation

#### Option A — libcurl

libcurl is the industry-standard C HTTP client. It supports TLS natively, proxies, redirects, and virtually every HTTP variant. It is available in vcpkg (`curl` package).

**Assessment:**

- Mature, battle-tested, extensively audited.
- vcpkg `curl` package brings in its own OpenSSL build. This adds meaningful build-time cost and bundle size (~2 MB shared / larger static).
- **Not currently in `vcpkg.json`.** Would be a new dependency.
- Async use requires either `curl_multi` (callback-heavy state machine) or running a blocking `curl_easy_perform` in a `std::thread` (simple and sufficient for our one-request-at-a-time model).
- API is C, verbose, and requires careful lifetime management of `CURL*` handles.

#### Option B — cpp-httplib

cpp-httplib is a header-only C++ HTTP/HTTPS client (and server). HTTPS requires OpenSSL headers at compile time and linking against the system OpenSSL (or a vcpkg-provided one).

**Assessment:**

- Header-only: zero additional vcpkg entries if OpenSSL is provided by the system.
- Simpler API than libcurl for our use case (one POST, one response read).
- TLS support is solid on Linux (OpenSSL 3.x). On Windows (MinGW), OpenSSL availability must be confirmed — this is a known MinGW packaging wrinkle.
- Actively maintained; used in production at moderate scale.
- `httplib::Client::Post()` is synchronous; we call it in a `std::thread` (same pattern as libcurl threaded approach).
- No additional vcpkg entry needed if system OpenSSL is present. If not, we add `openssl` to `vcpkg.json`.

#### Option C — Raw BSD Sockets

Implementing TLS over raw sockets requires embedding a TLS library (mbedTLS, BearSSL, etc.) and writing HTTP/1.1 framing by hand. Maintenance burden is unacceptably high for a non-core subsystem.

**Decision: NOT recommended.** Ruled out immediately.

### 3.2 Recommendation: cpp-httplib

**Chosen: cpp-httplib** (Option B).

Justification:

1. **No new vcpkg.json entry under the common case.** The Linux CI environment has OpenSSL 3.x available. Windows (MinGW) will require `openssl` in `vcpkg.json` if the toolchain does not provide it — this is flagged as an open question (§11).
2. **Simpler integration.** One `#include "httplib.h"` with `CPPHTTPLIB_OPENSSL_SUPPORT` defined. No handle lifecycle, no callback state machine.
3. **Bundle size.** No additional shared library shipped. libcurl static linkage adds ~2 MB to the editor binary for capability we do not need (proxies, FTP, IMAP, etc.).
4. **Sufficient capability.** We need exactly: POST with a JSON body, read a JSON response, enforce a byte cap, enforce a timeout. cpp-httplib covers all of this.
5. **Existing pattern.** FFE already vendors `cgltf.h` as a header-only library (`third_party/`). Adding `httplib.h` to `third_party/` is consistent.

**vcpkg.json impact:**

- If system OpenSSL is present at cmake time: **no change** to `vcpkg.json`.
- If not (primarily MinGW Windows cross-compile): add `"openssl"` to `vcpkg.json` dependencies and note it in the commit message per CLAUDE.md §4.

**File placement:** `third_party/httplib.h` (vendored single-header, pinned to a known release SHA). The commit message must record the version pinned.

**Build flag:** `CPPHTTPLIB_OPENSSL_SUPPORT=1` added to the editor target in `engine/editor/CMakeLists.txt`.

---

## 4. Context Assembly

### 4.1 Source Files

The system prompt is assembled from `.context.md` files in the engine subdirectories. The mapping from editor selection to included files is:

| Editor Selection State | `.context.md` Files Included (priority order) |
|---|---|
| Nothing selected | `engine/core/.context.md` |
| 2D entity selected (has Sprite/Tilemap/Particle) | `engine/core/.context.md`, `engine/renderer/.context.md` |
| 3D entity selected (has Transform3D/Mesh/Material3D) | `engine/core/.context.md`, `engine/renderer/.context.md` |
| Entity with Collider2D | `engine/core/.context.md`, `engine/physics/.context.md` |
| Entity with networking component | `engine/core/.context.md`, `engine/networking/.context.md` |
| Script editor active (any entity) | `engine/scripting/.context.md` appended last |
| Multiple conditions | All matching files, in the order listed above, deduplicated |

Detection of the selected entity's component set is done by `LLMPanel::setContextFiles(std::vector<std::string> paths)` — the caller (the editor shell) resolves which paths to pass based on the currently selected entity. `LLMPanel` does not inspect the ECS directly; it only reads the file list it is given.

### 4.2 Token Budget

- **Hard cap: 4096 characters** for the assembled system prompt context block (not counting the user message).
- Files are consumed in priority order (as listed above). Each file is read in full; if it would exceed the remaining budget, it is truncated at the last complete line that fits.
- The user's message is appended after the context block, separated by a blank line. It is not counted against the 4096-character budget.
- The final prompt sent to the API is: `[system context block]\n\n[user message]`.

### 4.3 System Prompt Prefix

A fixed preamble is prepended before any `.context.md` content:

```
You are an AI assistant for the FastFreeEngine (FFE) game engine.
Answer questions using only the API documented below.
Produce Lua code compatible with FFE's LuaJIT scripting layer.
Do not reference APIs not shown in the documentation below.

--- FFE DOCUMENTATION ---
```

This prefix is approximately 200 characters and is included within the 4096-character budget.

### 4.4 HTTP Request Messages Format

The LLM API request uses the OpenAI chat completion messages format: a `system` message containing the assembled context block (preamble + `.context.md` content), and a `user` message containing the user's query. These are sent as separate role entries in the `messages` array — never concatenated into a single string. This eliminates prompt injection at the API boundary by ensuring the LLM always receives structural role separation between engine documentation and user input.

```json
{
  "model": "<cfg.model>",
  "messages": [
    { "role": "system", "content": "<assembled context block>" },
    { "role": "user",   "content": "<user query from m_inputBuf>" }
  ]
}
```

### 4.5 File I/O

Context file reads happen on the **render thread** at `render()` time, only when a new request is submitted (not every frame). File reads are synchronous and fast (<1 ms for files of this size). The background HTTP thread receives a pre-assembled `std::string` — it does not touch the filesystem.

---

## 5. Lua Snippet Injection

### 5.1 ScriptEditorSlot Interface

The script editor text buffer is abstracted behind a minimal interface. This keeps `LLMPanel` decoupled from the concrete editor implementation:

```cpp
// engine/editor/llm_panel.h
namespace ffe::editor {

// Minimal interface for a script editor text slot that LLMPanel can inject into.
// The concrete implementation (in the standalone editor app) must inherit this.
class ScriptEditorSlot {
public:
    virtual ~ScriptEditorSlot() = default;

    // Append text at the end of the buffer.
    virtual void appendText(std::string_view text) = 0;

    // Replace the current text selection with text.
    // If there is no selection, behaves as appendText.
    virtual void replaceSelection(std::string_view text) = 0;
};

} // namespace ffe::editor
```

The virtual dispatch here is acceptable: `ScriptEditorSlot` is called only on an explicit user button click, not in any hot path.

### 5.2 Injection Flow

1. User clicks "Insert" button in the LLM panel.
2. `LLMPanel::render()` calls `m_scriptSlot->replaceSelection(m_lastResponse.text)` (if a slot is registered and a response exists).
3. The text is placed in the script editor buffer. It is NOT executed.
4. Execution requires the user to explicitly click "Run" in the script editor — no auto-exec path exists.

### 5.3 Lua Binding (editor-only, 2 bindings)

Two Lua bindings are added to allow editor-hosted scripts to query the configured LLM endpoint (for AI-assisted authoring workflows — e.g., generating NPC dialogue templates during level editing). These are strictly opt-in and disabled if no API key is configured:

```
ffe.llmQuery(prompt: string) -> string|nil, string|nil
  -- Synchronous (blocks Lua VM, not the render loop -- Lua runs in a scripting tick).
  -- Returns: responseText, errorMsg. errorMsg is nil on success.
  -- Returns nil, "LLM not configured" if no API key is set.

ffe.isLLMConfigured() -> boolean
  -- Returns true if a base URL and API key are present in preferences.
```

**These bindings are ONLY registered in editor builds (`#ifdef FFE_EDITOR`). They are NOT available in `ffe_runtime` game builds. Game scripts cannot make LLM API calls.**

Both bindings are wrapped in `#ifdef FFE_EDITOR` at the registration site in `script_engine.cpp`. The registration block is not compiled into any non-editor target.

Rationale: CLAUDE.md §5 states the Lua sandbox must be escape-proof. Registering unrestricted network egress bindings in `ffe_runtime` would allow any game script to exfiltrate data or make arbitrary outbound HTTP calls, violating the sandbox guarantee.

These bindings read the LLM config from the preferences file (same path: `~/.ffe/preferences.json`). The Lua VM budget (1M instruction limit) is suspended during the blocking HTTP call — the call is made on the Lua coroutine thread, not the render thread.

---

## 6. Credential Security

### 6.1 Storage Location

Editor preferences (including LLM credentials) are stored at:

```
~/.ffe/preferences.json
```

This path is:
- Outside the project directory — it cannot be accidentally committed.
- Per-user — multiple users on the same machine each have independent credentials.
- Consistent with the M4 editor preferences system planned for Phase 10.

The preferences file is created on first use if it does not exist. The directory `~/.ffe/` is created with mode `0700` (owner rwx only). The file itself is created with mode `0600` (owner read/write only).

### 6.2 Preferences File Schema

```json
{
  "llm": {
    "baseUrl": "https://api.openai.com",
    "apiKey": "sk-...",
    "timeoutSecs": 30,
    "maxResponseBytes": 32768,
    "caBundle": ""
  }
}
```

`caBundle` is an optional path to a custom CA certificate bundle. Empty string means the system default CA store is used (cpp-httplib uses the system store via OpenSSL by default).

The `maxResponseBytes` field is clamped to the range [512, 65536] when loaded from preferences. Values below 512 are raised to 512 (with a log warning); values above 65536 are clamped to 65536. This prevents misconfiguration from either starving the response buffer or permitting memory exhaustion.

The `caBundle` path, if non-empty, is validated with `stat()` to confirm it is a regular file (`S_ISREG`, not a symlink, device, or directory) before being passed to cpp-httplib. If validation fails, the request proceeds with the system CA store and a warning is logged.

### 6.3 API Key Handling Rules

- The API key is read from disk into `LLMConfig::apiKey` (`std::string`) at panel init time.
- It is never written to stdout, stderr, or any log file — not even at `TRACE` level.
- It is passed to cpp-httplib as an HTTP Authorization header: `"Authorization: Bearer " + apiKey`. The key never appears in a URL (not a query parameter).
- The `LLMConfig` struct is never serialised back to disk by `LLMPanel`. Writes to `~/.ffe/preferences.json` go through a dedicated `PreferencesManager` (shared with M4). If `PreferencesManager` is not yet available in M3, `LLMPanel` writes directly via a minimal local helper that masks the key in any log output.
- The `LLMResponse::errorMsg` field must NOT include the API key in any error string from the HTTP library. The implementation must strip or replace the Authorization header from any error message before storing it in `LLMResponse::errorMsg`.

### 6.4 File Permission Enforcement

On Linux and macOS, after writing `~/.ffe/preferences.json`:

```cpp
chmod(path.c_str(), 0600);
```

On startup, if the file exists and is world-readable (mode & 0044 != 0), the panel emits a visible warning in the ImGui panel: "Warning: ~/.ffe/preferences.json is world-readable. Run: chmod 600 ~/.ffe/preferences.json". This warning does not block functionality.

On Windows (MinGW builds), `chmod` is unavailable. The implementation uses `SetFileSecurityA` or falls back to a log warning that file ACLs should be reviewed. File permission enforcement is best-effort on Windows.

---

## 7. Async Design

### 7.1 Threading Model

```
Render thread                     Background thread (per request)
─────────────────────────────     ──────────────────────────────────────
LLMPanel::render() called         [spawned on user Submit click]
  if m_pending.load(): show        httplib::Client::Post(...)
    "Waiting..." indicator          → reads response body
  else: show response / idle        → enforces maxResponseBytes cap
                                    → sets m_result (LLMResponse)
  on Submit click:                  → m_pending.store(false)
    assemble context string         → thread exits
    set m_pending.store(true)
    spawn std::thread(requestFn)
    detach() [with join guard]
```

### 7.2 Shared State

Shared between render thread and background thread:

```cpp
// All fields accessed from both threads must use synchronisation.
std::atomic<bool>  m_pending{false};   // render thread polls; bg thread clears
LLMResponse        m_result{};         // written by bg thread once; read by render thread after m_pending == false
std::string        m_pendingPrompt{};  // set before thread spawn; read-only in bg thread
LLMConfig          m_config{};         // set at init time; not mutated while pending
```

`m_result` is written once by the background thread before `m_pending` is set to `false`. The render thread reads `m_result` only after observing `m_pending == false`. Because `m_pending` is `std::atomic<bool>` with default sequential consistency, this constitutes a correct happens-before relationship. No additional mutex is required for `m_result`.

`m_pendingPrompt` is written by the render thread before the `std::thread` is spawned, and read-only inside the thread. No race.

### 7.3 Panel Destructor — shared_ptr Handoff Pattern

The panel must not be destroyed while a background thread holds references to its members. The ONLY specified design for shared state is the `shared_ptr` handoff pattern. The poll-and-abandon approach is explicitly rejected because the abandon path contains undefined behaviour (the background thread writes to members of a destroyed `LLMPanel`).

**Mandatory shared state design:**

```cpp
struct LLMSharedState {
    std::atomic<bool> pending{false};
    LLMResponse result{};
    std::mutex resultMutex;
    LLMConfig config;  // copy held by shared state
};

// Panel holds: std::shared_ptr<LLMSharedState> m_state;
// Thread lambda captures: std::shared_ptr<LLMSharedState> state (copy of shared_ptr)
//
// Destructor: m_state.reset() — panel releases its reference.
// Thread finishes, its shared_ptr copy goes out of scope — state is destroyed.
// No UB: thread always writes to valid shared_ptr-owned memory.
```

The destructor is therefore:

```cpp
LLMPanel::~LLMPanel() {
    m_state.reset();  // panel releases its reference; thread keeps its own copy alive
    // If m_workerThread is joinable, join it (it will finish promptly once the
    // HTTP call completes — no UB possible because shared state is still alive
    // via the thread's own shared_ptr copy).
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}
```

The thread lambda must capture `state` (a copy of the `shared_ptr`, not a raw pointer or reference to `m_state`) so that the reference count stays above zero until the thread exits.

OQ-1 is **RESOLVED** by this pattern. No abandon path exists; the thread always completes with valid memory.

**Note for Windows builds:** If `SetFileSecurityA` fails when enforcing credentials file permissions, the implementation must display a persistent visible ImGui warning banner (not merely a log entry) advising the user that `preferences.json` may be world-readable. A silent log-only fallback is insufficient.

### 7.4 One Request at a Time

Only one background thread may be active at any time. The "Submit" button is greyed out while `m_pending.load() == true`. This is enforced in `render()` using `ImGui::BeginDisabled(m_pending.load())`.

### 7.5 No std::function in Render Loop

The `m_pending` atomic flag is the only communication channel polled each frame. No `std::function`, no `std::queue`, no condition variable is checked in `render()`.

---

## 8. New Files and Ownership

| File | Action | Owner |
|------|--------|-------|
| `engine/editor/llm_panel.h` | Create | engine-dev |
| `engine/editor/llm_panel.cpp` | Create | engine-dev |
| `engine/editor/CMakeLists.txt` | Modify (add llm_panel.cpp, CPPHTTPLIB_OPENSSL_SUPPORT) | engine-dev |
| `engine/editor/.context.md` | Modify (add LLM panel section) | api-designer |
| `engine/scripting/script_engine.h` | Modify (declare 2 new Lua bindings) | engine-dev |
| `engine/scripting/script_engine.cpp` | Modify (implement `ffe.llmQuery`, `ffe.isLLMConfigured`) | engine-dev |
| `tests/editor/test_llm_panel.cpp` | Create | engine-dev |
| `tests/CMakeLists.txt` | Modify (add test_llm_panel) | engine-dev |
| `third_party/httplib.h` | Vendor (pin to known release) | engine-dev |

No files under `engine/renderer/`, `engine/physics/`, `engine/networking/`, `engine/audio/`, or `engine/core/` are modified by this milestone.

---

## 9. Public API — Concrete C++ Signatures

All signatures below are authoritative. Implementation agents must follow them exactly. No deviations without a follow-up ADR amendment.

### 9.1 Data Types

```cpp
// engine/editor/llm_panel.h
#pragma once
#ifdef FFE_EDITOR

#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ffe::editor {

// ScriptEditorSlot — minimal interface for injecting text into the active script editor.
// The concrete implementation in the standalone editor app inherits this.
class ScriptEditorSlot {
public:
    virtual ~ScriptEditorSlot() = default;
    virtual void appendText(std::string_view text) = 0;
    virtual void replaceSelection(std::string_view text) = 0;
};

// LLMConfig — runtime configuration for the LLM endpoint.
// Populated from ~/.ffe/preferences.json at startup.
// NOT serialised by LLMPanel; write-back goes through PreferencesManager (M4).
struct LLMConfig {
    std::string baseUrl;                   // e.g. "https://api.openai.com"
    std::string apiKey;                    // Bearer token; never logged
    std::string model;                     // e.g. "gpt-4o"; default "gpt-4o"
    int         timeoutSecs      = 30;     // HTTP request timeout
    size_t      maxResponseBytes = 32768;  // Hard cap on response body (32 KB). Clamped to [512, 65536] on load from preferences. Values below 512 are raised to 512 (with a log warning); values above 65536 are clamped to 65536.
    std::string caBundle;                  // Path to CA bundle; empty = system default
};

// LLMResponse — result of one LLM request.
// Written once by the background thread before m_pending is cleared.
struct LLMResponse {
    std::string text;       // Extracted response text (content field from JSON)
    bool        success = false;
    std::string errorMsg;   // Human-readable error; MUST NOT contain the API key
};

// LLMPanel — docked ImGui panel for in-editor LLM queries.
//
// Lifecycle:
//   LLMPanel panel;
//   panel.setConfig(cfg);                  // once, at editor init
//   panel.setContextFiles(paths);          // each frame, after selection changes
//   panel.setScriptSlot(&mySlot);          // once (or nullptr to disable inject)
//   panel.render();                        // once per frame, inside ImGui dockspace
//
// Thread safety:
//   render() and the background thread share m_pending, m_result.
//   All access is safe as documented in §7.2.
//   Do NOT call setConfig() or setContextFiles() while isRequestPending().
class LLMPanel {
public:
    LLMPanel();
    ~LLMPanel();

    // Non-copyable, non-movable — owns thread state.
    LLMPanel(const LLMPanel&)            = delete;
    LLMPanel& operator=(const LLMPanel&) = delete;

    // Set the LLM endpoint configuration.
    // Must not be called while isRequestPending() == true.
    void setConfig(LLMConfig cfg);

    // Set the list of .context.md file paths to include in the next request's
    // system prompt. Called each frame by the editor after resolving the
    // selected entity's component set.
    // Files are read when the next Submit is clicked, not when this is called.
    // Must not be called while isRequestPending() == true.
    void setContextFiles(std::vector<std::string> paths);

    // Register the active script editor slot for Lua snippet injection.
    // Pass nullptr to disable injection (Insert button will be hidden).
    // Must not be called while isRequestPending() == true.
    void setScriptSlot(ScriptEditorSlot* slot);

    // Render the LLM panel into the current ImGui dockspace. Call once per frame.
    void render();

    // Returns true if a background HTTP request is in flight.
    bool isRequestPending() const;

private:
    // --- Configuration ---
    std::vector<std::string> m_contextFiles{};
    ScriptEditorSlot*      m_scriptSlot = nullptr;

    // --- Async request state ---
    // Shared state between the render thread and the background worker.
    // Allocated as a shared_ptr so that the background thread's captured copy
    // keeps the state alive even after LLMPanel is destroyed. See §7.3.
    //
    // LLMSharedState contains:
    //   std::atomic<bool> pending{false}  — render thread sets true; bg thread clears
    //   LLMResponse result{}              — written once by bg thread; read by render after pending == false
    //   std::mutex resultMutex            — guards result during write
    //   LLMConfig config                  — copy of config captured at submit time; read-only in bg thread
    std::shared_ptr<LLMSharedState> m_state;

    std::string            m_pendingPrompt{};  // assembled before thread spawn; read-only in bg thread
    std::thread            m_workerThread{};   // joined in destructor or at submit time

    // --- UI state ---
    // ImGui::InputTextMultiline requires a fixed-size char buffer.
    static constexpr size_t INPUT_BUF_SIZE = 4096;
    char    m_inputBuf[INPUT_BUF_SIZE] = {};
    bool    m_visible                  = true;
    bool    m_scrollToBottom           = false;

    // --- Internal helpers ---

    // Assemble the full prompt string from m_contextFiles and m_inputBuf.
    // Reads files synchronously (fast — small files, called once per submit).
    // Returns the assembled prompt string.
    std::string assemblePrompt() const;

    // Load and truncate .context.md files into a budget-limited string.
    // contextBudget is in characters (not tokens).
    static std::string loadContextBlock(const std::vector<std::string>& paths,
                                        size_t contextBudget);

    // Fire the HTTP request synchronously (call only from background thread).
    // Writes result to *out. Never throws.
    static void doRequest(const LLMConfig& cfg,
                          const std::string& prompt,
                          LLMResponse* out) noexcept;

    // Render the settings sub-panel (base URL + API key fields, masked).
    void renderSettingsSection();

    // Render the response area (scrollable ImGui child region).
    void renderResponseArea();
};

} // namespace ffe::editor

#endif // FFE_EDITOR
```

### 9.2 Lua Binding Signatures (in script_engine.cpp)

Both bindings are registered only inside an `#ifdef FFE_EDITOR` block. They are not compiled into `ffe_runtime` or any shipped game binary.

```cpp
#ifdef FFE_EDITOR

// ffe.isLLMConfigured() -> boolean
// Returns true if both baseUrl and apiKey are non-empty in the loaded preferences.
// EDITOR ONLY — not registered in ffe_runtime builds.
static int lua_isLLMConfigured(lua_State* L);

// ffe.llmQuery(prompt: string) -> string|nil, string|nil
// Sends a synchronous LLM request on the Lua coroutine thread.
// Returns: responseText (string) on success, nil on failure.
//          errorMsg (string) on failure, nil on success.
// If LLM is not configured, returns nil, "LLM not configured".
// Instruction limit is NOT applied during the HTTP wait (blocked on I/O, not CPU).
// EDITOR ONLY — not registered in ffe_runtime builds.
static int lua_llmQuery(lua_State* L);

#endif // FFE_EDITOR
```

---

## 10. Security Threat Model

### Threat 1 — API Key Exposure via Log Injection

**Risk:** The API key leaks into log files, stdout, or a debug dump.

**Mitigations:**
- The key is never passed to `FFE_LOG`, `printf`, `fprintf`, or any logging macro.
- `LLMResponse::errorMsg` is scrubbed before storage: any occurrence of the API key value in an error string is replaced with `"[REDACTED]"`. (The implementation must do this explicitly — cpp-httplib may include the Authorization header in error messages.)
- The key field in `~/.ffe/preferences.json` is masked in the UI: rendered as a password field (`ImGuiInputTextFlags_Password`).
- No agent is permitted to add log statements that print `LLMConfig::apiKey`. This is a named prohibition in the implementation instructions.

### Threat 2 — TLS Man-in-the-Middle

**Risk:** An attacker intercepts the HTTPS connection and reads the API key or injects a false response.

**Mitigations:**
- cpp-httplib with OpenSSL enforces certificate validation by default. Peer verification is NOT disabled.
- The `caBundle` field in `LLMConfig` allows operators to specify a custom CA bundle (e.g., corporate proxies with internal CAs). If the path is invalid, the request fails rather than falling back to no verification.
- Disabling certificate verification (`SSL_VERIFY_NONE`) is NOT a supported option — no such UI control or config key is provided.
- The `baseUrl` is validated to start with `https://` before any request is made. An `http://` base URL is rejected with an error (not a warning).

### Threat 3 — Prompt Injection via .context.md

**Risk:** A malicious or corrupted `.context.md` file contains text that hijacks the LLM's instructions (e.g., "Ignore all previous instructions and...").

**Assessment:** `.context.md` files are engine assets checked into the repository and owned by `api-designer`. They are not user-supplied input. The threat surface is low: an attacker would need write access to the repository to inject content.

**Mitigations:**
- Context files are loaded from paths under the project root only. The `loadContextBlock` function validates each path against the project root before opening it (same canonicalization logic as `core/platform.h`).
- Paths passed via `setContextFiles()` that escape the project root are silently dropped (logged at DEBUG, not ERROR — this is not a security event, as the editor itself sets the paths).
- The user's message is included verbatim. No sanitisation is applied — the LLM is expected to interpret the message in the context of the system prompt. This is the normal operating model for LLM APIs.

### Threat 4 — Oversized LLM Response (DoS / Memory Exhaustion)

**Risk:** The LLM API returns a multi-megabyte response body, exhausting editor memory.

**Mitigations:**
- `LLMConfig::maxResponseBytes` (default 32768, hard maximum 65536) caps the response body read in `doRequest()`.
- `doRequest()` uses cpp-httplib's `content_receiver` callback to accumulate bytes and aborts the connection once the cap is hit, setting `LLMResponse::errorMsg = "Response exceeded size limit"`.
- The cap is enforced before JSON parsing — a malformed oversized body cannot trigger a parse explosion.

### Threat 5 — Lua Snippet Execution Without Consent

**Risk:** LLM-generated code is auto-executed, potentially running destructive logic.

**Mitigations:**
- The "Insert" button requires an explicit user click. There is no auto-insert, no timer-based insert, and no keyboard shortcut that bypasses the button.
- After insertion, the code sits in the script editor buffer — it requires a further explicit "Run" action in the script editor to execute.
- `ScriptEditorSlot::appendText` / `replaceSelection` only modify the text buffer. They do not trigger compilation or execution.
- These guarantees are enforced structurally: `LLMPanel` holds a `ScriptEditorSlot*` and cannot call any execution function that does not exist on that interface.

### Threat 6 — Credential File World-Readable

**Risk:** Another user on the same machine reads `~/.ffe/preferences.json` and obtains the API key.

**Mitigations:**
- Directory `~/.ffe/` is created with mode `0700`.
- File `~/.ffe/preferences.json` is created/written with mode `0600`.
- On each panel init, the file's actual permissions are checked. If `stat()` shows `st_mode & 0044 != 0` (group-readable or world-readable), the panel renders a persistent yellow warning banner: "~/.ffe/preferences.json is world-readable. Fix: chmod 600 ~/.ffe/preferences.json". The panel remains functional.
- On Windows, ACL-based access control is noted but enforcement is best-effort (see §6.4).

### Threat 7 — Path Traversal in Context File Loading

**Risk:** A crafted editor state sets context file paths outside the project directory, loading arbitrary files as system prompt material.

**Mitigations:**
- `loadContextBlock()` calls the same `canonicalizePath()` from `engine/core/platform.h` used elsewhere in the engine.
- If `canonicalizePath()` returns false (file does not exist, path too long, or realpath fails), the path is rejected and the file is skipped without attempting to open it.
- Any canonicalized path that does not begin with the project root prefix is skipped.
- This threat is low-impact (context files are read-only and sent to an external API the user already controls), but defence-in-depth is applied regardless.

---

## 11. Tier Support

`LLMPanel` is **editor-only** (`#ifdef FFE_EDITOR`). It is compiled only into `ffe_editor_app` and `ffe_editor` targets, never into `ffe_runtime` or any game binary.

Hardware tier: **LEGACY** (OpenGL 3.3, 1 GB VRAM). The LLM panel has zero GPU dependency — all rendering is ImGui CPU-side rasterisation, identical to the existing `GraphEditorPanel` and `EditorOverlay`. The background HTTP thread is CPU and network I/O bound; it does not compete with the GPU timeline.

The panel degrades gracefully on slow network connections: the "Waiting..." indicator spins until `m_pending` clears or the `timeoutSecs` limit fires.

---

## 12. Test Plan

Tests live in `tests/editor/test_llm_panel.cpp`. They must compile and link without a real network connection (no HTTP requests in CI).

| Test | Approach |
|------|----------|
| `LLMConfig` default values | Unit: construct, assert defaults |
| `loadContextBlock` with empty path list | Unit: returns prefix only |
| `loadContextBlock` budget truncation | Unit: create temp file > 4096 chars, assert truncation |
| `loadContextBlock` path traversal rejection | Unit: pass `../../etc/passwd`, assert not included |
| `LLMResponse` error scrubbing | Unit: inject fake key string into error msg, assert `[REDACTED]` |
| `isRequestPending` initial state | Unit: construct panel, assert false |
| `setContextFiles` / `setConfig` before pending | Unit: set config, assert stored |
| `ScriptEditorSlot` mock inject | Unit: mock slot, call `appendText`, assert received |
| `baseUrl` https-only validation | Unit: set `http://` URL, fire submit, assert error without HTTP call |
| `maxResponseBytes` cap | Unit: stub `doRequest` returning oversized string, assert truncation |
| Panel destructor with no pending thread | Unit: construct, destruct — no hang |

Approximately 11-15 test cases expected. Network is not required; `doRequest` is tested via dependency injection (a test-only constructor or seam that accepts a mock request function).

---

## 13. Open Questions

The following must be resolved before or during implementation. The implementation agent should surface these to PM if not resolved.

**OQ-1 — RESOLVED.** The destructor abandon-path UB has been eliminated by mandating the `shared_ptr<LLMSharedState>` handoff pattern (see §7.3). The panel releases its `shared_ptr` reference in the destructor; the background thread's captured copy keeps the state alive until the thread exits. No UB is possible. Implementation agents must follow §7.3 exactly.

**OQ-2 (vcpkg / Windows):** Does the MinGW cross-compile environment provide OpenSSL headers? If not, `"openssl"` must be added to `vcpkg.json`. The build-engineer should test the MinGW build after M3 lands. If it fails due to missing OpenSSL, the fix is to add the vcpkg entry — this is a one-line change and does not require a new ADR.

**OQ-3 (cpp-httplib version pin):** The implementation agent must pin `third_party/httplib.h` to a specific release (SHA or tagged version). The recommended version is the latest stable release at implementation time. The commit message must include: `vendor: cpp-httplib vX.Y.Z (SHA: ...)`.

**OQ-4 (Model field in LLMConfig):** `LLMConfig` includes a `model` field (`std::string`, default `"gpt-4o"`). This allows the user to target a different model without code changes. The preferences file schema includes `"model"`. No UI for model selection is provided in M3 (the field is set by directly editing `~/.ffe/preferences.json`). A model picker UI is deferred to M4.

**OQ-5 — RESOLVED.** `ffe.llmQuery` uses a separate default timeout of 5 seconds (independent of `LLMConfig.timeoutSecs`, which is editor-panel-specific). This shorter timeout prevents the scripting tick from stalling for the full editor timeout duration. The implementation agent must apply this 5-second timeout specifically to the `lua_llmQuery` HTTP call path in `script_engine.cpp`, separate from and independent of the timeout configured in `LLMConfig`. The instruction counter is suspended before the HTTP call and resumed after (zero Lua instructions are consumed during the I/O wait).

**OQ-6 (PreferencesManager coordination with M4):** M3 writes `~/.ffe/preferences.json` directly via a minimal file helper. M4 will introduce a proper `PreferencesManager` that owns the preferences file. When M4 lands, M3's direct write code must be replaced with `PreferencesManager` calls. The implementation agent should design the M3 file helper so that its interface matches the expected `PreferencesManager` API (to be specified in the M4 ADR), making the replacement a drop-in substitution.

---

## 14. Summary

| Dimension | Decision |
|---|---|
| HTTP transport | cpp-httplib (header-only, vendored to `third_party/httplib.h`) |
| TLS | OpenSSL via cpp-httplib; cert validation enforced; `https://` required |
| Async model | One `std::thread` per request; `std::atomic<bool>` flag; shared_ptr shared state (OQ-1) |
| Context budget | 4096 chars; files in priority order; truncated at line boundary |
| Credential storage | `~/.ffe/preferences.json`; mode 0600; key never logged |
| Injection | Explicit "Insert" button; `ScriptEditorSlot` virtual interface; no auto-exec |
| Lua bindings | `ffe.llmQuery`, `ffe.isLLMConfigured` (2 new bindings, editor-only via `#ifdef FFE_EDITOR`) |
| Tier | Editor-only; LEGACY; no GPU dependency |
| New vcpkg entries | None (common case); `openssl` if MinGW requires it (OQ-2) |
| New third-party files | `third_party/httplib.h` (vendored, version-pinned) |
| Tests | ~11-15 unit tests; no network required in CI |

---

## 15. Revision History

| Rev | Date | Author | Summary |
|-----|------|--------|---------|
| 1 | 2026-03-08 | architect | Initial proposal |
| 2 | 2026-03-08 | architect | Security-auditor shift-left review — 2 HIGH, 4 MEDIUM addressed: shared_ptr destructor design mandated (OQ-1 RESOLVED), Lua bindings restricted to `#ifdef FFE_EDITOR` (sandbox enforcement), `canonicalizePath()` failure branch specified (Threat 7), `caBundle` stat validation added (§6.2), messages array format mandated (§4.4), `maxResponseBytes` clamping specified (§6.2), Windows `SetFileSecurityA` failure banner required (§7.3), `ffe.llmQuery` 5-second timeout specified (OQ-5 RESOLVED) |
