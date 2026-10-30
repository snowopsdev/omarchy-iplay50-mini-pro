#!/usr/bin/env python3
"""sim-overlay.py <root> <cpio>...  : unpack newc cpios in order into <root> with (approximate) kernel unpacker semantics:
parent-path symlinks are followed (absolute targets resolved inside <root>), existing entries of another type are replaced,
device nodes are skipped (unprivileged). Prints notable events."""
import os, sys, stat
root = os.path.realpath(sys.argv[1])
def resolve_parent(path):          # resolve all but the last component inside root
    parts = [p for p in path.split("/") if p not in ("", ".")]
    cur = root; depth = 0
    for i, p in enumerate(parts[:-1]):
        nxt = os.path.join(cur, p)
        while os.path.islink(nxt):
            depth += 1
            if depth > 40: raise OSError("loop")
            t = os.readlink(nxt)
            nxt = os.path.normpath(os.path.join(root, t.lstrip("/"))) if t.startswith("/") else os.path.normpath(os.path.join(os.path.dirname(nxt), t))
            if not nxt.startswith(root): raise OSError("escape " + nxt)
        if not os.path.exists(nxt): os.makedirs(nxt, exist_ok=True)
        cur = nxt
    return os.path.join(cur, parts[-1]) if parts else root
def parse(data):
    i = 0
    while True:
        assert data[i:i+6] == b"070701", i
        h = [int(data[i+6+8*k:i+14+8*k], 16) for k in range(13)]
        mode, size, namesz = h[1], h[6], h[11]
        name = data[i+110:i+110+namesz-1].decode()
        i += 110 + namesz; i += (-i) % 4
        body = data[i:i+size]; i += size; i += (-i) % 4
        if name == "TRAILER!!!": return i
        yield name, mode, body
        # (multiple concatenated archives are handled by the caller via offset)
def unpack(path):
    data = open(path, "rb").read(); off = 0; n = 0; ev = []
    while off < len(data):
        if data[off:off+6] != b"070701":
            off += 4 if data[off:off+4] == b"\0\0\0\0" else len(data); continue
        sub = data[off:]; g = parse(sub); 
        try:
            while True:
                name, mode, body = next(g)
                dst = resolve_parent(name); n += 1
                if os.path.lexists(dst):
                    cur = os.lstat(dst).st_mode
                    if stat.S_ISDIR(mode) and stat.S_ISDIR(cur): continue
                    if stat.S_ISDIR(cur):
                        if os.path.islink(dst): pass
                        try: os.rmdir(dst)
                        except OSError: ev.append("cannot replace dir %s" % name); continue
                    else: os.unlink(dst)
                    ev.append("replaced %s" % name) if stat.S_ISDIR(cur) or stat.S_ISLNK(cur) else None
                t = stat.S_IFMT(mode)
                if t == stat.S_IFDIR: os.makedirs(dst, exist_ok=True)
                elif t == stat.S_IFLNK: os.symlink(body.decode(), dst)
                elif t == stat.S_IFREG:
                    open(dst, "wb").write(body); os.chmod(dst, mode & 0o7777 | 0o200)
                else: ev.append("skip node %s" % name)
        except StopIteration as e:
            off += e.value
    print("%s: %d entries; events: %s" % (os.path.basename(path), n, "; ".join(ev[:12]) or "none"))
for c in sys.argv[2:]: unpack(c)
