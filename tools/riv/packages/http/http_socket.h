/* The sockets under $("http"), shared by http.c (C programs) and http.hpp
 * (C++ programs). Plain C: a handle is the socket as an integer, errors are
 * reported through `error` (a message) and a -1 / NULL result. */
#ifndef DESSA_HTTP_SOCKET_H
#define DESSA_HTTP_SOCKET_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET hs_socket;
#define HS_BAD INVALID_SOCKET
#define hs_closesocket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int hs_socket;
#define HS_BAD (-1)
#define hs_closesocket close
#endif

static inline int hs_started(void) {
#ifdef _WIN32
    static int started = 0;
    if (!started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;
        started = 1;
    }
#endif
    return 1;
}

/* A socket listening on `port` (all addresses), or -1 */
static inline int64_t hs_listen(int64_t port, char* error, size_t size) {
    if (!hs_started()) {
        snprintf(error, size, "the network could not be started");
        return -1;
    }
    hs_socket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == HS_BAD) {
        snprintf(error, size, "could not make a socket");
        return -1;
    }
    int yes = 1;
#ifndef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
#else
    (void)yes;
#endif
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr*)&address, sizeof address) != 0) {
        hs_closesocket(s);
        snprintf(error, size, "port %lld is already in use (or not allowed)", (long long)port);
        return -1;
    }
    if (listen(s, 64) != 0) {
        hs_closesocket(s);
        snprintf(error, size, "could not listen on port %lld", (long long)port);
        return -1;
    }
    return (int64_t)s;
}

/* The next connection (waits for it), or -1 */
static inline int64_t hs_accept(int64_t server) {
    hs_socket c = accept((hs_socket)server, NULL, NULL);
    return c == HS_BAD ? -1 : (int64_t)c;
}

static inline const char* hs_find(const char* data, size_t len, const char* what) {
    size_t n = strlen(what);
    for (size_t i = 0; i + n <= len; ++i) {
        if (memcmp(data + i, what, n) == 0) return data + i;
    }
    return NULL;
}

/* Content-Length in the headers (0 when there is none) */
static inline size_t hs_content_length(const char* head, size_t len) {
    const char* names[] = {"\r\nContent-Length:", "\r\ncontent-length:", "\r\nContent-length:", "\r\nCONTENT-LENGTH:"};
    for (int i = 0; i < 4; ++i) {
        const char* at = hs_find(head, len, names[i]);
        if (at) return (size_t)strtoull(at + strlen(names[i]), NULL, 10);
    }
    return 0;
}

/* One whole request (the headers and Content-Length bytes of body), malloc'd,
 * or NULL when the other side closed. Requests over 64 MB are cut off. */
static inline char* hs_read(int64_t conn, size_t* outLen) {
    size_t cap = 8192, len = 0;
    char* data = (char*)malloc(cap);
    size_t want = (size_t)-1;
    while (data) {
        if (len == cap) {
            if (cap >= ((size_t)64 << 20)) break;
            cap *= 2;
            char* bigger = (char*)realloc(data, cap);
            if (!bigger) break;
            data = bigger;
        }
        int got = recv((hs_socket)conn, data + len, (int)(cap - len), 0);
        if (got <= 0) break;
        len += (size_t)got;
        if (want == (size_t)-1) {
            const char* end = hs_find(data, len, "\r\n\r\n");
            if (end) want = (size_t)(end - data) + 4 + hs_content_length(data, (size_t)(end - data));
        }
        if (want != (size_t)-1 && len >= want) break;
    }
    if (!data || len == 0) {
        free(data);
        return NULL;
    }
    *outLen = len;
    return data;
}

static inline int hs_write(int64_t conn, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send((hs_socket)conn, data + sent, (int)(len - sent), 0);
        if (n <= 0) return 0;
        sent += (size_t)n;
    }
    return 1;
}

static inline void hs_close(int64_t s) {
    hs_closesocket((hs_socket)s);
}

#endif
