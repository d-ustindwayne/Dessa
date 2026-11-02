/* Native part of $("http") in C (dsx_http__..., see http.native). A server
 * needs sockets, which WebAssembly doesn't have: there every call fails. */
#include "dessa_rt.h"

#ifdef __wasm__

ds_int dsx_http__listen(ds_int port) {
    (void)port;
    ds_fail("http: a server can't run in WebAssembly (run it as a native program)");
}
ds_int dsx_http__accept(ds_int server) { (void)server; return -1; }
ds_str* dsx_http__read(ds_int conn) { (void)conn; return ds_str_cstr(""); }
void dsx_http__write(ds_int conn, ds_str* data) { (void)conn; (void)data; }
void dsx_http__close(ds_int handle) { (void)handle; }

#else

#include "http_socket.h"

ds_int dsx_http__listen(ds_int port) {
    char error[200];
    int64_t s = hs_listen(port, error, sizeof error);
    if (s < 0) ds_fail("http: %s", error);
    return s;
}

ds_int dsx_http__accept(ds_int server) {
    return hs_accept(server);
}

/* The whole request, or "" when the other side closed */
ds_str* dsx_http__read(ds_int conn) {
    size_t len = 0;
    char* data = hs_read(conn, &len);
    if (!data) return ds_str_cstr("");
    ds_str* text = ds_str_new(data, (int64_t)len);
    free(data);
    return text;
}

void dsx_http__write(ds_int conn, ds_str* data) {
    hs_write(conn, data->data, (size_t)data->len);
}

void dsx_http__close(ds_int handle) {
    hs_close(handle);
}

#endif
