/* Execute the actual keyboard state machine and Cairo renderer off-screen.
 * Only outbound Wayland messages are replaced by a recorder. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "keyboard.h"
#include "config.mobintl.h"
#include "proto/virtual-keyboard-unstable-v1-client-protocol.h"

static unsigned keys[KEY_MAX + 1], presses[KEY_MAX + 1], mask;
static const char *last_action;
uint32_t wl_proxy_get_version(struct wl_proxy *proxy) { return 1; }
struct wl_display *wl_proxy_get_display(struct wl_proxy *proxy) { return (void *)1; }
int wl_display_flush(struct wl_display *display) { return 0; }
struct wl_proxy *wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
    const struct wl_interface *interface, uint32_t version, uint32_t flags, ...)
{
    va_list args;
    va_start(args, flags);
    if (opcode == ZWP_VIRTUAL_KEYBOARD_V1_KEY) {
        (void)va_arg(args, uint32_t); // timestamp
        unsigned code = va_arg(args, uint32_t), down = va_arg(args, uint32_t);
        assert(code <= KEY_MAX);
        keys[code] = down;
        presses[code] += down;
    } else if (opcode == ZWP_VIRTUAL_KEYBOARD_V1_MODIFIERS) {
        mask = va_arg(args, uint32_t);
    }
    va_end(args);
    return NULL;
}
static void action(const char *name) { last_action = name; }
static void released(void) {
    assert(mask == 0);
    for (unsigned i = 0; i <= KEY_MAX; ++i) assert(keys[i] == 0);
}
static struct key *label(enum layout_id page, const char *name) {
    for (struct key *k = layouts[page].keys; k->type != Last; ++k)
        if (k->type != EndRow && strcmp(k->label, name) == 0) return k;
    abort();
}
static void tap(struct kbd *kb, struct key *key) {
    kbd_press_key(kb, key, 123);
    kbd_release_key(kb, 124);
}
static void render(struct kbd *kb, enum layout_id page, unsigned width,
                   unsigned height, const char *filename) {
    struct drwbuf *buffer = kb->surf->back_buffer;
    if (buffer->layout) g_object_unref(buffer->layout);
    if (buffer->cairo) cairo_destroy(buffer->cairo);
    if (buffer->cairo_surf) cairo_surface_destroy(buffer->cairo_surf);
    buffer->cairo_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    buffer->cairo = cairo_create(buffer->cairo_surf);
    buffer->layout = pango_cairo_create_layout(buffer->cairo);
    kb->w = width; kb->h = height; kb->layout = &layouts[page];
    kbd_init_layout(kb->layout, width, height);
    assert(kbd_get_rows(kb->layout) == 5);
    for (struct key *k = kb->layout->keys; k->type != Last; ++k) {
        if (k->width <= 0) continue;
        assert(k->h >= 52 && k->w >= 64);
        assert(k->x + k->w <= width && k->y + k->h <= height);
        assert(kbd_get_key(kb, k->x + k->w / 2, k->y + k->h / 2) == k);
    }
    kbd_draw_layout(kb);
    assert(cairo_surface_write_to_png(buffer->cairo_surf, filename) == CAIRO_STATUS_SUCCESS);
}
int main(void) {
    struct drwbuf buffer = {0};
    struct drwsurf surface = {.back_buffer=&buffer, .released=true, .scale=1,
        .damage=cairo_region_create(), .backport_damage=cairo_region_create()};
    struct kbd kb = {.surf=&surface, .schemes=schemes, .layouts=layouts,
        .layout=&layouts[Tablet], .vkbd=(void *)1, .show_highlight=true, .action=action,
        .layers=layers, .landscape_layers=landscape_layers};
    // Same palette and font as tablet-ui, using real Cairo/Pango drawing.
    schemes[0].bg.color = schemes[1].bg.color = 0xff16181d;
    schemes[0].fg.color = 0xff292d36; schemes[1].fg.color = 0xff343b48;
    schemes[0].high.color = schemes[1].high.color = 0xff6a8caf;
    for (size_t i = 0; i < sizeof(schemes)/sizeof(schemes[0]); ++i)
        schemes[i].font_description = pango_font_description_from_string("DejaVu Sans 20");
    for (enum layout_id page=Tablet; page<=Kid123; ++page) {
        char filename[128];
        snprintf(filename,sizeof(filename),"ui/evidence/%s-portrait.png",layouts[page].name);
        render(&kb,page,800,360,filename);
        snprintf(filename,sizeof(filename),"ui/evidence/%s-landscape.png",layouts[page].name);
        render(&kb,page,1280,260,filename);
    }
    kb.layout = &layouts[Tablet];
    tap(&kb,label(Tablet,"q")); assert(presses[KEY_Q] == 1); released();
    // Two tap modifiers compose, and unlatch after the next ordinary key.
    tap(&kb,label(Tablet,"Ctrl")); tap(&kb,label(Tablet,"Super"));
    kbd_press_key(&kb,label(Tablet,"c"),125); assert(mask == (Ctrl|Super));
    kbd_release_key(&kb,126); assert(kb.mods == 0); released();
    // Forced multi-modifier shortcuts must never leave Meta/Shift down.
    tap(&kb,label(TabletOmarchy,"Prev win")); released();
    tap(&kb,label(TabletOmarchy,"Copy")); released();
    tap(&kb,label(TabletOmarchy,"Ctrl+A")); released();
    tap(&kb,label(TabletOmarchy,"Desk 2")); released();
    // A forced modifier combines with an already latched modifier using OR.
    struct key ctrl_c = {"copy","copy",1,Code,KEY_C,.code_mod=Ctrl,
        .x=0,.y=0,.w=100,.h=52};
    tap(&kb,label(Tablet,"Ctrl"));
    kbd_press_key(&kb,&ctrl_c,130); assert(mask == Ctrl);
    kbd_release_key(&kb,131); released();
    // Cancellation/hiding resets pending keys and latched modifiers.
    tap(&kb,label(Tablet,"Alt")); kbd_reset_state(&kb,135); released();
    tap(&kb,label(Tablet,"Shift"));
    kbd_press_key(&kb,label(Tablet,"h"),136);
    kbd_reset_state(&kb,137); assert(kb.last_press == NULL); released();
    // Actions are fixed argv names, and clear the modifier state first.
    tap(&kb,label(Tablet,"Super")); tap(&kb,label(TabletOmarchy,"Apps"));
    assert(last_action && strcmp(last_action,"apps") == 0); released();
    last_action = NULL; tap(&kb,label(Tablet,"Hide"));
    assert(last_action && strcmp(last_action,"keyboard-hide") == 0); released();
    last_action = NULL; tap(&kb,label(TabletSymbols,"Hide"));
    assert(last_action && strcmp(last_action,"keyboard-hide") == 0); released();
    last_action = NULL; tap(&kb,label(TabletNav,"Hide"));
    assert(last_action && strcmp(last_action,"keyboard-hide") == 0); released();
    last_action = NULL; tap(&kb,label(TabletOmarchy,"Hide"));
    assert(last_action && strcmp(last_action,"keyboard-hide") == 0); released();
    /* Kid pages: no Super/Ctrl/Alt, no route to Fn/Desktop, only Hide. */
    for (int page = KidAbc; page <= Kid123; page++) {
        for (struct key *k = layouts[page].keys; k->type != Last; k++) {
            if (k->type == Mod) assert(k->code == Shift);
            if (k->code_mod) assert(k->code_mod == Shift);
            if (k->type == Layout) assert(k->layout == &layouts[KidAbc] || k->layout == &layouts[Kid123]);
            if (k->type == Action) assert(strcmp(k->action, "keyboard-hide") == 0);
        }
    }
    last_action = NULL; tap(&kb,label(KidAbc,"Hide"));
    assert(last_action && strcmp(last_action,"keyboard-hide") == 0); released();
    puts("PASS: 12 native renders, both geometries, hit targets, one tap/letter, modifier chords, forced modifier release, cancellation, fixed actions, Hide on every page, kid pages without shortcut modifiers");
}
