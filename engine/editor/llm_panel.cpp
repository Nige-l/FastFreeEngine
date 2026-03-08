// llm_panel.cpp -- In-editor LLM (AI assistant) chat panel implementation.
//
// See engine/editor/llm_panel.h and docs/architecture/adr-phase10-m3-llm-panel.md
// for the full design rationale, security threat model, and threading contract.

#ifdef FFE_EDITOR

// CPPHTTPLIB_OPENSSL_SUPPORT enables HTTPS in cpp-httplib.
// Defined here (before the include) and also via -DCPPHTTPLIB_OPENSSL_SUPPORT
// in engine/editor/CMakeLists.txt so both paths are covered.
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include "editor/llm_panel.h"

#include "core/logging.h"
#include "core/platform.h"  // canonicalizePath

#include <nlohmann/json.hpp>

// ImGui is only included in non-test builds. Tests exercise the non-UI methods
// (doRequest, loadContextBlock, scrubApiKey, lifecycle) without a render context.
#ifndef FFE_TEST
#include <imgui.h>
#endif

// vendor: cpp-httplib v0.14.3
// Replace with the real httplib.h for actual HTTPS transport.
// third_party/ is in the include path via engine/editor/CMakeLists.txt.
#include <httplib.h>

#include <algorithm>
#include <climits>   // PATH_MAX
#include <cstring>   // strlen
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>    // SetFileSecurityA
#include <aclapi.h>
#else
#include <sys/types.h>
#include <unistd.h>     // access
#include <pwd.h>        // getpwuid
#endif

namespace ffe::editor {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr size_t CONTEXT_BUDGET_CHARS = 4096;
static constexpr size_t MIN_RESPONSE_BYTES   = 512;
static constexpr size_t MAX_RESPONSE_BYTES   = 65536;

// Fixed system-prompt preamble prepended to all context blocks.
// This is counted within the 4096-character budget.
static constexpr const char* SYSTEM_PROMPT_PREFIX =
    "You are an AI assistant for the FastFreeEngine (FFE) game engine.\n"
    "Answer questions using only the API documented below.\n"
    "Produce Lua code compatible with FFE's LuaJIT scripting layer.\n"
    "Do not reference APIs not shown in the documentation below.\n"
    "\n"
    "--- FFE DOCUMENTATION ---\n";

// ---------------------------------------------------------------------------
// LLMPanel constructor / destructor
// ---------------------------------------------------------------------------

LLMPanel::LLMPanel()
    : m_state(std::make_shared<LLMSharedState>())
{
    checkPrefsPermissions();
}

LLMPanel::~LLMPanel() {
    // Release panel's reference first. The thread (if running) holds its own
    // shared_ptr copy — LLMSharedState stays alive until the thread exits.
    m_state.reset();

    // Join the worker thread so the destructor is clean.
    // Because m_state has already been reset, any in-flight HTTP call will
    // complete (or timeout) and write into the thread's local shared_ptr copy.
    // No UB is possible: the thread never touches LLMPanel members directly.
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

// ---------------------------------------------------------------------------
// Configuration setters
// ---------------------------------------------------------------------------

void LLMPanel::setConfig(LLMConfig cfg) {
    // Clamp maxResponseBytes to [MIN_RESPONSE_BYTES, MAX_RESPONSE_BYTES].
    if (cfg.maxResponseBytes < MIN_RESPONSE_BYTES) {
        FFE_LOG_WARN("LLMPanel",
            "maxResponseBytes %zu below minimum %zu; clamping",
            cfg.maxResponseBytes, MIN_RESPONSE_BYTES);
        cfg.maxResponseBytes = MIN_RESPONSE_BYTES;
    }
    if (cfg.maxResponseBytes > MAX_RESPONSE_BYTES) {
        FFE_LOG_WARN("LLMPanel",
            "maxResponseBytes %zu above maximum %zu; clamping",
            cfg.maxResponseBytes, MAX_RESPONSE_BYTES);
        cfg.maxResponseBytes = MAX_RESPONSE_BYTES;
    }
    m_state->config = std::move(cfg);
    checkPrefsPermissions();
}

void LLMPanel::setContextFiles(std::vector<std::string> paths) {
    m_contextFiles = std::move(paths);
}

void LLMPanel::setScriptSlot(ScriptEditorSlot* slot) {
    m_scriptSlot = slot;
}

bool LLMPanel::isRequestPending() const {
    return m_state->pending.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// validateCaBundle
// ---------------------------------------------------------------------------

/*static*/
bool LLMPanel::validateCaBundle(const std::string& path) {
    if (path.empty()) return false;
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) {
        return false;  // file does not exist or inaccessible
    }
    // S_ISREG returns true for regular files only — not symlinks, dirs, devices.
    return S_ISREG(st.st_mode) != 0;
}

// ---------------------------------------------------------------------------
// scrubApiKey
// ---------------------------------------------------------------------------

/*static*/
std::string LLMPanel::scrubApiKey(const std::string& msg, const std::string& apiKey) {
    if (apiKey.empty()) return msg;
    std::string result = msg;
    size_t pos = result.find(apiKey);
    while (pos != std::string::npos) {
        result.replace(pos, apiKey.size(), "[REDACTED]");
        pos = result.find(apiKey, pos + 10);  // 10 = strlen("[REDACTED]")
    }
    return result;
}

// ---------------------------------------------------------------------------
// checkPrefsPermissions
// ---------------------------------------------------------------------------

void LLMPanel::checkPrefsPermissions() {
    m_prefsWorldReadable = false;

#ifndef _WIN32
    // Build the preferences file path: ~/.ffe/preferences.json
    const char* home = ::getenv("HOME");
    if (!home) {
        const struct passwd* pw = ::getpwuid(::getuid());
        if (pw) { home = pw->pw_dir; }
    }
    if (!home) return;

    std::string prefsPath = std::string(home) + "/.ffe/preferences.json";

    struct stat st{};
    if (::stat(prefsPath.c_str(), &st) != 0) {
        return;  // file does not exist yet — no warning needed
    }
    // If group-readable (0040) or world-readable (0004), set warning flag.
    if ((st.st_mode & 0044) != 0) {
        m_prefsWorldReadable = true;
    }
#else
    // Windows: best-effort — we rely on a visible banner set elsewhere if
    // SetFileSecurityA fails during initial write (see ADR §7.3 note).
    (void)this;
#endif
}

// ---------------------------------------------------------------------------
// loadContextBlock (static — used by doRequest and tests)
// ---------------------------------------------------------------------------

/*static*/
std::string LLMPanel::loadContextBlock(const std::vector<std::string>& paths,
                                       size_t contextBudget) {
    std::string result;
    result.reserve(contextBudget);

    // Prepend the fixed preamble. It counts against the budget.
    result += SYSTEM_PROMPT_PREFIX;

    if (result.size() >= contextBudget) {
        // Preamble alone exceeds budget — truncate at budget and return.
        result.resize(contextBudget);
        return result;
    }

    size_t remaining = contextBudget - result.size();

    for (const auto& rawPath : paths) {
        if (remaining == 0) break;

        // Canonicalize the path to resolve symlinks and '..' components.
        char canonBuf[PATH_MAX] = {};
        if (!ffe::canonicalizePath(rawPath.c_str(), canonBuf, sizeof(canonBuf))) {
            FFE_LOG_DEBUG("LLMPanel",
                "loadContextBlock: skipping path (canonicalize failed): %s",
                rawPath.c_str());
            continue;
        }

        // Determine the project root by canonicalizing the cmake source tree.
        // We derive it from __FILE__ at compile time to avoid a runtime lookup.
        // __FILE__ is .../engine/editor/llm_panel.cpp
        // Project root is four parent directories up from engine/editor/.
        //
        // Security: the project-root prefix check is mandatory. If FFE_PROJECT_ROOT
        // is not defined (e.g. test builds), we derive the root from __FILE__ as a
        // fallback so that the check is never skipped. Failing open (allowing all
        // resolved paths when the root is unknown) would permit path traversal to
        // files outside the project tree such as /etc/passwd.
#ifdef FFE_PROJECT_ROOT
        const char* projectRootStr = FFE_PROJECT_ROOT;
#else
        // Derive the project root from __FILE__:
        //   __FILE__ == .../engine/editor/llm_panel.cpp
        // Walk up four components: llm_panel.cpp -> editor -> engine -> <root>
        static const char* projectRootStr = []() -> const char* {
            static char derivedRoot[PATH_MAX] = {};
            // Start from the source file path baked in at compile time.
            const char* src = __FILE__;
            // Copy into mutable buffer so we can truncate.
            std::strncpy(derivedRoot, src, PATH_MAX - 1);
            // Strip three trailing path components (llm_panel.cpp, editor, engine).
            for (int i = 0; i < 3; ++i) {
                char* sep = std::strrchr(derivedRoot, '/');
                if (!sep) { derivedRoot[0] = '\0'; break; }
                *sep = '\0';
            }
            return derivedRoot;
        }();
#endif
        {
            char rootBuf[PATH_MAX] = {};
            if (!ffe::canonicalizePath(projectRootStr, rootBuf, sizeof(rootBuf))
                    || rootBuf[0] == '\0') {
                // Cannot determine project root — fail closed: skip this path.
                FFE_LOG_DEBUG("LLMPanel",
                    "loadContextBlock: skipping path (cannot determine project root): %s",
                    canonBuf);
                continue;
            }
            // The canonicalized path must begin with the project root.
            if (std::strncmp(canonBuf, rootBuf, std::strlen(rootBuf)) != 0) {
                FFE_LOG_DEBUG("LLMPanel",
                    "loadContextBlock: skipping path outside project root: %s -> %s",
                    canonBuf, rootBuf);
                continue;
            }
        }

        // Open the file.
        std::ifstream file(canonBuf, std::ios::in);
        if (!file.is_open()) {
            FFE_LOG_DEBUG("LLMPanel",
                "loadContextBlock: could not open file: %s", canonBuf);
            continue;
        }

        // Read up to `remaining` characters, truncating at the last complete
        // line that fits within the budget.
        std::string fileContent;
        fileContent.reserve(remaining);

        std::string line;
        while (std::getline(file, line) && remaining > 0) {
            // +1 for the newline character we append.
            const size_t lineWithNl = line.size() + 1;
            if (lineWithNl > remaining) {
                // This line doesn't fit — truncate at line boundary (omit line).
                break;
            }
            fileContent += line;
            fileContent += '\n';
            remaining -= lineWithNl;
        }

        result += fileContent;
    }

    return result;
}

// ---------------------------------------------------------------------------
// assembleSystemContext
// ---------------------------------------------------------------------------

std::string LLMPanel::assembleSystemContext() const {
    return loadContextBlock(m_contextFiles, CONTEXT_BUDGET_CHARS);
}

// ---------------------------------------------------------------------------
// doRequest (static — called only from background thread)
// ---------------------------------------------------------------------------

/*static*/
void LLMPanel::doRequest(const LLMConfig& cfg,
                         const std::string& systemPrompt,
                         const std::string& userPrompt,
                         LLMResponse* out) noexcept {
    // Validate that baseUrl uses https:// per ADR §10 Threat 2.
    if (cfg.baseUrl.rfind("https://", 0) != 0) {
        out->success  = false;
        out->errorMsg = "LLM request rejected: baseUrl must start with https://";
        return;
    }

    if (cfg.apiKey.empty()) {
        out->success  = false;
        out->errorMsg = "LLM not configured: API key is empty";
        return;
    }

    // Build the OpenAI-format JSON payload (§4.4 of the ADR).
    // Uses separate role entries — never concatenates system + user content.
    nlohmann::json payload;
    payload["model"] = cfg.model.empty() ? "gpt-4o" : cfg.model;
    payload["messages"] = nlohmann::json::array({
        {{"role", "system"}, {"content", systemPrompt}},
        {{"role", "user"},   {"content", userPrompt}}
    });
    const std::string body = payload.dump();

    // Strip the scheme prefix to get the host (e.g. "api.openai.com").
    // cpp-httplib's Client constructor accepts the full URL with scheme.
    httplib::Client cli(cfg.baseUrl);
    cli.set_connection_timeout(cfg.timeoutSecs);
    cli.set_read_timeout(cfg.timeoutSecs);
    // TLS certificate verification is always enabled (ADR §10 Threat 2).
    cli.enable_server_certificate_verification(true);

    // Set CA bundle if it is a valid regular file.
    if (validateCaBundle(cfg.caBundle)) {
        cli.set_ca_cert_path(cfg.caBundle.c_str());
    } else if (!cfg.caBundle.empty()) {
        // Invalid CA bundle path — reject rather than fall back to no verify.
        out->success  = false;
        out->errorMsg = "LLM request rejected: caBundle path is invalid or not a regular file";
        return;
    }

    // Authorization header. The API key must never appear in a URL or log.
    httplib::Headers headers = {
        {"Authorization", "Bearer " + cfg.apiKey},
        {"Content-Type",  "application/json"}
    };

    // Enforce maxResponseBytes via ContentReceiver (ADR §10 Threat 4).
    // We accumulate bytes and abort once the cap is hit.
    const size_t cap = cfg.maxResponseBytes;
    std::string responseBody;
    responseBody.reserve(std::min(cap, static_cast<size_t>(4096)));
    bool capExceeded = false;

    auto receiver = [&](const char* data, size_t length) -> bool {
        if (capExceeded) return false;
        if (responseBody.size() + length > cap) {
            // Accumulate only up to the cap, then signal abort.
            const size_t allowed = cap - responseBody.size();
            responseBody.append(data, allowed);
            capExceeded = true;
            return false;  // abort the transfer
        }
        responseBody.append(data, length);
        return true;
    };

    auto res = cli.Post("/v1/chat/completions", headers, body, "application/json", receiver);

    if (capExceeded) {
        out->success  = false;
        out->errorMsg = "Response exceeded size limit";
        return;
    }

    if (!res) {
        // cpp-httplib returns a null result on connection/TLS failure.
        out->success  = false;
        out->errorMsg = scrubApiKey("HTTP request failed", cfg.apiKey);
        return;
    }

    if (res->status != 200) {
        std::string errMsg = "HTTP error " + std::to_string(res->status);
        out->success  = false;
        out->errorMsg = scrubApiKey(errMsg, cfg.apiKey);
        return;
    }

    // Use the accumulated body from the ContentReceiver (it may be empty
    // if the stub is being used). Fall back to res->body for compatibility
    // with the stub (which does not call the receiver).
    const std::string& bodyToparse =
        responseBody.empty() ? res->body : responseBody;

    // Parse the response JSON and extract choices[0].message.content.
    // Pass false as the third argument to disable exceptions; a parse failure
    // returns a discarded value instead of throwing nlohmann::json::parse_error.
    auto json = nlohmann::json::parse(bodyToparse, nullptr, /*throwOnError=*/false);
    if (json.is_discarded()) {
        out->success  = false;
        out->errorMsg = "Failed to parse LLM response JSON";
        return;
    }

    // Extract choices[0].message.content using explicit presence and type checks
    // so that no exceptions are needed. Any missing or wrong-typed field is
    // treated as an unexpected response structure.
    if (!json.contains("choices") || !json["choices"].is_array()
            || json["choices"].empty()) {
        out->success  = false;
        out->errorMsg = "Unexpected response structure: missing or empty choices array";
        return;
    }
    const auto& choice0 = json["choices"][0];
    if (!choice0.is_object() || !choice0.contains("message")
            || !choice0["message"].is_object()) {
        out->success  = false;
        out->errorMsg = "Unexpected response structure: missing message object";
        return;
    }
    const auto& message = choice0["message"];
    if (!message.contains("content") || !message["content"].is_string()) {
        out->success  = false;
        out->errorMsg = "Unexpected response structure: missing or non-string content";
        return;
    }

    out->text     = message["content"].get<std::string>();
    out->success  = true;
    out->errorMsg = {};
}

// ---------------------------------------------------------------------------
// pollResult — called from render() to pick up completed requests
// ---------------------------------------------------------------------------

void LLMPanel::pollResult() {
    // Only act when the pending flag transitions to false.
    if (m_state->pending.load(std::memory_order_acquire)) return;
    if (!m_workerThread.joinable()) return;

    // Join the finished thread before resetting state (safe — it has already
    // cleared pending, so join() is non-blocking here).
    m_workerThread.join();

    // Copy result under lock.
    {
        std::lock_guard<std::mutex> lock(m_state->resultMutex);
        if (m_state->result.success) {
            m_lastResponse     = m_state->result.text;
            m_lastError        = {};
            m_showInsertButton = true;
        } else {
            m_lastResponse     = {};
            m_lastError        = m_state->result.errorMsg;
            m_showInsertButton = false;
        }
        // Reset result for next request.
        m_state->result = LLMResponse{};
    }
}

// ---------------------------------------------------------------------------
// render, renderSettingsSection, renderResponseArea
// Guarded by #ifndef FFE_TEST so that the test binary can compile llm_panel.cpp
// without linking ImGui. Tests exercise the non-UI logic only (doRequest,
// loadContextBlock, scrubApiKey, lifecycle).
// ---------------------------------------------------------------------------

#ifndef FFE_TEST

void LLMPanel::render() {
    // Poll for completed background request before drawing.
    pollResult();

    if (!ImGui::Begin("AI Assistant", &m_visible)) {
        ImGui::End();
        return;
    }

    // World-readable preferences file warning banner.
    if (m_prefsWorldReadable) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.0f, 1.0f));
        ImGui::TextWrapped(
            "Warning: ~/.ffe/preferences.json is world-readable. "
            "Fix: chmod 600 ~/.ffe/preferences.json");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // Status indicator.
    const bool pending = m_state->pending.load(std::memory_order_acquire);
    if (pending) {
        ImGui::TextUnformatted("Status: Waiting for response...");
    } else if (!m_lastError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextUnformatted("Status: Error");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted("Status: Ready");
    }

    ImGui::Separator();

    renderSettingsSection();

    ImGui::Separator();

    // Query input area.
    ImGui::TextUnformatted("Query:");
    ImGui::InputTextMultiline(
        "##llm_query",
        m_inputBuf,
        INPUT_BUF_SIZE,
        ImVec2(-1.0f, 80.0f));

    // "Ask" button — disabled while a request is pending or query is empty.
    const bool queryEmpty = (m_inputBuf[0] == '\0');
    ImGui::BeginDisabled(pending || queryEmpty);
    if (ImGui::Button("Ask")) {
        // Assemble context and fire request on background thread.
        const std::string systemCtx = assembleSystemContext();
        const std::string userQuery = m_inputBuf;

        // Clear input buffer.
        m_inputBuf[0] = '\0';

        // Capture a copy of the config into shared state at submit time.
        // (Config must not be mutated while pending — contract documented in header.)
        // m_state->config was already set by setConfig(); no copy needed here.

        // Join any previously finished thread that pollResult() hasn't cleaned up
        // (edge case: render() was not called between two requests).
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }

        m_lastResponse     = {};
        m_lastError        = {};
        m_showInsertButton = false;

        // Set pending before spawning the thread so the render thread sees it
        // immediately on the next frame.
        m_state->pending.store(true, std::memory_order_release);

        // Spawn the background thread. It captures a copy of m_state (the
        // shared_ptr) so LLMSharedState stays alive even if LLMPanel is
        // destroyed before the thread exits (ADR §7.3).
        std::shared_ptr<LLMSharedState> stateCapture = m_state;
        std::string systemCopy = systemCtx;
        std::string userCopy   = userQuery;

        m_workerThread = std::thread([stateCapture = std::move(stateCapture),
                                      systemCopy   = std::move(systemCopy),
                                      userCopy     = std::move(userCopy)]() mutable {
            LLMResponse response;
            doRequest(stateCapture->config, systemCopy, userCopy, &response);

            {
                std::lock_guard<std::mutex> lock(stateCapture->resultMutex);
                stateCapture->result = std::move(response);
            }
            // Release pending AFTER writing result — sequential consistency
            // of std::atomic guarantees the render thread sees the write.
            stateCapture->pending.store(false, std::memory_order_release);
        });
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    renderResponseArea();

    // "Insert Snippet" button — only shown when there is a valid response
    // and a script slot is registered.
    if (m_showInsertButton && m_scriptSlot && !m_lastResponse.empty()) {
        if (ImGui::Button("Insert Snippet")) {
            m_scriptSlot->appendText(m_lastResponse);
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// renderSettingsSection
// ---------------------------------------------------------------------------

void LLMPanel::renderSettingsSection() {
    if (!ImGui::CollapsingHeader("Settings")) return;

    // Base URL (read-only display — editable via ~/.ffe/preferences.json).
    ImGui::TextUnformatted("Base URL:");
    ImGui::SameLine();
    const std::string& url = m_state->config.baseUrl;
    ImGui::TextUnformatted(url.empty() ? "(not set)" : url.c_str());

    // API key — never displayed in plain text; show masked indicator.
    ImGui::TextUnformatted("API Key:");
    ImGui::SameLine();
    if (m_state->config.apiKey.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextUnformatted("(not set — edit ~/.ffe/preferences.json)");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted("******* (configured)");
    }

    // Model.
    ImGui::TextUnformatted("Model:");
    ImGui::SameLine();
    const std::string& model = m_state->config.model;
    ImGui::TextUnformatted(model.empty() ? "gpt-4o" : model.c_str());

    ImGui::TextUnformatted("Edit settings in ~/.ffe/preferences.json");
}

// ---------------------------------------------------------------------------
// renderResponseArea
// ---------------------------------------------------------------------------

void LLMPanel::renderResponseArea() {
    ImGui::TextUnformatted("Response:");

    // Show error in red.
    if (!m_lastError.empty()) {
        // IMPORTANT: m_lastError must never contain the API key (scrubbed in doRequest).
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", m_lastError.c_str());
        ImGui::PopStyleColor();
        return;
    }

    if (m_lastResponse.empty()) {
        ImGui::TextDisabled("(no response yet)");
        return;
    }

    // Scrollable child region for the response text.
    ImGui::BeginChild("##llm_response", ImVec2(-1.0f, 200.0f), true);
    ImGui::TextWrapped("%s", m_lastResponse.c_str());
    ImGui::EndChild();
}

#endif // !FFE_TEST

} // namespace ffe::editor

#endif // FFE_EDITOR
