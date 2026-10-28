/* dessa_web.h -- the page's objects (webutils) for Dessa compiled to
 * WebAssembly.
 *
 * A page object (Element, Event, Document, ...) lives in JavaScript; Dessa
 * holds a counted ds_js with the object's number in the bridge's table
 * (runtime/dessa_wasm.js). Reading a field, setting one and calling a method
 * go through a few calls to the bridge:
 *
 *     el.textContent = "Hi"   ->  dsw_push_str(s); dsw_set(el, "textContent")
 *     el.appendChild(child)   ->  dsw_push_obj(child); dsw_call(el, "appendChild"); dsw_take_obj(&Element)
 *
 * The functions of webutils itself (alert, log, setTimeout, ...) are called
 * the same way on no object (NULL). Event listeners and timers come back in
 * through dsw_invoke. */
#ifndef DESSA_WEB_H
#define DESSA_WEB_H

#include "dessa_rt.h"

typedef struct ds_js {
    ds_head h;
    int32_t js;          /* the object's number in the bridge's table */
} ds_js;

/* A page object's class (Element, Event, ...): its counted wrapper */
void dsw_js_drop(ds_head* self);
ds_str* dsw_js_str(ds_head* self);
#define DSW_CLASS(name) {(name), DS_K_OBJECT, dsw_js_drop, dsw_js_str, NULL, NULL, 0}

/* Arguments, in order, for the next dsw_set / dsw_call */
void dsw_push_str(ds_str* s);              /* NULL: null */
void dsw_push_num(double v);
void dsw_push_bool(ds_bul v);
void dsw_push_obj(void* object);           /* a ds_js, or NULL */
void dsw_push_strs(ds_list* list);         /* a Str<> as one array */
/* A Dessa function as a JavaScript function: kind 0 takes nothing, kind 1
 * one page object (of class `arg`) */
void dsw_push_fn(ds_fn* f, int kind, const ds_type* arg);

/* object.name, obj.name = the pushed value, obj.name(pushed values...);
 * NULL: webutils' own functions. The result waits for a dsw_take_ */
void dsw_get(void* object, const char* name);
void dsw_set(void* object, const char* name);
void dsw_call(void* object, const char* name);

/* The last result as a Dessa value */
ds_str* dsw_take_str(void);                /* owned; NULL for null / undefined */
ds_str* dsw_take_text(void);               /* owned; "" for null / undefined */
double dsw_take_num(void);
ds_bul dsw_take_bool(void);
void* dsw_take_obj(const ds_type* cls);    /* owned ds_js, NULL for null */
ds_list* dsw_take_objs(const ds_type* cls);/* owned list of ds_js */

#endif
