// $(<request>) -- native part: one HTTP request, done by the `curl` program
// (shipped with Windows 10+, macOS and Linux). The friendly API (get, post,
// Response, ...) is written in Dessa in request.dm.
#pragma once
#include "dessa_runtime.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

namespace dessa::request {

inline Str read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// One command-line argument, quoted for the shell.
inline Str shell_quote(const Str& value) {
    Str out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n' || c == '\r') continue; // never let a value break the command
        out += c;
    }
    return out + "\"";
}

// Sends one request. `headers` is "Name: value" lines.
// Returns [status code, response headers, response body].
inline List<Str> _send(const Str& method, const Str& url, const Str& headers, const Str& body, Int timeoutSeconds) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "dessa-request";
    fs::create_directories(dir, ec);
    std::random_device random;
    const Str id = std::to_string(random()) + std::to_string(random());
    const fs::path bodyIn = dir / (id + ".in");
    const fs::path headersOut = dir / (id + ".headers");
    const fs::path bodyOut = dir / (id + ".body");
    const fs::path errors = dir / (id + ".err");

    Str command = "curl -sS -L --max-time " + std::to_string(timeoutSeconds > 0 ? timeoutSeconds : 30) +
                  " -X " + shell_quote(method) +
                  " -D " + shell_quote(headersOut.string()) +
                  " -o " + shell_quote(bodyOut.string()) +
                  " -w \"%{http_code}\"";
    size_t start = 0;
    while (start < headers.size()) {
        size_t end = headers.find('\n', start);
        Str line = headers.substr(start, end == Str::npos ? Str::npos : end - start);
        if (!line.empty()) command += " -H " + shell_quote(line);
        if (end == Str::npos) break;
        start = end + 1;
    }
    if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
        std::ofstream(bodyIn, std::ios::binary) << body;
        command += " --data-binary " + shell_quote("@" + bodyIn.string());
    }
    command += " " + shell_quote(url) + " 2> " + shell_quote(errors.string());

#ifdef _WIN32
    FILE* pipe = _popen(("\"" + command + "\"").c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) fail("request: could not start curl (is it installed?)");
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
    for (const auto& file : {bodyIn, headersOut, bodyOut, errors}) fs::remove(file, ec);

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
