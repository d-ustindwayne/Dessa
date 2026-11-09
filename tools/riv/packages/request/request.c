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
#include <direct.h>
#include <process.h>
#define rmdir _rmdir
#else
#include <stdlib.h>
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

/* One setting of curl's config file. Everything a program gives us goes here
 * and never on a command line, so no shell ever reads it. */
static void add_setting(ds_sb* b, const char* name, const char* value, int64_t len) {
    ds_sb_cstr(b, name);
    ds_sb_cstr(b, " = \"");
    for (int64_t i = 0; i < len; ++i) {
        char c = value[i];
        if (c == '\n' || c == '\r' || c == '\0') continue;
        if (c == '"' || c == '\\') ds_sb_bytes(b, "\\", 1);
        ds_sb_bytes(b, &c, 1);
    }
    ds_sb_cstr(b, "\"\n");
}

static void add_setting_c(ds_sb* b, const char* name, const char* value) {
    add_setting(b, name, value, (int64_t)strlen(value));
}

/* For the few paths on the command line: they are our own temporary folder */
static void add_quoted_path(ds_sb* b, const char* value) {
    ds_sb_cstr(b, " \"");
    ds_sb_cstr(b, value);
    ds_sb_cstr(b, "\"");
}

/* A new folder only this user can read: the request's headers pass through it */
static void make_private_folder(char* out, size_t size) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
#ifdef _WIN32
    static unsigned counter = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        snprintf(out, size, "%s/dessa-request-%d-%u-%lld", tmp, _getpid(), ++counter, (long long)time(NULL));
        if (_mkdir(out) == 0) return;
    }
#else
    snprintf(out, size, "%s/dessa-request-XXXXXX", tmp);
    if (mkdtemp(out)) return;
#endif
    ds_fail("request: could not make a temporary folder in %s", tmp);
}

/* Sends one request. `headers` is "Name: value" lines.
 * Returns [status code, response headers, response body]. */
ds_list* dsx_request__send(ds_str* method, ds_str* url, ds_str* headers, ds_str* body, ds_int timeoutSeconds) {
    char base[1024];
    make_private_folder(base, sizeof base);
    char bodyIn[1100], headersOut[1100], bodyOut[1100], errors[1100], settings[1100];
    snprintf(bodyIn, sizeof bodyIn, "%s/in", base);
    snprintf(headersOut, sizeof headersOut, "%s/headers", base);
    snprintf(bodyOut, sizeof bodyOut, "%s/body", base);
    snprintf(errors, sizeof errors, "%s/err", base);
    snprintf(settings, sizeof settings, "%s/curl.cfg", base);

    ds_sb config;
    ds_sb_init(&config);
    ds_sb_cstr(&config, "silent\nshow-error\nlocation\nmax-time = ");
    ds_sb_int(&config, timeoutSeconds > 0 ? timeoutSeconds : 30);
    /* Only the web, also after a redirect: never file:// and the like */
    ds_sb_cstr(&config, "\nproto = \"=http,https\"\nproto-redir = \"=http,https\"\nwrite-out = \"%{http_code}\"\n");
    add_setting(&config, "request", method->data, method->len);
    add_setting_c(&config, "dump-header", headersOut);
    add_setting_c(&config, "output", bodyOut);
    for (int64_t start = 0; start < headers->len;) {
        const char* nl = memchr(headers->data + start, '\n', (size_t)(headers->len - start));
        int64_t end = nl ? nl - headers->data : headers->len;
        if (end > start) add_setting(&config, "header", headers->data + start, end - start);
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
        add_setting_c(&config, "data-binary", at);
    }
    add_setting(&config, "url", url->data, url->len);
    ds_str* configText = ds_sb_done(&config);
    FILE* settingsFile = fopen(settings, "wb");
    if (settingsFile) {
        fwrite(configText->data, 1, (size_t)configText->len, settingsFile);
        fclose(settingsFile);
    }
    ds_release(configText);

    ds_sb command;
    ds_sb_init(&command);
    ds_sb_cstr(&command, "curl -K");
    add_quoted_path(&command, settings);
    ds_sb_cstr(&command, " 2>");
    add_quoted_path(&command, errors);
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
    if (!pipe) {
        remove(settings);
        remove(bodyIn);
        rmdir(base);
        ds_fail("request: could not start curl (is it installed?)");
    }
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
    remove(settings);
    rmdir(base);

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
