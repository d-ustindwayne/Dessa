/* The sockets under $("http"), shared by http.c (C programs) and http.hpp
 * (C++ programs). Plain C: a handle is the socket as an integer, errors are
 * reported through `error` (a message) and a -1 / NULL result.
 *
 * Connections are read side by side with select(), and a request reaches the
 * program only when all of it has arrived. So a client that sends slowly, or
 * nothing at all, doesn't hold up the others; it gets a limit of time and of
 * size instead. Handlers still run one at a time (no threads). */
#ifndef DESSA_HTTP_SOCKET_H
#define DESSA_HTTP_SOCKET_H

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET hs_socket;
#define HS_BAD INVALID_SOCKET
#define hs_closesocket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int hs_socket;
#define HS_BAD (-1)
#define hs_closesocket close
#endif

#define HS_MAX_CONNECTIONS 60        /* fits select() on Windows (64 sockets) */
#define HS_MAX_HEAD (64 * 1024)      /* the request line and headers */

typedef struct {
    hs_socket socket;
    int64_t server;
    char* data;
    size_t len, cap, want;
    time_t started;
    int used, complete;
} hs_connection;

static hs_connection hs_connections[HS_MAX_CONNECTIONS];

static inline int hs_started(void) {
#ifdef _WIN32
    static int started = 0;
    if (!started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;
        started = 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);   /* a client that left is an error from send, not the end of the program */
#endif
    return 1;
}

/* A socket listening on `host` ("127.0.0.1", "0.0.0.0", ...) and `port`, or -1 */
static inline int64_t hs_listen(int64_t port, const char* host, char* error, size_t size) {
    if (!hs_started()) {
        snprintf(error, size, "the network could not be started");
        return -1;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        snprintf(error, size, "'%s' is not an address like 127.0.0.1", host);
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

/* The port a socket really got (port 0 asks the system to choose) */
static inline int64_t hs_port(int64_t server) {
    struct sockaddr_in address;
    socklen_t size = sizeof address;
    if (getsockname((hs_socket)server, (struct sockaddr*)&address, &size) != 0) return -1;
    return ntohs(address.sin_port);
}

static inline void hs_set_blocking(hs_socket s, int blocking) {
#ifdef _WIN32
    u_long nonblocking = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#endif
}

/* An answer can't wait for a client forever either */
static inline void hs_set_send_timeout(hs_socket s, int seconds) {
#ifdef _WIN32
    DWORD ms = (DWORD)seconds * 1000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof ms);
#else
    struct timeval limit;
    limit.tv_sec = seconds;
    limit.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&limit, sizeof limit);
#endif
}

static inline int hs_would_block(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
#endif
}

static inline void hs_drop(hs_connection* c) {
    hs_closesocket(c->socket);
    free(c->data);
    memset(c, 0, sizeof *c);
}

/* A short answer from the server itself, and the connection ends */
static inline void hs_refuse(hs_connection* c, const char* status) {
    char answer[200];
    int n = snprintf(answer, sizeof answer, "HTTP/1.1 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
    hs_set_blocking(c->socket, 1);
    hs_set_send_timeout(c->socket, 2);
    send(c->socket, answer, n, 0);
    hs_drop(c);
}

/* Checks the headers once they have all arrived. Returns the size of the body
 * (-1: refused, and the connection is gone). A length written twice or in a
 * strange way, and chunked bodies, which aren't read here, are refused: another
 * server in front of this one could read those differently. */
static inline int64_t hs_body_size(hs_connection* c, size_t headLength, int64_t maxBytes) {
    int64_t length = -1;
    const char* end = c->data + headLength;
    for (const char* line = (const char*)memchr(c->data, '\n', headLength); line && line < end;) {
        ++line;
        const char* stop = (const char*)memchr(line, '\n', (size_t)(end - line));
        if (!stop) stop = end;
        static const char lengthName[] = "content-length:", codingName[] = "transfer-encoding:";
        const size_t size = (size_t)(stop - line);
        int isLength = size >= sizeof lengthName - 1, isCoding = size >= sizeof codingName - 1;
        for (size_t i = 0; isLength && i < sizeof lengthName - 1; ++i) isLength = tolower((unsigned char)line[i]) == lengthName[i];
        for (size_t i = 0; isCoding && i < sizeof codingName - 1; ++i) isCoding = tolower((unsigned char)line[i]) == codingName[i];
        if (isCoding) {
            hs_refuse(c, "501 Not Implemented");
            return -1;
        }
        if (isLength) {
            const char* p = line + sizeof lengthName - 1;
            while (p < stop && (*p == ' ' || *p == '\t')) ++p;
            const char* q = stop;
            while (q > p && (q[-1] == '\r' || q[-1] == ' ' || q[-1] == '\t')) --q;
            int64_t value = 0;
            int valid = p < q && length < 0;
            for (; valid && p < q; ++p) {
                valid = *p >= '0' && *p <= '9' && value <= maxBytes;
                if (valid) value = value * 10 + (*p - '0');
            }
            if (!valid && value > maxBytes) {
                hs_refuse(c, "413 Content Too Large");
                return -1;
            }
            if (!valid) {
                hs_refuse(c, "400 Bad Request");
                return -1;
            }
            length = value;
        }
        line = stop < end ? stop : NULL;
    }
    if (length > maxBytes) {
        hs_refuse(c, "413 Content Too Large");
        return -1;
    }
    return length < 0 ? 0 : length;
}

/* Reads what has arrived on one connection */
static inline void hs_receive(hs_connection* c, int64_t maxBytes) {
    if (c->len == c->cap) {
        char* bigger = (char*)realloc(c->data, c->cap * 2);
        if (!bigger) {
            hs_refuse(c, "503 Service Unavailable");
            return;
        }
        c->data = bigger;
        c->cap *= 2;
    }
    size_t room = c->cap - c->len;
    int got = recv(c->socket, c->data + c->len, (int)(room > (1u << 20) ? (1u << 20) : room), 0);
    if (got == 0 || (got < 0 && !hs_would_block())) {
        hs_drop(c);
        return;
    }
    if (got < 0) return;
    size_t before = c->len;
    c->len += (size_t)got;
    if (c->want == 0) {
        /* The end of the headers may lie across two reads */
        size_t from = before >= 3 ? before - 3 : 0;
        const char* end = NULL;
        for (size_t i = from; i + 4 <= c->len; ++i) {
            if (memcmp(c->data + i, "\r\n\r\n", 4) == 0) {
                end = c->data + i;
                break;
            }
        }
        if (!end) {
            if (c->len > HS_MAX_HEAD) hs_refuse(c, "431 Request Header Fields Too Large");
            return;
        }
        size_t headLength = (size_t)(end - c->data);
        if (headLength > HS_MAX_HEAD) {
            hs_refuse(c, "431 Request Header Fields Too Large");
            return;
        }
        int64_t body = hs_body_size(c, headLength, maxBytes);
        if (body < 0) return;
        c->want = headLength + 4 + (size_t)body;
    }
    if (c->len >= c->want) {
        c->len = c->want;
        c->complete = 1;
        hs_set_blocking(c->socket, 1);
    }
}

/* Waits until some client has sent a whole request, and returns its
 * connection; -1 if the server's socket no longer works. */
static inline int64_t hs_next(int64_t server, int64_t timeoutSeconds, int64_t maxBytes) {
    for (;;) {
        time_t now = time(NULL);
        fd_set readable;
        FD_ZERO(&readable);
        hs_socket highest = (hs_socket)server;
        for (int i = 0; i < HS_MAX_CONNECTIONS; ++i) {
            hs_connection* c = &hs_connections[i];
            if (!c->used || c->server != server) continue;
            if (c->complete) continue;
            FD_SET(c->socket, &readable);
            if (c->socket > highest) highest = c->socket;
        }
        int total = 0;
        for (int i = 0; i < HS_MAX_CONNECTIONS; ++i) total += hs_connections[i].used;
        /* When full, new clients wait in the system's queue */
        if (total < HS_MAX_CONNECTIONS) FD_SET((hs_socket)server, &readable);
        struct timeval wait;
        wait.tv_sec = 1;   /* to notice clients that ran out of time */
        wait.tv_usec = 0;
        int ready = select((int)highest + 1, &readable, NULL, NULL, &wait);
        if (ready < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            return -1;
        }
        if (total < HS_MAX_CONNECTIONS && FD_ISSET((hs_socket)server, &readable)) {
            hs_socket s = accept((hs_socket)server, NULL, NULL);
            if (s != HS_BAD) {
#ifndef _WIN32
                if (s >= FD_SETSIZE) {
                    hs_closesocket(s);
                    continue;
                }
#endif
                for (int i = 0; i < HS_MAX_CONNECTIONS; ++i) {
                    hs_connection* c = &hs_connections[i];
                    if (c->used) continue;
                    c->data = (char*)malloc(8192);
                    if (!c->data) {
                        hs_closesocket(s);
                        break;
                    }
                    c->used = 1;
                    c->socket = s;
                    c->server = server;
                    c->cap = 8192;
                    c->started = now;
                    hs_set_blocking(s, 0);
                    hs_set_send_timeout(s, (int)timeoutSeconds);
                    break;
                }
            }
        }
        for (int i = 0; i < HS_MAX_CONNECTIONS; ++i) {
            hs_connection* c = &hs_connections[i];
            if (c->used && !c->complete && c->server == server && FD_ISSET(c->socket, &readable)) {
                hs_receive(c, maxBytes);
                if (c->used && c->complete) return (int64_t)c->socket;
            }
        }
        /* Checked after reading: a slow handler isn't the client's fault */
        now = time(NULL);
        for (int i = 0; i < HS_MAX_CONNECTIONS; ++i) {
            hs_connection* c = &hs_connections[i];
            if (c->used && !c->complete && c->server == server && (int64_t)(now - c->started) >= timeoutSeconds) {
                hs_refuse(c, "408 Request Timeout");
            }
        }
    }
}

/* The request read on a connection hs_next returned. The connection keeps it
 * until it is closed. */
static inline const char* hs_request(int64_t conn, size_t* outLen) {
    for (int i = 0; i < HS_MAX_CONNECTIONS; ++i) {
        hs_connection* c = &hs_connections[i];
        if (c->used && (int64_t)c->socket == conn) {
            *outLen = c->len;
            return c->data;
        }
    }
    *outLen = 0;
    return "";
}

static inline int hs_write(int64_t conn, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        size_t part = len - sent > (1u << 30) ? (1u << 30) : len - sent;
        int n = send((hs_socket)conn, data + sent, (int)part, 0);
        if (n <= 0) return 0;
        sent += (size_t)n;
    }
    return 1;
}

/* Closes a connection, or a server with the connections still waiting on it */
static inline void hs_close(int64_t s) {
    for (int i = 0; i < HS_MAX_CONNECTIONS; ++i) {
        hs_connection* c = &hs_connections[i];
        if (!c->used) continue;
        if ((int64_t)c->socket == s) {
            hs_drop(c);
            return;
        }
        if (c->server == s) hs_drop(c);
    }
    hs_closesocket((hs_socket)s);
}

#endif
