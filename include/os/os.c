/* $("os") -- the native functions that C programs and WebAssembly call
 * (dsx_os_<name>, see src/codegen/cglue.hpp). The simple file functions
 * (readFile, writeFile, ...) are in the runtime (dessa_rt.c). */
#include "dessa_rt.h"
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#ifndef __wasm__
#include "os_exec.h"
#endif

static void push_text(ds_list* list, const char* text) {
    if (list->len == list->cap) ds_list_grow(list);
    ((ds_str**)list->data)[list->len++] = ds_str_cstr(text);
}

static bool is_dir(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int by_bytes(const void* a, const void* b) {
    ds_str* x = *(ds_str* const*)a;
    ds_str* y = *(ds_str* const*)b;
    int64_t n = x->len < y->len ? x->len : y->len;
    int c = memcmp(x->data, y->data, (size_t)n);
    return c ? c : (x->len < y->len ? -1 : x->len > y->len ? 1 : 0);
}

ds_list* dsx_os_listDir(ds_str* path) {
    DIR* dir = opendir(path->data);
    if (!dir) ds_fail("Cannot list folder '%s': %s", path->data, strerror(errno));
    ds_list* names = ds_list_new(&ds_elem_str, 8);
    for (struct dirent* entry; (entry = readdir(dir)) != NULL;) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        push_text(names, entry->d_name);
    }
    closedir(dir);
    qsort(names->data, (size_t)names->len, sizeof(ds_str*), by_bytes);
    return names;
}

/* Removes `path` and everything in it; adds how many were removed to *count */
static bool remove_all(const char* path, ds_int* count) {
    if (is_dir(path)) {
        DIR* dir = opendir(path);
        if (!dir) return false;
        size_t length = strlen(path);
        for (struct dirent* entry; (entry = readdir(dir)) != NULL;) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            char* inner = (char*)malloc(length + strlen(entry->d_name) + 2);
            sprintf(inner, "%s/%s", path, entry->d_name);
            bool ok = remove_all(inner, count);
            free(inner);
            if (!ok) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);
#ifdef _WIN32
        if (_rmdir(path) != 0) return false;
#else
        if (rmdir(path) != 0) return false;
#endif
    } else if (remove(path) != 0) {
        return false;
    }
    ++*count;
    return true;
}

ds_int dsx_os_deleteAll(ds_str* path) {
    struct stat st;
    if (stat(path->data, &st) != 0) return 0;
    ds_int count = 0;
    if (!remove_all(path->data, &count)) ds_fail("Cannot delete '%s': %s", path->data, strerror(errno));
    return count;
}

void dsx_os_copyFile(ds_str* from, ds_str* to) {
    FILE* in = fopen(from->data, "rb");
    if (!in) ds_fail("Cannot read file '%s': %s", from->data, strerror(errno));
    FILE* out = fopen(to->data, "wb");
    if (!out) {
        fclose(in);
        ds_fail("Cannot write file '%s': %s", to->data, strerror(errno));
    }
    char chunk[65536];
    size_t n;
    bool wrote = true;
    while (wrote && (n = fread(chunk, 1, sizeof chunk, in)) > 0) wrote = fwrite(chunk, 1, n, out) == n;
    fclose(in);
    if (fclose(out) != 0 || !wrote) ds_fail("Cannot write file '%s'", to->data);
}

void dsx_os_moveFile(ds_str* from, ds_str* to) {
#ifdef _WIN32
    /* rename won't replace a file on Windows, as it does elsewhere */
    if (_access(to->data, 0) == 0 && _access(from->data, 0) == 0) remove(to->data);
#endif
    if (rename(from->data, to->data) != 0) ds_fail("Cannot move '%s' to '%s': %s", from->data, to->data, strerror(errno));
}

ds_int dsx_os_fileSize(ds_str* path) {
    struct stat st;
    if (stat(path->data, &st) != 0) ds_fail("Cannot read file '%s': %s", path->data, strerror(errno));
    return (ds_int)st.st_size;
}

ds_int dsx_os_modifiedTime(ds_str* path) {
    struct stat st;
    if (stat(path->data, &st) != 0) ds_fail("Cannot read file '%s': %s", path->data, strerror(errno));
    return (ds_int)st.st_mtime;
}

void dsx_os_setEnv(ds_str* name, ds_str* value) {
#ifdef _WIN32
    if (_putenv_s(name->data, value->data) != 0) ds_fail("Cannot set '%s'", name->data);
#else
    if (setenv(name->data, value->data, 1) != 0) ds_fail("Cannot set '%s': %s", name->data, strerror(errno));
#endif
}

/* [exit code, output], see os_exec.h */
ds_list* dsx_os__execute(ds_str* program, ds_list* args) {
#ifdef __wasm__
    (void)program;
    (void)args;
    ds_fail("execute: WebAssembly can't start other programs");
#else
    fflush(stdout);
    const char** values = (const char**)malloc(((size_t)args->len + 1) * sizeof(char*));
    for (int64_t i = 0; i < args->len; ++i) values[i] = ((ds_str**)args->data)[i]->data;
    os_exec_buffer output = {NULL, 0, 0};
    char error[1200];
    int code = os_exec_run(program->data, values, (size_t)args->len, &output, error, sizeof error);
    free(values);
    if (code < 0) {
        free(output.data);
        ds_fail("%s", error);
    }
    ds_list* out = ds_list_new(&ds_elem_str, 2);
    char number[24];
    snprintf(number, sizeof number, "%d", code);
    ((ds_str**)out->data)[out->len++] = ds_str_cstr(number);
    ((ds_str**)out->data)[out->len++] = ds_str_new(output.data ? output.data : "", (int64_t)output.len);
    free(output.data);
    return out;
#endif
}

ds_int dsx_os_run(ds_str* command) {
    fflush(stdout);
#if defined(__wasm__)
    (void)command;
    ds_fail("run: WebAssembly can't start other programs");
#elif defined(_WIN32)
    /* cmd.exe drops the first and last quote of the line: "a b.exe" "x" would break */
    char* line = (char*)malloc((size_t)command->len + 3);
    sprintf(line, "\"%s\"", command->data);
    int code = system(line);
    free(line);
    return code;
#else
    int code = system(command->data);
    return code;
#endif
}

/* ---- class File in os.dm: open files by number ---- */

typedef struct open_file {
    FILE* f;
    char* path;
    char mode;   /* 'r', 'w' or 'a' */
} open_file;

static open_file* files = NULL;
static int64_t fileCount = 0, fileCap = 0;

static open_file* file_at(ds_int handle, bool reading) {
    if (handle < 0 || handle >= fileCount || !files[handle].f) ds_fail("The file is closed");
    open_file* o = &files[handle];
    if (reading && o->mode != 'r') ds_fail("File '%s' is open for writing, not reading", o->path);
    if (!reading && o->mode == 'r') ds_fail("File '%s' is open for reading, not writing (open it with \"w\" or \"a\")", o->path);
    return o;
}

ds_int dsx_os__fileOpen(ds_str* path, ds_str* mode) {
    const char* how = NULL;
    if (strcmp(mode->data, "r") == 0) how = "rb";
    else if (strcmp(mode->data, "w") == 0) how = "wb";
    else if (strcmp(mode->data, "a") == 0) how = "ab";
    else ds_fail("open: mode must be \"r\" (read), \"w\" (write) or \"a\" (add to the end), got \"%s\"", mode->data);
    FILE* f = fopen(path->data, how);
    if (!f) {
        if (how[0] == 'r') ds_fail("Cannot read file '%s'", path->data);
        ds_fail("Cannot write file '%s'", path->data);
    }
    if (fileCount == fileCap) {
        fileCap = fileCap ? fileCap * 2 : 8;
        files = (open_file*)realloc(files, (size_t)fileCap * sizeof(open_file));
    }
    open_file* o = &files[fileCount];
    o->f = f;
    o->path = (char*)malloc((size_t)path->len + 1);
    memcpy(o->path, path->data, (size_t)path->len + 1);
    o->mode = mode->data[0];
    return fileCount++;
}

ds_str* dsx_os__fileReadLine(ds_int handle) {
    open_file* o = file_at(handle, true);
    ds_sb b;
    ds_sb_init(&b);
    int c;
    bool any = false;
    while ((c = getc(o->f)) != EOF) {
        any = true;
        if (c == '\n') break;
        char ch = (char)c;
        ds_sb_bytes(&b, &ch, 1);
    }
    ds_str* line = ds_sb_done(&b);
    if (!any) {
        ds_release(line);
        return NULL;
    }
    if (line->len > 0 && line->data[line->len - 1] == '\r') {
        ds_str* trimmed = ds_str_new(line->data, line->len - 1);
        ds_release(line);
        return trimmed;
    }
    return line;
}

ds_str* dsx_os__fileRead(ds_int handle) {
    open_file* o = file_at(handle, true);
    ds_sb b;
    ds_sb_init(&b);
    char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof buffer, o->f)) > 0) ds_sb_bytes(&b, buffer, (int64_t)n);
    clearerr(o->f);
    return ds_sb_done(&b);
}

ds_bul dsx_os__fileAtEnd(ds_int handle) {
    open_file* o = file_at(handle, true);
    int c = getc(o->f);
    if (c == EOF) return true;
    ungetc(c, o->f);
    return false;
}

void dsx_os__fileWrite(ds_int handle, ds_str* text) {
    open_file* o = file_at(handle, false);
    if (fwrite(text->data, 1, (size_t)text->len, o->f) != (size_t)text->len) ds_fail("Cannot write file '%s'", o->path);
}

void dsx_os__fileFlush(ds_int handle) {
    fflush(file_at(handle, false)->f);
}

void dsx_os__fileClose(ds_int handle) {
    if (handle < 0 || handle >= fileCount || !files[handle].f) return;
    fclose(files[handle].f);
    free(files[handle].path);
    files[handle].f = NULL;
    files[handle].path = NULL;
}
