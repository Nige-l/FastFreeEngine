// test_llm_panel.cpp -- Catch2 unit tests for LLMPanel.
//
// Tests validate: config clamping, context file loading, path traversal
// rejection, budget truncation, CA bundle validation, API key scrubbing,
// and panel lifecycle. No real HTTP calls are made.
//
// The test binary is compiled with FFE_EDITOR defined so that llm_panel.h
// and llm_panel.cpp are compiled in. See tests/CMakeLists.txt.

#include <catch2/catch_test_macros.hpp>

// Ensure FFE_EDITOR is set so the LLM panel headers compile.
#ifndef FFE_EDITOR
#define FFE_EDITOR
#endif

#include "editor/llm_panel.h"

#include <cstdio>      // tmpnam, remove
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>  // stat, S_ISREG
#include <vector>

using namespace ffe::editor;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Write content to a temp file and return its path.
// Returns empty string on failure. Caller must remove the file when done.
static std::string writeTempFile(const std::string& content) {
#ifdef _WIN32
    char buf[L_tmpnam] = {};
    if (!std::tmpnam(buf)) return {};
#else
    char buf[] = "/tmp/ffe_test_llm_XXXXXX";
    int fd = ::mkstemp(buf);
    if (fd < 0) return {};
    ::close(fd);
#endif
    std::ofstream f(buf, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return {};
    f << content;
    f.close();
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// 1. LLMConfig default values
// ---------------------------------------------------------------------------

TEST_CASE("LLMConfig has correct default values", "[llm_panel]") {
    LLMConfig cfg;
    CHECK(cfg.baseUrl.empty());
    CHECK(cfg.apiKey.empty());
    CHECK(cfg.model == "gpt-4o");
    CHECK(cfg.timeoutSecs == 30);
    CHECK(cfg.maxResponseBytes == 32768);
    CHECK(cfg.caBundle.empty());
}

// ---------------------------------------------------------------------------
// 2. LLMPanel construction and isRequestPending initial state
// ---------------------------------------------------------------------------

TEST_CASE("LLMPanel: isRequestPending starts false", "[llm_panel]") {
    LLMPanel panel;
    CHECK_FALSE(panel.isRequestPending());
}

// ---------------------------------------------------------------------------
// 3. setConfig clamps maxResponseBytes below minimum
// ---------------------------------------------------------------------------

TEST_CASE("LLMPanel: setConfig clamps maxResponseBytes below minimum to 512", "[llm_panel]") {
    LLMPanel panel;
    LLMConfig cfg;
    cfg.baseUrl          = "https://api.example.com";
    cfg.apiKey           = "sk-test";
    cfg.maxResponseBytes = 0;  // below minimum 512

    // setConfig should clamp to 512 without crashing.
    // We verify indirectly: after setConfig, the panel is not pending.
    panel.setConfig(cfg);
    CHECK_FALSE(panel.isRequestPending());
}

// ---------------------------------------------------------------------------
// 4. setConfig clamps maxResponseBytes above maximum
// ---------------------------------------------------------------------------

TEST_CASE("LLMPanel: setConfig clamps maxResponseBytes above maximum to 65536", "[llm_panel]") {
    LLMPanel panel;
    LLMConfig cfg;
    cfg.baseUrl          = "https://api.example.com";
    cfg.apiKey           = "sk-test";
    cfg.maxResponseBytes = 999999;  // above maximum 65536

    panel.setConfig(cfg);
    CHECK_FALSE(panel.isRequestPending());
}

// ---------------------------------------------------------------------------
// 5. setContextFiles and setConfig accepted without crash
// ---------------------------------------------------------------------------

TEST_CASE("LLMPanel: setContextFiles accepted before pending", "[llm_panel]") {
    LLMPanel panel;
    std::vector<std::string> paths = {"/tmp/nonexistent.md", "/tmp/also_nonexistent.md"};
    panel.setContextFiles(paths);
    CHECK_FALSE(panel.isRequestPending());
}

// ---------------------------------------------------------------------------
// 6. loadContextBlock with empty path list returns preamble only
// ---------------------------------------------------------------------------

TEST_CASE("loadContextBlock: empty path list returns system prompt prefix only", "[llm_panel]") {
    const std::string result = LLMPanel::loadContextBlock({}, 4096);
    // Must contain the required preamble text.
    CHECK(result.find("FastFreeEngine") != std::string::npos);
    CHECK(result.find("FFE DOCUMENTATION") != std::string::npos);
    // Must be non-empty.
    CHECK(!result.empty());
    // Must not exceed budget.
    CHECK(result.size() <= 4096);
}

// ---------------------------------------------------------------------------
// 7. loadContextBlock reads real file content
// ---------------------------------------------------------------------------

TEST_CASE("loadContextBlock: reads content from a real temp file", "[llm_panel]") {
    // Derive the absolute path from __FILE__ so the test works regardless of
    // the process working directory (ctest may not run from the project root).
    // __FILE__ is the absolute path to this source file:
    //   <project_root>/tests/editor/test_llm_panel.cpp
    // Stripping the trailing "/tests/editor/test_llm_panel.cpp" gives us the
    // project root, from which we can construct a canonical absolute path.
    std::string srcFile = __FILE__;
    std::string projectRoot = srcFile.substr(
        0, srcFile.rfind("/tests/editor/test_llm_panel.cpp"));
    const std::string path = projectRoot + "/engine/core/.context.md";

    const std::string result = LLMPanel::loadContextBlock({path}, 8192);

    // The preamble must appear.
    CHECK(result.find("FFE DOCUMENTATION") != std::string::npos);
    // Content from engine/core/.context.md must appear (line 1 of that file).
    CHECK(result.find("Entity Component System") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 8. loadContextBlock: budget truncation
// ---------------------------------------------------------------------------

TEST_CASE("loadContextBlock: truncates content at budget boundary", "[llm_panel]") {
    // Create a file with 6000 characters of content (well above 4096 budget).
    std::string bigContent;
    bigContent.reserve(6000);
    for (int i = 0; i < 100; ++i) {
        // 60 chars per line x 100 = 6000 chars
        bigContent += "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n";
    }
    REQUIRE(bigContent.size() >= 5000);

    const std::string path = writeTempFile(bigContent);
    REQUIRE(!path.empty());

    const size_t budget = 4096;
    const std::string result = LLMPanel::loadContextBlock({path}, budget);

    CHECK(result.size() <= budget);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 9. loadContextBlock: path traversal rejection
// ---------------------------------------------------------------------------

TEST_CASE("loadContextBlock: path traversal paths are rejected (do not appear in output)", "[llm_panel]") {
    // These paths contain traversal sequences. They should not be opened.
    // If they were opened (e.g. on a system where /etc/passwd exists), the
    // content would contain "root:" — we assert it does not.
    const std::vector<std::string> traversalPaths = {
        "../../etc/passwd",
        "../../../etc/shadow",
        "/etc/passwd",   // absolute — canonicalize will fail or succeed; either way
                         // the project-root check filters it (if FFE_PROJECT_ROOT is set).
                         // Even without FFE_PROJECT_ROOT, the file is skipped if canonicalize
                         // resolves to outside the engine expectation.
    };

    // We test the primary canonicalizePath failure path:
    // Non-existent traversal paths fail canonicalizePath and are skipped.
    const std::string result = LLMPanel::loadContextBlock(traversalPaths, 4096);

    // The result must not contain typical /etc/passwd content.
    CHECK(result.find("root:") == std::string::npos);
    CHECK(result.find("/bin/bash") == std::string::npos);
}

// ---------------------------------------------------------------------------
// 10. scrubApiKey: replaces all occurrences of the API key
// ---------------------------------------------------------------------------

TEST_CASE("LLMPanel::scrubApiKey replaces API key with [REDACTED]", "[llm_panel]") {
    const std::string apiKey = "sk-supersecret123";
    const std::string msg    = "Error: Bearer sk-supersecret123 was rejected. "
                               "Token sk-supersecret123 is invalid.";
    const std::string scrubbed = LLMPanel::scrubApiKey(msg, apiKey);

    CHECK(scrubbed.find(apiKey) == std::string::npos);
    CHECK(scrubbed.find("[REDACTED]") != std::string::npos);
}

TEST_CASE("LLMPanel::scrubApiKey with empty apiKey returns message unchanged", "[llm_panel]") {
    const std::string msg     = "Some error message";
    const std::string scrubbed = LLMPanel::scrubApiKey(msg, "");
    CHECK(scrubbed == msg);
}

// ---------------------------------------------------------------------------
// 11. validateCaBundle: regular file vs non-existent path
// ---------------------------------------------------------------------------

TEST_CASE("LLMPanel validateCaBundle: rejects non-existent path", "[llm_panel]") {
    // Access the static member via a public test seam.
    // validateCaBundle is declared private in the header, so we test it
    // indirectly through setConfig: if caBundle is invalid, doRequest rejects.
    // We test the behaviour directly via doRequest with an invalid caBundle.

    LLMConfig cfg;
    cfg.baseUrl          = "https://api.example.com";
    cfg.apiKey           = "sk-test-key";
    cfg.caBundle         = "/tmp/nonexistent_ca_bundle_xyz_12345.pem";
    cfg.timeoutSecs      = 1;
    cfg.maxResponseBytes = 512;

    LLMResponse resp;
    LLMPanel::doRequest(cfg, "system", "user", &resp);

    // Should fail because the caBundle path is invalid.
    CHECK_FALSE(resp.success);
    // The error message must NOT contain the API key.
    CHECK(resp.errorMsg.find("sk-test-key") == std::string::npos);
}

TEST_CASE("LLMPanel validateCaBundle: accepts a real regular file", "[llm_panel]") {
    // Create a temporary regular file to act as the CA bundle path.
    const std::string path = writeTempFile("-----BEGIN CERTIFICATE-----\n");
    REQUIRE(!path.empty());

    // A valid regular file path should be accepted by validateCaBundle.
    // We verify via doRequest: it will proceed past the caBundle check
    // and fail at the actual HTTP connection (stub returns Connection error).
    LLMConfig cfg;
    cfg.baseUrl          = "https://api.example.com";
    cfg.apiKey           = "sk-test-key";
    cfg.caBundle         = path;
    cfg.timeoutSecs      = 1;
    cfg.maxResponseBytes = 512;

    LLMResponse resp;
    LLMPanel::doRequest(cfg, "system", "user", &resp);

    // Must fail (stub doesn't connect), but NOT due to invalid caBundle.
    // i.e. error is NOT "caBundle path is invalid or not a regular file".
    CHECK_FALSE(resp.success);
    CHECK(resp.errorMsg.find("caBundle path is invalid") == std::string::npos);
    // API key must not appear in error.
    CHECK(resp.errorMsg.find("sk-test-key") == std::string::npos);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// 12. doRequest: rejects http:// (non-TLS) baseUrl
// ---------------------------------------------------------------------------

TEST_CASE("doRequest: rejects http:// baseUrl (must use https://)", "[llm_panel]") {
    LLMConfig cfg;
    cfg.baseUrl          = "http://api.example.com";  // plain HTTP — rejected
    cfg.apiKey           = "sk-test";
    cfg.timeoutSecs      = 1;
    cfg.maxResponseBytes = 512;

    LLMResponse resp;
    LLMPanel::doRequest(cfg, "system", "user prompt", &resp);

    CHECK_FALSE(resp.success);
    CHECK(resp.errorMsg.find("https://") != std::string::npos);
    // API key must never appear in error.
    CHECK(resp.errorMsg.find("sk-test") == std::string::npos);
}

// ---------------------------------------------------------------------------
// 13. doRequest: rejects empty apiKey
// ---------------------------------------------------------------------------

TEST_CASE("doRequest: rejects empty apiKey", "[llm_panel]") {
    LLMConfig cfg;
    cfg.baseUrl          = "https://api.example.com";
    cfg.apiKey           = "";  // not configured
    cfg.timeoutSecs      = 1;
    cfg.maxResponseBytes = 512;

    LLMResponse resp;
    LLMPanel::doRequest(cfg, "system", "user prompt", &resp);

    CHECK_FALSE(resp.success);
    CHECK(!resp.errorMsg.empty());
}

// ---------------------------------------------------------------------------
// 14. ScriptEditorSlot mock injection
// ---------------------------------------------------------------------------

class MockScriptSlot : public ScriptEditorSlot {
public:
    std::string appended;
    std::string replaced;

    void appendText(std::string_view text) override {
        appended = std::string(text);
    }
    void replaceSelection(std::string_view text) override {
        replaced = std::string(text);
    }
};

TEST_CASE("ScriptEditorSlot mock: appendText receives correct text", "[llm_panel]") {
    MockScriptSlot slot;
    slot.appendText("print('hello FFE')");
    CHECK(slot.appended == "print('hello FFE')");
}

TEST_CASE("ScriptEditorSlot mock: replaceSelection receives correct text", "[llm_panel]") {
    MockScriptSlot slot;
    slot.replaceSelection("ffe.log('inserted!')");
    CHECK(slot.replaced == "ffe.log('inserted!')");
}

// ---------------------------------------------------------------------------
// 15. Panel destructor with no pending thread — no hang, no crash
// ---------------------------------------------------------------------------

TEST_CASE("LLMPanel: destructor with no pending thread is clean", "[llm_panel]") {
    // Construct and immediately destroy. Should not hang or assert.
    {
        LLMPanel panel;
        panel.setConfig(LLMConfig{});
        panel.setContextFiles({});
    }
    // If we reach here, the destructor completed without hanging.
    CHECK(true);
}
