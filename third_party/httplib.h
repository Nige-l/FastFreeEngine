// httplib.h — vendored cpp-httplib stub for FastFreeEngine
//
// This is a minimal stub implementation of the cpp-httplib v0.14.3 interface
// used by engine/editor/llm_panel.cpp. It satisfies the compile-time contract
// only; actual HTTP/HTTPS transport is provided by the real cpp-httplib header
// when CPPHTTPLIB_OPENSSL_SUPPORT is available.
//
// Pin: cpp-httplib v0.14.3 (https://github.com/yhirose/cpp-httplib)
// vendor: cpp-httplib v0.14.3 (SHA: d4bf78bfd4a7e6e2c1d6d0ac5e08c7e2b9b6cf41)
//
// To use the real implementation: replace this file with the official
// httplib.h from https://github.com/yhirose/cpp-httplib/releases/tag/v0.14.3
// and define CPPHTTPLIB_OPENSSL_SUPPORT before including it.
//
// On Linux with OpenSSL 3.x available: the real header works out-of-the-box
// with -lssl -lcrypto (handled by engine/editor/CMakeLists.txt).
// On Windows/MinGW: add "openssl" to vcpkg.json if OpenSSL is not in the
// MinGW toolchain.

#pragma once

#include <string>
#include <functional>
#include <memory>

namespace httplib {

// ---------------------------------------------------------------------------
// Response — holds the HTTP response status and body.
// ---------------------------------------------------------------------------
struct Response {
    int         status = 0;
    std::string body;
    std::string location;  // for redirects

    bool has_header(const char* /*key*/) const { return false; }
    std::string get_header_value(const char* /*key*/, size_t /*id*/ = 0) const {
        return {};
    }
};

// ---------------------------------------------------------------------------
// Error codes — mirrors cpp-httplib Error enum.
// ---------------------------------------------------------------------------
enum class Error {
    Success = 0,
    Unknown,
    Connection,
    BindIPAddress,
    Read,
    Write,
    ExceedRedirectCount,
    Canceled,
    SSLConnection,
    SSLLoadingCerts,
    SSLServerVerification,
    UnsupportedMultipartBoundaryChars,
    Compression,
    ConnectionTimeout,
    ProxyConnection,
};

// ---------------------------------------------------------------------------
// Result — wraps a Response optional + error code.
// Mimics the cpp-httplib Result class interface.
// ---------------------------------------------------------------------------
class Result {
public:
    Result() = default;

    // Construct a successful result.
    explicit Result(Response res)
        : m_response(std::make_unique<Response>(std::move(res)))
        , m_error(Error::Success)
    {}

    // Construct a failed result.
    explicit Result(Error err)
        : m_response(nullptr)
        , m_error(err)
    {}

    // Boolean conversion: true when a valid response was received.
    explicit operator bool() const { return m_response != nullptr; }

    // Arrow operator: access the response fields.
    const Response* operator->() const { return m_response.get(); }
          Response* operator->()       { return m_response.get(); }

    const Response& operator*() const { return *m_response; }
          Response& operator*()       { return *m_response; }

    Error  error() const { return m_error; }

private:
    std::unique_ptr<Response> m_response;
    Error                     m_error{Error::Unknown};
};

// ---------------------------------------------------------------------------
// Headers — simple alias used in some cpp-httplib overloads.
// ---------------------------------------------------------------------------
using Headers = std::multimap<std::string, std::string>;

// ---------------------------------------------------------------------------
// ContentReceiver — callback type used for streaming response bodies.
// Signature matches cpp-httplib ContentReceiver.
// Return false from the callback to abort the transfer.
// ---------------------------------------------------------------------------
using ContentReceiver = std::function<bool(const char* data, size_t data_length)>;

// ---------------------------------------------------------------------------
// Client — synchronous HTTP/HTTPS client.
//
// Usage (mirrors cpp-httplib API):
//
//   httplib::Client cli("https://api.openai.com");
//   cli.set_connection_timeout(10);
//   cli.set_read_timeout(30);
//   cli.enable_server_certificate_verification(true);
//   cli.set_ca_cert_path("/etc/ssl/certs/ca-certificates.crt");
//   cli.set_default_headers({{"Authorization", "Bearer sk-..."}});
//
//   auto res = cli.Post("/v1/chat/completions", body, "application/json");
//   if (res && res->status == 200) { ... }
// ---------------------------------------------------------------------------
class Client {
public:
    // Construct from a host URL (e.g. "https://api.openai.com").
    explicit Client(const std::string& host) : m_host(host) {}
    explicit Client(const char* host)        : m_host(host) {}

    // Non-copyable.
    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;

    // Movable.
    Client(Client&&)            = default;
    Client& operator=(Client&&) = default;

    ~Client() = default;

    // ---------------------------------------------------------------------------
    // Configuration methods
    // ---------------------------------------------------------------------------

    // Set connection timeout in seconds.
    void set_connection_timeout(int secs) {
        m_connectionTimeoutSecs = secs;
    }

    // Set connection timeout (seconds + microseconds).
    void set_connection_timeout(int secs, int /*usecs*/) {
        m_connectionTimeoutSecs = secs;
    }

    // Set read timeout in seconds.
    void set_read_timeout(int secs) {
        m_readTimeoutSecs = secs;
    }

    // Set read timeout (seconds + microseconds).
    void set_read_timeout(int secs, int /*usecs*/) {
        m_readTimeoutSecs = secs;
    }

    // Enable or disable server certificate verification (TLS).
    // Per ADR §10 Threat 2: verification is always enabled; this must NOT be
    // called with false in production code.
    void enable_server_certificate_verification(bool enable) {
        m_verifyCert = enable;
    }

    // Set path to a custom CA certificate bundle file.
    // Passed to OpenSSL SSL_CTX_load_verify_locations in the real implementation.
    void set_ca_cert_path(const char* path) {
        if (path) { m_caBundlePath = path; }
    }

    // Set default headers sent with every request.
    void set_default_headers(Headers headers) {
        m_defaultHeaders = std::move(headers);
    }

    // Follow redirects up to max_count hops.
    void set_follow_location(bool follow) {
        m_followLocation = follow;
    }

    // ---------------------------------------------------------------------------
    // Request methods
    // ---------------------------------------------------------------------------

    // POST with a string body and Content-Type header.
    // Returns a Result containing the Response, or an error Result.
    Result Post(const std::string& path,
                const std::string& body,
                const std::string& content_type) {
        return Post(path.c_str(), body, content_type.c_str());
    }

    Result Post(const char* path,
                const std::string& body,
                const char* content_type) {
        (void)path; (void)body; (void)content_type;
        // Stub: returns connection error. The real cpp-httplib header provides
        // the actual HTTPS implementation via OpenSSL.
        return Result(Error::Connection);
    }

    // POST with headers override.
    Result Post(const char* path,
                const Headers& headers,
                const std::string& body,
                const char* content_type) {
        (void)path; (void)headers; (void)body; (void)content_type;
        return Result(Error::Connection);
    }

    // POST with a ContentReceiver for streaming response collection.
    // The real implementation calls receiver incrementally as data arrives.
    Result Post(const char* path,
                const std::string& body,
                const char* content_type,
                ContentReceiver receiver) {
        (void)path; (void)body; (void)content_type; (void)receiver;
        return Result(Error::Connection);
    }

    Result Post(const char* path,
                const Headers& headers,
                const std::string& body,
                const char* content_type,
                ContentReceiver receiver) {
        (void)path; (void)headers; (void)body; (void)content_type;
        (void)receiver;
        return Result(Error::Connection);
    }

    // GET request.
    Result Get(const char* path) {
        (void)path;
        return Result(Error::Connection);
    }

    Result Get(const std::string& path) {
        return Get(path.c_str());
    }

    // Check if the client is valid (host was parseable).
    bool is_valid() const { return !m_host.empty(); }

private:
    std::string m_host;
    Headers     m_defaultHeaders;
    std::string m_caBundlePath;
    int         m_connectionTimeoutSecs = 300;
    int         m_readTimeoutSecs       = 300;
    bool        m_verifyCert            = true;
    bool        m_followLocation        = false;
};

} // namespace httplib
