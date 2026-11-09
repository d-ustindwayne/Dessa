// $("request") -- native part: one HTTP request, done by the `curl` program
// (shipped with Windows 10+, macOS and Linux). The friendly API (get, post,
// Response, ...) is written in Dessa in request.dm.
#pragma once
#include "dessa_runtime.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#ifndef _WIN32
#include <stdlib.h>
#endif

namespace dessa::request {

inline Str read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// One setting of curl's config file. Everything a program gives us goes here
// and never on a command line, so no shell ever reads it.
inline Str curl_setting(const Str& name, const Str& value) {
    Str out = name + " = \"";
    for (char c : value) {
        if (c == '\n' || c == '\r' || c == '\0') continue;
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"\n";
}

// A new folder only this user can read: the request's headers pass through it
inline std::filesystem::path private_folder() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
#ifdef _WIN32
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const fs::path dir = temp / ("dessa-request-" + std::to_string(random()) + std::to_string(random()));
        if (fs::create_directory(dir, ec)) return dir;
    }
#else
    Str pattern = (temp / "dessa-request-XXXXXX").string();
    if (mkdtemp(pattern.data())) return pattern;
#endif
    fail("request: could not make a temporary folder in " + temp.string());
}

// Sends one request. `headers` is "Name: value" lines.
// Returns [status code, response headers, response body].
inline List<Str> _send(const Str& method, const Str& url, const Str& headers, const Str& body, Int timeoutSeconds) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = private_folder();
    const fs::path bodyIn = dir / "in";
    const fs::path headersOut = dir / "headers";
    const fs::path bodyOut = dir / "body";
    const fs::path errors = dir / "err";
    const fs::path settings = dir / "curl.cfg";

    Str config = "silent\nshow-error\nlocation\nmax-time = " + std::to_string(timeoutSeconds > 0 ? timeoutSeconds : 30) +
                 // Only the web, also after a redirect: never file:// and the like
                 "\nproto = \"=http,https\"\nproto-redir = \"=http,https\"\nwrite-out = \"%{http_code}\"\n" +
                 curl_setting("request", method) +
                 curl_setting("dump-header", headersOut.string()) +
                 curl_setting("output", bodyOut.string());
    size_t start = 0;
    while (start < headers.size()) {
        size_t end = headers.find('\n', start);
        Str line = headers.substr(start, end == Str::npos ? Str::npos : end - start);
        if (!line.empty()) config += curl_setting("header", line);
        if (end == Str::npos) break;
        start = end + 1;
    }
    if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
        std::ofstream(bodyIn, std::ios::binary) << body;
        config += curl_setting("data-binary", "@" + bodyIn.string());
    }
    config += curl_setting("url", url);
    {
        std::ofstream out(settings, std::ios::binary);
        out << config;
    }
    // Only our own temporary paths are on the command line
    const Str command = "curl -K \"" + settings.string() + "\" 2> \"" + errors.string() + "\"";

#ifdef _WIN32
    FILE* pipe = _popen(("\"" + command + "\"").c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        fs::remove_all(dir, ec);
        fail("request: could not start curl (is it installed?)");
    }
    Str status;
    char buffer[256];
    while (std::fgets(buffer, sizeof buffer, pipe)) status += buffer;
#ifdef _WIN32
    int code = _pclose(pipe);
#else
    int code = pclose(pipe);
#endif

    Str responseHeaders = read_all(headersOut);
    Str responseBody = read_all(bodyOut);
    Str errorText = read_all(errors);
    fs::remove_all(dir, ec);

    if (code != 0) {
        Str message = errorText;
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
        if (message.rfind("curl: ", 0) == 0) message = message.substr(6);
        if (message.empty()) message = "curl exited with code " + std::to_string(code);
        fail("Request to " + url + " failed: " + message);
    }

    // With redirects (-L) there is one header block per response; keep the last.
    size_t last = responseHeaders.rfind("\r\n\r\nHTTP/");
    if (last != Str::npos) responseHeaders = responseHeaders.substr(last + 4);

    return {status, responseHeaders, responseBody};
}

} // namespace dessa::request
