/*
 * gpsprops: LD_PRELOAD shim for the vendor GPS daemon (mnld) running in a
 * private Bionic chroot without Android's property service. It answers the
 * Bionic property API from a small in-memory table: compiled-in defaults (the
 * values the vendor connsys tools would have set) plus anything the daemon
 * sets itself. Lines "name=value" in /gps-shim/props.txt override defaults.
 * No libc string helpers are used; open/read/write/close come from Bionic.
 */
#include <stddef.h>
#include <stdint.h>

#define NAME_MAX_ 96
#define VALUE_MAX 92            /* PROP_VALUE_MAX */
#define SLOTS 160

struct prop { char name[NAME_MAX_]; char value[VALUE_MAX]; uint32_t serial; };
static struct prop table[SLOTS];
static int used, loaded;

extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern long write(int, const void *, unsigned long);
extern int close(int);

static const char *defaults[][2] = {
    { "persist.vendor.connsys.chipid", "0x6789" },
    { "vendor.connsys.adie.chipid", "0x6631" },
    { "vendor.connsys.driver.ready", "yes" },
    { "vendor.service.nvram_init", "Ready" },
    { "ro.build.type", "user" },
    { "ro.debuggable", "0" },
    { "ro.vendor.mtk_log_hide_gps", "0" },
};

static int eq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
static int cpy(char *d, const char *s, int max)
{
    int n = 0;
    while (s[n] && n < max - 1) { d[n] = s[n]; n++; }
    d[n] = 0;
    return n;
}
static void logline(const char *what, const char *n, const char *v)
{
    char buf[256]; int i = 0;
    i += cpy(buf + i, "gpsprops: ", 200); i += cpy(buf + i, what, 200 - i);
    i += cpy(buf + i, " ", 200 - i); i += cpy(buf + i, n, 240 - i);
    if (v) { i += cpy(buf + i, "=", 250 - i); i += cpy(buf + i, v, 254 - i); }
    buf[i++] = '\n';
    write(2, buf, i);
}
static struct prop *lookup(const char *name)
{
    for (int i = 0; i < used; i++) if (eq(table[i].name, name)) return &table[i];
    return 0;
}
static struct prop *put(const char *name, const char *value)
{
    struct prop *p = lookup(name);
    if (!p) { if (used == SLOTS) return 0; p = &table[used++]; cpy(p->name, name, NAME_MAX_); }
    cpy(p->value, value, VALUE_MAX); p->serial += 2;
    return p;
}
static void load(void)
{
    static char buf[8192];
    if (loaded) return;
    loaded = 1;
    for (unsigned i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) put(defaults[i][0], defaults[i][1]);
    int fd = open("/gps-shim/props.txt", 0);
    if (fd < 0) return;
    long n = read(fd, buf, sizeof(buf) - 1); close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    char *line = buf;
    while (*line) {
        char *e = line; while (*e && *e != '\n') e++;
        char save = *e; *e = 0;
        char *eqp = line; while (*eqp && *eqp != '=') eqp++;
        if (*line != '#' && *eqp == '=') { *eqp = 0; put(line, eqp + 1); }
        if (!save) break;
        line = e + 1;
    }
}

typedef struct prop prop_info;

const prop_info *__system_property_find(const char *name)
{
    load();
    struct prop *p = lookup(name);
    if (!p) logline("miss", name, 0);
    return p;
}
int __system_property_get(const char *name, char *value)
{
    const prop_info *p = __system_property_find(name);
    if (!p) { value[0] = 0; return 0; }
    return cpy(value, p->value, VALUE_MAX);
}
int __system_property_read(const prop_info *p, char *name, char *value)
{
    if (name) cpy(name, p->name, 32);
    return cpy(value, p->value, VALUE_MAX);
}
void __system_property_read_callback(const prop_info *p,
        void (*cb)(void *, const char *, const char *, uint32_t), void *cookie)
{
    cb(cookie, p->name, p->value, p->serial);
}
uint32_t __system_property_serial(const prop_info *p) { return p->serial; }
int __system_property_set(const char *name, const char *value)
{
    load();
    logline("set", name, value);
    return put(name, value) ? 0 : -1;
}
int __system_property_foreach(void (*fn)(const prop_info *, void *), void *cookie)
{
    load();
    for (int i = 0; i < used; i++) fn(&table[i], cookie);
    return 0;
}
