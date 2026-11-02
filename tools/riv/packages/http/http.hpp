// $("http") -- native part for C++ programs: the sockets of a small web
// server (http_socket.h, shared with http.c). The API (Server, Request,
// Response) is written in Dessa in http.dm.
#pragma once
#include "dessa_runtime.hpp"
#include "http_socket.h"

namespace dessa::http {

inline Int _listen(Int port) {
    char error[200];
    int64_t s = hs_listen(port, error, sizeof error);
    if (s < 0) fail(Str("http: ") + error);
    return s;
}

inline Int _accept(Int server) { return hs_accept(server); }

// The whole request, or "" when the other side closed
inline Str _read(Int conn) {
    size_t len = 0;
    char* data = hs_read(conn, &len);
    if (!data) return "";
    Str text(data, len);
    std::free(data);
    return text;
}

inline void _write(Int conn, const Str& data) { hs_write(conn, data.data(), data.size()); }

inline void _close(Int handle) { hs_close(handle); }

} // namespace dessa::http
