/* Running a program directly, without a shell, for $("os")'s execute: shared by
 * os.c (C programs) and os.hpp (C++ programs). Plain C.
 *
 * The arguments reach the program as they are: nothing in them is a command,
 * whatever they hold. */
#ifndef DESSA_OS_EXEC_H
#define DESSA_OS_EXEC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len, cap;
} os_exec_buffer;

static inline void os_exec_add(os_exec_buffer* b, const char* data, size_t len) {
    if (b->len + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->len + len + 1) cap *= 2;
        char* bigger = (char*)realloc(b->data, cap);
        if (!bigger) return;
        b->data = bigger;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
}

#ifdef _WIN32

#include <windows.h>

static inline wchar_t* os_exec_wide(const char* text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t* out = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (out) MultiByteToWideChar(CP_UTF8, 0, text, -1, out, n);
    return out;
}

/* One argument as Windows programs read their command line back
 * (CommandLineToArgvW): backslashes count only before a quote */
static inline void os_exec_quote(os_exec_buffer* b, const char* arg) {
    os_exec_add(b, "\"", 1);
    for (const char* p = arg;; ++p) {
        size_t slashes = 0;
        while (*p == '\\') {
            ++slashes;
            ++p;
        }
        for (size_t i = 0; i < slashes * (*p == '"' || *p == '\0' ? 2 : 1); ++i) os_exec_add(b, "\\", 1);
        if (*p == '\0') break;
        if (*p == '"') os_exec_add(b, "\\", 1);
        os_exec_add(b, p, 1);
    }
    os_exec_add(b, "\"", 1);
}

/* Runs it and returns its exit code, with its output (and errors) in `output`;
 * -1 and a message in `error` if it can't be started */
static inline int os_exec_run(const char* program, const char* const* args, size_t count, os_exec_buffer* output, char* error, size_t errorSize) {
    /* Windows hands these to cmd.exe, whose rules for quotes differ: the
     * arguments could turn into commands */
    size_t length = strlen(program);
    if (length >= 4 && (_stricmp(program + length - 4, ".bat") == 0 || _stricmp(program + length - 4, ".cmd") == 0)) {
        snprintf(error, errorSize, "execute can't run %s: batch files go through the command shell (use run, and quote what goes in it yourself)", program);
        return -1;
    }
    os_exec_buffer line = {NULL, 0, 0};
    os_exec_quote(&line, program);
    for (size_t i = 0; i < count; ++i) {
        os_exec_add(&line, " ", 1);
        os_exec_quote(&line, args[i]);
    }
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
    HANDLE readEnd, writeEnd;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
        free(line.data);
        snprintf(error, errorSize, "could not start %s", program);
        return -1;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION process;
    wchar_t* wideLine = os_exec_wide(line.data);
    free(line.data);
    BOOL started = wideLine && CreateProcessW(NULL, wideLine, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process);
    free(wideLine);
    CloseHandle(writeEnd);
    if (!started) {
        CloseHandle(readEnd);
        snprintf(error, errorSize, "could not start %s (is it installed?)", program);
        return -1;
    }
    char chunk[65536];
    DWORD got;
    while (ReadFile(readEnd, chunk, sizeof chunk, &got, NULL) && got > 0) os_exec_add(output, chunk, got);
    CloseHandle(readEnd);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return (int)code;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static inline int os_exec_run(const char* program, const char* const* args, size_t count, os_exec_buffer* output, char* error, size_t errorSize) {
    int ends[2], failure[2];
    if (pipe(ends) != 0 || pipe(failure) != 0) {
        snprintf(error, errorSize, "could not start %s", program);
        return -1;
    }
    const char** argv = (const char**)malloc((count + 2) * sizeof(char*));
    argv[0] = program;
    for (size_t i = 0; i < count; ++i) argv[i + 1] = args[i];
    argv[count + 1] = NULL;
    fflush(stdout);
    pid_t child = fork();
    if (child < 0) {
        free(argv);
        snprintf(error, errorSize, "could not start %s", program);
        return -1;
    }
    if (child == 0) {
        dup2(ends[1], 1);
        dup2(ends[1], 2);
        close(ends[0]);
        close(ends[1]);
        close(failure[0]);
        /* Closed by a successful exec: nothing arriving here means it started */
        fcntl(failure[1], F_SETFD, FD_CLOEXEC);
        execvp(program, (char* const*)argv);
        char failed = 1;
        ssize_t ignored = write(failure[1], &failed, 1);
        (void)ignored;
        _exit(127);
    }
    free(argv);
    close(ends[1]);
    close(failure[1]);
    char chunk[65536];
    ssize_t got;
    while ((got = read(ends[0], chunk, sizeof chunk)) > 0 || (got < 0 && errno == EINTR)) {
        if (got > 0) os_exec_add(output, chunk, (size_t)got);
    }
    close(ends[0]);
    char failed = 0;
    ssize_t failedRead = read(failure[0], &failed, 1);
    close(failure[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (failedRead == 1) {
        snprintf(error, errorSize, "could not start %s (is it installed?)", program);
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

#endif
#endif
