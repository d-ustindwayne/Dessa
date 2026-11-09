// $("http") -- native part for C++ programs: the sockets of a small web
// server (http_socket.h, shared with http.c). The API (Server, Request,
// Response) is written in Dessa in http.dm.
#pragma once
#include "dessa_runtime.hpp"
#include "http_socket.h"

namespace dessa::http {

inline Int _listen(Int port, const Str& host) {
    char error[300];
    int64_t s = hs_listen(port, host.c_str(), error, sizeof error);
    if (s < 0) fail(Str("http: ") + error);
    return s;
}

inline Int _port(Int server) { return hs_port(server); }

inline Int _next(Int server, Int timeoutSeconds, Int maxBytes) { return hs_next(server, timeoutSeconds, maxBytes); }

// The whole request a connection from _next sent
inline Str _take(Int conn) {
    size_t len = 0;
    const char* data = hs_request(conn, &len);
    return Str(data, len);
}

inline void _write(Int conn, const Str& data) { hs_write(conn, data.data(), data.size()); }

inline void _close(Int handle) { hs_close(handle); }

// In bytes: a file's content needn't be valid text
inline Int _size(const Str& text) { return static_cast<Int>(text.size()); }

} // namespace dessa::http
