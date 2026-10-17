#include "web.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define DESSA_CLOSE closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define DESSA_CLOSE close
#endif

namespace fs = std::filesystem;

namespace dessa::web {

namespace {

std::string readFile(const fs::path& p, bool& ok) {
    std::ifstream in(p, std::ios::binary);
    ok = static_cast<bool>(in);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The value of attribute `name` in the attribute text of a tag ("" if absent;
// `present` tells whether it was there at all).
std::string attribute(const std::string& attrs, const std::string& name, bool& present) {
    present = false;
    std::string low = lower(attrs);
    size_t at = 0;
    while ((at = low.find(name, at)) != std::string::npos) {
        bool startOk = at == 0 || std::isspace(static_cast<unsigned char>(low[at - 1]));
        size_t after = at + name.size();
        while (after < low.size() && std::isspace(static_cast<unsigned char>(low[after]))) after++;
        if (!startOk || after >= low.size() || low[after] != '=') {
            at += name.size();
            continue;
        }
        present = true;
        after++;
        while (after < low.size() && std::isspace(static_cast<unsigned char>(low[after]))) after++;
        if (after < attrs.size() && (attrs[after] == '"' || attrs[after] == '\'')) {
            char q = attrs[after];
            size_t end = attrs.find(q, after + 1);
            return attrs.substr(after + 1, end == std::string::npos ? std::string::npos : end - after - 1);
        }
        size_t end = after;
        while (end < attrs.size() && !std::isspace(static_cast<unsigned char>(attrs[end])) && attrs[end] != '>') end++;
        return attrs.substr(after, end - after);
    }
    return "";
}

bool isDessaType(const std::string& type) {
    std::string t = lower(type);
    return t == "text/dessa" || t == "text/ds" || t == "application/dessa" || t == "text/x-dessa";
}

bool isDessaFile(const std::string& src) {
    std::string ext = lower(fs::path(src).extension().string());
    static const char* const exts[] = {".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss"};
    return std::any_of(std::begin(exts), std::end(exts), [&](const char* e) { return ext == e; });
}

// JavaScript inside <script>...</script>: "</script" would end the tag early
std::string safeForScript(std::string js) {
    size_t at = 0;
    while ((at = lower(js).find("</script", at)) != std::string::npos) {
        js.replace(at, 2, "<\\/");
        at += 3;
    }
    return js;
}

} // namespace

bool isDessaPage(const std::string& path) {
    return lower(fs::path(path).extension().string()) == ".dhtml";
}

bool isPage(const std::string& path) {
    std::string ext = lower(fs::path(path).extension().string());
    return ext == ".dhtml" || ext == ".html" || ext == ".htm";
}

std::vector<Script> scripts(const std::string& path, const std::string& page) {
    std::vector<Script> found;
    const bool dhtml = isDessaPage(path);
    const std::string low = lower(page);
    size_t pos = 0;
    while (true) {
        Script s;
        s.open = low.find("<script", pos);
        if (s.open == std::string::npos) break;
        // Inside an HTML comment? Skip past it.
        size_t comment = low.find("<!--", pos);
        if (comment != std::string::npos && comment < s.open) {
            size_t commentEnd = low.find("-->", comment + 4);
            if (commentEnd == std::string::npos) break;
            pos = commentEnd + 3;
            continue;
        }
        s.tagEnd = page.find('>', s.open);
        if (s.tagEnd == std::string::npos) break;
        s.close = low.find("</script", s.tagEnd);
        if (s.close == std::string::npos) break;
        size_t end = page.find('>', s.close);
        s.closeEnd = end == std::string::npos ? page.size() : end + 1;
        const std::string attrs = page.substr(s.open + 7, s.tagEnd - s.open - 7);
        bool hasType = false;
        const std::string type = attribute(attrs, "type", hasType);
        s.src = attribute(attrs, "src", s.hasSrc);
        s.dessa = isDessaType(type) || (dhtml && !hasType && (!s.hasSrc || isDessaFile(s.src)));
        found.push_back(s);
        pos = s.closeEnd;
    }
    return found;
}

size_t offsetOf(const std::string& text, int line, int column) {
    size_t at = 0;
    for (int l = 1; l < line && at < text.size(); ++l) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) return text.size();
        at = nl + 1;
    }
    return std::min(text.size(), at + static_cast<size_t>(std::max(0, column - 1)));
}

std::string dessaOnly(const std::string& path, const std::string& page, size_t only) {
    std::string out(page.size(), ' ');
    for (size_t i = 0; i < page.size(); ++i) {
        if (page[i] == '\n' || page[i] == '\r') out[i] = page[i];
    }
    for (const auto& s : scripts(path, page)) {
        if (!s.dessa || s.hasSrc) continue;
        if (only != std::string::npos && (only <= s.tagEnd || only > s.close)) continue;
        for (size_t i = s.tagEnd + 1; i < s.close; ++i) out[i] = page[i];
    }
    return out;
}

bool buildPage(const std::string& path, const CompileFn& compile, std::string& html, std::string& errors) {
    bool ok = false;
    const std::string page = readFile(path, ok);
    if (!ok) {
        errors = "Could not open " + path + "\n";
        return false;
    }
    const fs::path folder = fs::path(path).parent_path();
    html.clear();
    size_t pos = 0;
    bool allOk = true;
    for (const auto& s : scripts(path, page)) {
        const size_t open = s.open, tagEnd = s.tagEnd, close = s.close, closeEnd = s.closeEnd;
        const bool hasSrc = s.hasSrc;
        const std::string& src = s.src;
        html += page.substr(pos, open - pos);
        if (!s.dessa) {
            html += page.substr(open, closeEnd - open);
            pos = closeEnd;
            continue;
        }

        std::string js, error;
        bool compiled;
        if (hasSrc) {
            fs::path file = folder / src;
            bool read = false;
            std::string source = readFile(file, read);
            if (!read) {
                error = path + ": can't find the script " + src + "\n";
                compiled = false;
            } else {
                compiled = compile(file.string(), source, js, error);
            }
        } else {
            // Keep the script where it is in the page: same line and column numbers in messages
            const std::string content = page.substr(tagEnd + 1, close - tagEnd - 1);
            size_t line = static_cast<size_t>(std::count(page.begin(), page.begin() + static_cast<long>(tagEnd + 1), '\n'));
            size_t lineStart = page.rfind('\n', tagEnd);
            size_t column = tagEnd + 1 - (lineStart == std::string::npos ? 0 : lineStart + 1);
            std::string padded = std::string(line, '\n') + std::string(column, ' ') + content;
            compiled = compile(path, padded, js, error);
        }
        if (!compiled) {
            errors += error;
            allOk = false;
        }
        html += "<script>\n" + safeForScript(js) + "\n</script>";
        pos = closeEnd;
    }
    html += page.substr(pos);
    return allOk;
}

// ============================================================
// dessa serve
// ============================================================

namespace {

std::string mimeType(const std::string& path) {
    std::string ext = lower(fs::path(path).extension().string());
    if (ext == ".html" || ext == ".htm" || ext == ".dhtml") return "text/html; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "text/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".wasm") return "application/wasm";
    if (isDessaFile(path) || ext == ".txt" || ext == ".phl" || ext == ".md") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

std::string escapeHtml(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i] == '+' ? ' ' : s[i];
        }
    }
    return out;
}

void sendAll(socket_t client, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(client, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

void respond(socket_t client, int status, const std::string& type, const std::string& body) {
    const char* text = status == 200 ? "OK" : status == 404 ? "Not Found" : status == 403 ? "Forbidden" : "Internal Server Error";
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\nContent-Type: " + type +
                       "\r\nContent-Length: " + std::to_string(body.size()) +
                       "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    sendAll(client, head + body);
}

void openBrowser(const std::string& url) {
#ifdef _WIN32
    std::string command = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    std::string command = "open \"" + url + "\" >/dev/null 2>&1 &";
#else
    std::string command = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
#endif
    if (std::system(command.c_str()) != 0) std::cerr << "Open " << url << " in your browser\n";
}

} // namespace

int serve(const std::string& folderArg, int port, const std::string& open, const CompileFn& compile) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::absolute(folderArg.empty() ? "." : folderArg), ec);
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "dessa serve: could not start networking\n";
        return 1;
    }
#endif
    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "dessa serve: could not open a socket\n";
        return 1;
    }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // this computer only
    int chosen = -1;
    for (int p = port; p < port + 20; ++p) {
        address.sin_port = htons(static_cast<unsigned short>(p));
        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0) {
            chosen = p;
            break;
        }
    }
    if (chosen < 0 || listen(server, 16) != 0) {
        std::cerr << "dessa serve: ports " << port << " to " << port + 19 << " are all in use (try --port)\n";
        return 1;
    }
    const std::string base = "http://localhost:" + std::to_string(chosen) + "/";
    std::cout << "Serving " << root.string() << " at " << base << "  (Ctrl+C to stop)\n" << std::flush;
    if (!open.empty()) openBrowser(base + open);

    while (true) {
        socket_t client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 65536) {
            int n = recv(client, buffer, sizeof buffer, 0);
            if (n <= 0) break;
            request.append(buffer, static_cast<size_t>(n));
        }
        std::istringstream first(request.substr(0, request.find("\r\n")));
        std::string method, target;
        first >> method >> target;
        std::string pathPart = urlDecode(target.substr(0, target.find('?')));
        if (pathPart.empty() || pathPart[0] != '/') pathPart = "/" + pathPart;
        fs::path file = fs::weakly_canonical(root / pathPart.substr(1), ec);
        // Stay inside the folder
        std::string rootText = root.string(), fileText = file.string();
        if (fileText.compare(0, rootText.size(), rootText) != 0) {
            respond(client, 403, "text/plain", "Forbidden");
            DESSA_CLOSE(client);
            continue;
        }
        if (fs::is_directory(file, ec)) {
            for (const char* index : {"index.dhtml", "index.html"}) {
                if (fs::is_regular_file(file / index, ec)) {
                    file /= index;
                    break;
                }
            }
        }
        if (!fs::is_regular_file(file, ec)) {
            respond(client, 404, "text/plain; charset=utf-8", "Not found: " + pathPart);
            DESSA_CLOSE(client);
            continue;
        }
        std::string ext = lower(file.extension().string());
        bool read = false;
        std::string body = readFile(file, read);
        if (ext == ".dhtml" || ((ext == ".html" || ext == ".htm") &&
                                (lower(body).find("text/dessa") != std::string::npos || lower(body).find("text/ds") != std::string::npos))) {
            std::string html, errors;
            if (buildPage(file.string(), compile, html, errors)) {
                respond(client, 200, "text/html; charset=utf-8", html);
                std::cout << "  " << pathPart << " (compiled)\n" << std::flush;
            } else {
                std::cerr << errors << std::flush;
                respond(client, 200, "text/html; charset=utf-8",
                        "<!doctype html><meta charset=\"utf-8\"><title>Dessa error</title><body style=\"font-family:monospace;padding:16px\">"
                        "<h2 style=\"color:#c62828\">This page has errors</h2><pre>" + escapeHtml(errors) + "</pre>"
                        "<p>Fix them and reload.</p></body>");
            }
        } else {
            respond(client, 200, mimeType(file.string()), body);
        }
        DESSA_CLOSE(client);
    }
}

} // namespace dessa::web
