/* Native part of $("http") in C (dsx_http__..., see http.native). A server
 * needs sockets, which WebAssembly doesn't have: there every call fails. */
#include "dessa_rt.h"

#ifdef __wasm__

ds_int dsx_http__listen(ds_int port, ds_str* host) {
    (void)port;
    (void)host;
    ds_fail("http: a server can't run in WebAssembly (run it as a native program)");
}
ds_int dsx_http__port(ds_int server) { (void)server; return -1; }
ds_int dsx_http__next(ds_int server, ds_int timeoutSeconds, ds_int maxBytes) { (void)server; (void)timeoutSeconds; (void)maxBytes; return -1; }
ds_str* dsx_http__take(ds_int conn) { (void)conn; return ds_str_cstr(""); }
void dsx_http__write(ds_int conn, ds_str* data) { (void)conn; (void)data; }
void dsx_http__close(ds_int handle) { (void)handle; }
ds_int dsx_http__size(ds_str* text) { return text->len; }

#else

#include "http_socket.h"

ds_int dsx_http__listen(ds_int port, ds_str* host) {
    char error[300];
    int64_t s = hs_listen(port, host->data, error, sizeof error);
    if (s < 0) ds_fail("http: %s", error);
    return s;
}

ds_int dsx_http__port(ds_int server) {
    return hs_port(server);
}

ds_int dsx_http__next(ds_int server, ds_int timeoutSeconds, ds_int maxBytes) {
    return hs_next(server, timeoutSeconds, maxBytes);
}

/* The whole request a connection from _next sent */
ds_str* dsx_http__take(ds_int conn) {
    size_t len = 0;
    const char* data = hs_request(conn, &len);
    return ds_str_new(data, (int64_t)len);
}

void dsx_http__write(ds_int conn, ds_str* data) {
    hs_write(conn, data->data, (size_t)data->len);
}

void dsx_http__close(ds_int handle) {
    hs_close(handle);
}

/* In bytes: a file's content needn't be valid text */
ds_int dsx_http__size(ds_str* text) {
    return text->len;
}

#endif
