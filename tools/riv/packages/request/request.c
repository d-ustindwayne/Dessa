/* Native part of $("request") in C, for C programs and WebAssembly
 * (dsx_request__send, see src/codegen/cglue.hpp). Natively the `curl`
 * program sends the request (like request.hpp); in WebAssembly the
 * JavaScript around it does (dessa_wasm.js: _http). */
#ifdef __wasm__
#include "dessa_web.h"
#else
#include "dessa_rt.h"
#include <time.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#endif

static void push_text(ds_list* list, ds_str* text) {
    if (list->len == list->cap) ds_list_grow(list);
    ((ds_str**)list->data)[list->len++] = text;
}

#ifdef __wasm__

/* Returns [status code, response headers, response body] */
ds_list* dsx_request__send(ds_str* method, ds_str* url, ds_str* headers, ds_str* body, ds_int timeoutSeconds) {
    dsw_push_str(method);
    dsw_push_str(url);
    dsw_push_str(headers);
    dsw_push_str(body);
    dsw_push_num((double)timeoutSeconds);
    dsw_call(NULL, "_http");   /* fails with the message when the request couldn't be made */
    ds_list* out = ds_list_new(&ds_elem_str, 3);
    for (int part = 0; part < 3; ++part) {
        dsw_push_num(part);
        dsw_call(NULL, "_httpPart");
        push_text(out, dsw_take_text());
    }
    return out;
}

#else

static ds_str* read_all(const char* path) {
    FILE* f = fopen(path, "rb");
    ds_sb b;
    ds_sb_init(&b);
    if (f) {
        char buffer[65536];
        size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) ds_sb_bytes(&b, buffer, (int64_t)n);
        fclose(f);
    }
    return ds_sb_done(&b);
}

/* One command-line argument, quoted for the shell */
static void add_quoted(ds_sb* b, const char* value, int64_t len) {
    ds_sb_cstr(b, " \"");
    for (int64_t i = 0; i < len; ++i) {
        char c = value[i];
        if (c == '\n' || c == '\r') continue;   /* never let a value break the command */
        if (c == '"' || c == '\\') ds_sb_bytes(b, "\\", 1);
        ds_sb_bytes(b, &c, 1);
    }
    ds_sb_cstr(b, "\"");
}

static void add_quoted_c(ds_sb* b, const char* value) { add_quoted(b, value, (int64_t)strlen(value)); }

/* Sends one request. `headers` is "Name: value" lines.
 * Returns [status code, response headers, response body]. */
ds_list* dsx_request__send(ds_str* method, ds_str* url, ds_str* headers, ds_str* body, ds_int timeoutSeconds) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    static unsigned counter = 0;
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = getpid();
#endif
    char base[1024];
    snprintf(base, sizeof base, "%s/dessa-request-%d-%u-%lld", tmp, pid, ++counter, (long long)time(NULL));
    char bodyIn[1100], headersOut[1100], bodyOut[1100], errors[1100];
    snprintf(bodyIn, sizeof bodyIn, "%s.in", base);
    snprintf(headersOut, sizeof headersOut, "%s.headers", base);
    snprintf(bodyOut, sizeof bodyOut, "%s.body", base);
    snprintf(errors, sizeof errors, "%s.err", base);

    ds_sb command;
    ds_sb_init(&command);
    ds_sb_cstr(&command, "curl -sS -L --max-time ");
    ds_sb_int(&command, timeoutSeconds > 0 ? timeoutSeconds : 30);
    ds_sb_cstr(&command, " -X");
    add_quoted(&command, method->data, method->len);
    ds_sb_cstr(&command, " -D");
    add_quoted_c(&command, headersOut);
    ds_sb_cstr(&command, " -o");
    add_quoted_c(&command, bodyOut);
    ds_sb_cstr(&command, " -w \"%{http_code}\"");
    for (int64_t start = 0; start < headers->len;) {
        const char* nl = memchr(headers->data + start, '\n', (size_t)(headers->len - start));
        int64_t end = nl ? nl - headers->data : headers->len;
        if (end > start) {
            ds_sb_cstr(&command, " -H");
            add_quoted(&command, headers->data + start, end - start);
        }
        start = end + 1;
    }
    const bool sendsBody = body->len > 0 || strcmp(method->data, "POST") == 0 || strcmp(method->data, "PUT") == 0 ||
                           strcmp(method->data, "PATCH") == 0;
    if (sendsBody) {
        FILE* f = fopen(bodyIn, "wb");
        if (f) {
            fwrite(body->data, 1, (size_t)body->len, f);
            fclose(f);
        }
        char at[1104];
        snprintf(at, sizeof at, "@%s", bodyIn);
        ds_sb_cstr(&command, " --data-binary");
        add_quoted_c(&command, at);
    }
    add_quoted(&command, url->data, url->len);
    ds_sb_cstr(&command, " 2>");
    add_quoted_c(&command, errors);
    ds_str* line = ds_sb_done(&command);

#ifdef _WIN32
    /* cmd.exe drops the first and last quote of the line */
    char* wrapped = (char*)malloc((size_t)line->len + 3);
    sprintf(wrapped, "\"%s\"", line->data);
    FILE* pipe = _popen(wrapped, "r");
    free(wrapped);
#else
    FILE* pipe = popen(line->data, "r");
#endif
    ds_release(line);
    if (!pipe) ds_fail("request: could not start curl (is it installed?)");
    ds_sb status;
    ds_sb_init(&status);
    char buffer[256];
    while (fgets(buffer, sizeof buffer, pipe)) ds_sb_cstr(&status, buffer);
#ifdef _WIN32
    int code = _pclose(pipe);
#else
    int code = pclose(pipe);
#endif

    ds_str* responseHeaders = read_all(headersOut);
    ds_str* responseBody = read_all(bodyOut);
    ds_str* errorText = read_all(errors);
    remove(bodyIn);
    remove(headersOut);
    remove(bodyOut);
    remove(errors);

    if (code != 0) {
        ds_str* statusText = ds_sb_done(&status);
        ds_release(statusText);
        ds_release(responseHeaders);
        ds_release(responseBody);
        int64_t len = errorText->len;
        while (len > 0 && (errorText->data[len - 1] == '\n' || errorText->data[len - 1] == '\r')) --len;
        const char* message = errorText->data;
        if (len >= 6 && strncmp(message, "curl: ", 6) == 0) {
            message += 6;
            len -= 6;
        }
        if (len == 0) ds_fail("Request to %s failed: curl exited with code %d", url->data, code);
        ds_fail("Request to %s failed: %.*s", url->data, (int)len, message);
    }
    ds_release(errorText);

    /* With redirects (-L) there is one header block per response; keep the last */
    for (int64_t i = responseHeaders->len - 9; i >= 0; --i) {
        if (memcmp(responseHeaders->data + i, "\r\n\r\nHTTP/", 9) == 0) {
            ds_str* last = ds_str_new(responseHeaders->data + i + 4, responseHeaders->len - i - 4);
            ds_release(responseHeaders);
            responseHeaders = last;
            break;
        }
    }

    ds_list* out = ds_list_new(&ds_elem_str, 3);
    push_text(out, ds_sb_done(&status));
    push_text(out, responseHeaders);
    push_text(out, responseBody);
    return out;
}

#endif
