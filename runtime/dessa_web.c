/* dessa_web.c -- see dessa_web.h. Compiled with the runtime for programs
 * and pages built to WebAssembly. */
#include "dessa_web.h"

#define DSW_IMPORT(name) __attribute__((import_module("dessa"), import_name(#name)))
#define DSW_EXPORT(name) __attribute__((used, export_name(#name)))

/* The bridge (dessa_wasm.js) */
DSW_IMPORT(push_str) void js_push_str(const char* bytes, int32_t len);
DSW_IMPORT(push_null) void js_push_null(void);
DSW_IMPORT(push_num) void js_push_num(double v);
DSW_IMPORT(push_bool) void js_push_bool(int32_t v);
DSW_IMPORT(push_obj) void js_push_obj(int32_t js);
DSW_IMPORT(push_fn) void js_push_fn(void* f, int32_t kind, const void* arg);
DSW_IMPORT(pack) void js_pack(int32_t count);
DSW_IMPORT(get) void js_get(int32_t js, const char* name, int32_t len);
DSW_IMPORT(set) void js_set(int32_t js, const char* name, int32_t len);
DSW_IMPORT(call) int32_t js_call(int32_t js, const char* name, int32_t len);   /* 1: it threw (the message is the result) */
DSW_IMPORT(take_num) double js_take_num(void);
DSW_IMPORT(take_bool) int32_t js_take_bool(void);
DSW_IMPORT(take_obj) int32_t js_take_obj(void);
DSW_IMPORT(text_len) int32_t js_text_len(void);       /* -1 for null / undefined */
DSW_IMPORT(text_copy) void js_text_copy(char* to);
DSW_IMPORT(length) int32_t js_length(int32_t js);
DSW_IMPORT(item) int32_t js_item(int32_t js, int32_t index);
DSW_IMPORT(drop) void js_drop(int32_t js);

static int32_t js_of(void* object) {
    return object ? ((ds_js*)object)->js : 0;
}

static void* wrap(int32_t js, const ds_type* cls) {
    if (!js) return NULL;
    ds_js* o = (ds_js*)ds_new_object(cls, sizeof(ds_js));
    o->js = js;
    return o;
}

void dsw_js_drop(ds_head* self) {
    js_drop(((ds_js*)self)->js);
    ds_object_free(self, sizeof(ds_js));
}

ds_str* dsw_js_str(ds_head* self) {
    return ds_str_cstr(self->type->name);
}

void dsw_push_str(ds_str* s) {
    if (s) js_push_str(s->data, (int32_t)s->len);
    else js_push_null();
}
void dsw_push_num(double v) { js_push_num(v); }
void dsw_push_bool(ds_bul v) { js_push_bool(v ? 1 : 0); }
void dsw_push_obj(void* object) {
    if (object) js_push_obj(js_of(object));
    else js_push_null();
}
void dsw_push_strs(ds_list* list) {
    for (int64_t i = 0; i < list->len; ++i) dsw_push_str(((ds_str**)list->data)[i]);
    js_pack((int32_t)list->len);
}
void dsw_push_fn(ds_fn* f, int kind, const ds_type* arg) {
    if (!f) {
        js_push_null();
        return;
    }
    ds_retain(f);   /* the page keeps it (a listener, a timer) */
    js_push_fn(f, kind, arg);
}

void dsw_get(void* object, const char* name) {
    js_get(js_of(object), name, (int32_t)strlen(name));
}
void dsw_set(void* object, const char* name) {
    js_set(js_of(object), name, (int32_t)strlen(name));
}

ds_str* dsw_take_str(void) {
    int32_t len = js_text_len();
    if (len < 0) return NULL;
    if (len == 0) return ds_str_cstr("");
    char small[256];
    char* bytes = len <= (int32_t)sizeof small ? small : (char*)malloc((size_t)len);
    js_text_copy(bytes);
    ds_str* s = ds_str_new(bytes, len);
    if (bytes != small) free(bytes);
    return s;
}
ds_str* dsw_take_text(void) {
    ds_str* s = dsw_take_str();
    return s ? s : ds_str_cstr("");
}

void dsw_call(void* object, const char* name) {
    if (js_call(js_of(object), name, (int32_t)strlen(name))) {
        ds_fail_str(dsw_take_text());   /* what JavaScript threw */
    }
}

double dsw_take_num(void) { return js_take_num(); }
ds_bul dsw_take_bool(void) { return js_take_bool() != 0; }
void* dsw_take_obj(const ds_type* cls) { return wrap(js_take_obj(), cls); }

ds_list* dsw_take_objs(const ds_type* cls) {
    int32_t array = js_take_obj();
    int32_t n = array ? js_length(array) : 0;
    ds_list* list = ds_list_new(&ds_elem_ref, n);
    for (int32_t i = 0; i < n; ++i) {
        if (list->len == list->cap) ds_list_grow(list);
        ((void**)list->data)[list->len++] = wrap(js_item(array, i), cls);
    }
    if (array) js_drop(array);
    return list;
}

/* An event listener or a timer: the Dessa function runs; an error nobody
 * catches is shown, and the page goes on */
DSW_EXPORT(dsw_invoke) void dsw_invoke(ds_fn* f, int32_t kind, int32_t js, const ds_type* arg) {
    ds_catch frame;
    frame.prev = ds_handler;
    ds_handler = &frame;
    if (DS_TRY(frame)) {
        if (kind == 1) {
            void* o = wrap(js, arg);
            ((void (*)(ds_fn*, void*))f->code)(f, o);
            ds_release(o);
        } else {
            ((void (*)(ds_fn*))f->code)(f);
        }
    } else {
        ds_report_uncaught();
    }
    ds_handler = frame.prev;
    fflush(stdout);
}
