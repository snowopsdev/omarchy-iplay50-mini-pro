#!/usr/bin/env python3
"""apkfetch.py <apk> <pubkey> <outdir>: verify Alpine v2 apk (RSA-SHA1 sig over control stream, datahash) and extract data tar to outdir."""
import sys, zlib, io, tarfile, hashlib, subprocess, tempfile, os, re
apk, key, out = sys.argv[1:4]
d = open(apk,'rb').read(); segs=[]; i=0
while i < len(d):
    z = zlib.decompressobj(31); o = z.decompress(d[i:]); u = len(d[i:])-len(z.unused_data); segs.append((d[i:i+u], o)); i += u
assert len(segs)==3
sigtar = tarfile.open(fileobj=io.BytesIO(segs[0][1])); sig = sigtar.extractfile(sigtar.getmembers()[0]).read()
with tempfile.TemporaryDirectory() as t:
    open(t+'/s','wb').write(sig); open(t+'/c','wb').write(segs[1][0])
    subprocess.run(['openssl','dgst','-sha1','-verify',key,'-signature',t+'/s',t+'/c'],check=True)
ctl = tarfile.open(fileobj=io.BytesIO(segs[1][1])); pki = ctl.extractfile('.PKGINFO').read().decode()
dh = re.search(r'datahash = (\w+)', pki).group(1)
assert hashlib.sha256(segs[2][0]).hexdigest()==dh, "datahash mismatch"
print("OK signature+datahash", apk, dh)
os.makedirs(out, exist_ok=True)
tarfile.open(fileobj=io.BytesIO(segs[2][1])).extractall(out, filter='tar')
