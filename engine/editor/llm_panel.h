#pragma once
#ifdef FFE_EDITOR

// llm_panel.h -- In-editor LLM (AI assistant) chat panel.
//
// LLMPanel renders a docked ImGui panel that lets developers query a
// configured LLM endpoint (OpenAI-compatible) without leaving the editor.
// Relevant .context.md files are automatically assembled into the system
// prompt so the model answers in terms of the FFE API.
//
// This is an editor-only feature (#ifdef FFE_EDITOR). It is never compiled
// into ffe_runtime or any shipped game binary.
//
// Threading: one background std::thread per request. The render thread polls
// a std::atomic<bool> flag and never blocks. Shared state is owned by a
// shared_ptr so the panel can be destroyed safely while a request is in
// flight (see §7.3 of the ADR).
//
// Owner: engine-dev
// Tiers: Editor-only; LEGACY+ (no GPU dependency — ImGui CPU rasterisation).
// ADR: docs/architecture/adr-phase10-m3-llm-panel.md

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ffe::editor {

// ---------------------------------------------------------------------------
// ScriptEditorSlot — minimal interface for injecting text into the active
// script editor. The concrete implementation in the standalone editor app
// inherits this. LLMPanel holds a non-owning pointer.
// ---------------------------------------------------------------------------
class ScriptEditorSlot {
public:
    virtual ~ScriptEditorSlot() = default;

    // Append text at the end of the buffer.
    virtual void appendText(std::string_view text) = 0;

    // Replace the current text selection with text.
    // If there is no selection, behaves as appendText.
    virtual void replaceSelection(std::string_view text) = 0;
};

// ---------------------------------------------------------------------------
// LLMConfig — runtime configuration for the LLM endpoint.
// Populated from ~/.ffe/preferences.json at startup.
// NOT serialised by LLMPanel; write-back goes through PreferencesManager (M4).
// ---------------------------------------------------------------------------
struct LLMConfig {
    std::string baseUrl;                   // e.g. "https://api.openai.com"
    std::string apiKey;                    // Bearer token; NEVER logged
    std::string model{"gpt-4o"};          // model ID; default "gpt-4o"
    int         timeoutSecs      = 30;     // HTTP request timeout in seconds
    size_t      maxResponseBytes = 32768;  // Hard cap on response body [512, 65536]
    std::string caBundle;                  // Path to CA bundle; empty = system default
};

// ---------------------------------------------------------------------------
// LLMResponse — result of one LLM request.
// Written once by the background thread before m_pending is cleared.
// ---------------------------------------------------------------------------
struct LLMResponse {
    std::string text;             // Extracted response text (content field from JSON)
    bool        success = false;
    std::string errorMsg;         // Human-readable error; MUST NOT contain the API key
};

// ---------------------------------------------------------------------------
// LLMSharedState — shared between the render thread and the background worker.
// Owned via shared_ptr so that panel destruction while a request is in flight
// is safe: the thread holds its own shared_ptr copy (§7.3 of the ADR).
// ---------------------------------------------------------------------------
struct LLMSharedState {
    std::atomic<bool> pending{false};  // render thread sets true; bg thread clears
    LLMResponse       result{};        // written once by bg thread; read after pending == false
    std::mutex        resultMutex;     // guards result during write
    LLMConfig         config;          // copy of config captured at submit time; read-only in bg thread
};

// ---------------------------------------------------------------------------
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
//   render() and the background thread share m_pending, m_result via
//   LLMSharedState. All access is safe per §7.2 of the ADR.
//   Do NOT call setConfig() or setContextFiles() while isRequestPending().
// ---------------------------------------------------------------------------
class LLMPanel {
public:
    LLMPanel();
    ~LLMPanel();

    // Non-copyable, non-movable — owns thread state.
    LLMPanel(const LLMPanel&)            = delete;
    LLMPanel& operator=(const LLMPanel&) = delete;

    // Set the LLM endpoint configuration.
    // Clamps maxResponseBytes to [512, 65536] and logs a warning if clamped.
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

    // ---------------------------------------------------------------------------
    // Internal helpers — public for unit testing only. Do not call from game code.
    // ---------------------------------------------------------------------------

    // Load and truncate .context.md files into a budget-limited string.
    // contextBudget is in characters (not tokens).
    static std::string loadContextBlock(const std::vector<std::string>& paths,
                                        size_t contextBudget);

    // Fire the HTTP request synchronously (call only from background thread).
    // Writes result to *out. Never throws.
    static void doRequest(const LLMConfig& cfg,
                          const std::string& systemPrompt,
                          const std::string& userPrompt,
                          LLMResponse* out) noexcept;

    // Scrub the API key from an error message, replacing it with "[REDACTED]".
    // Returns the scrubbed string. If apiKey is empty, returns msg unchanged.
    static std::string scrubApiKey(const std::string& msg, const std::string& apiKey);

private:
    // --- Configuration ---
    std::vector<std::string> m_contextFiles{};
    ScriptEditorSlot*        m_scriptSlot = nullptr;

    // --- Async request state ---
    // Shared state between the render thread and the background worker.
    // Allocated as a shared_ptr so that the background thread's captured copy
    // keeps the state alive even after LLMPanel is destroyed. See §7.3.
    std::shared_ptr<LLMSharedState> m_state;

    std::thread m_workerThread{};  // joined in destructor after m_state.reset()

    // --- UI state ---
    // ImGui::InputTextMultiline requires a fixed-size char buffer.
    static constexpr size_t INPUT_BUF_SIZE = 4096;
    char m_inputBuf[INPUT_BUF_SIZE] = {};
    bool m_visible                  = true;

    // Response display state — only accessed from render thread.
    std::string m_lastResponse{};
    std::string m_lastError{};
    bool        m_showInsertButton = false;

    // Preferences file world-readable warning state.
    bool m_prefsWorldReadable = false;

    // --- Internal helpers ---

    // Assemble the system prompt context block from m_contextFiles.
    // Reads files synchronously at submit time (fast — small files).
    std::string assembleSystemContext() const;

    // Render the settings sub-panel (base URL + API key fields, masked).
    void renderSettingsSection();

    // Render the response area (scrollable ImGui child region).
    void renderResponseArea();

    // Check ~/.ffe/preferences.json permissions and update m_prefsWorldReadable.
    void checkPrefsPermissions();

    // Validate that caBundle path is a regular file (not symlink, dir, or device).
    // Returns true only for a regular file. Uses stat() + S_ISREG.
    static bool validateCaBundle(const std::string& path);

    // Poll the shared state after a frame: if pending just cleared, copy result
    // into m_lastResponse / m_lastError and update m_showInsertButton.
    void pollResult();
};

} // namespace ffe::editor

#endif // FFE_EDITOR
