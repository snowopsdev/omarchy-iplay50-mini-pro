#!/usr/bin/env python3
# Annotate llvm-objdump aarch64 disassembly: resolve adrp+add to rodata C strings and bl to PLT names.
import re, sys, subprocess
binf, dis = sys.argv[1], sys.argv[2]
data = open(binf,'rb').read()
# section table via llvm-readelf
out = subprocess.run(['llvm-readelf','-S','-W',binf],capture_output=True,text=True).stdout
secs=[]
for l in out.splitlines():
    m=re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)',l)
    if m: secs.append((m.group(1),int(m.group(3),16),int(m.group(4),16),int(m.group(5),16)))
def rd(addr,n=80):
    for nm,va,off,sz in secs:
        if va<=addr<va+sz and nm in('.rodata','.data','.text'):
            b=data[off+addr-va:off+addr-va+n]
            s=b.split(b'\0')[0]
            return s.decode('latin1')
    return None
# plt names via relocation
rel = subprocess.run(['llvm-readelf','-r','-W',binf],capture_output=True,text=True).stdout
got={}
for l in rel.splitlines():
    m=re.match(r'([0-9a-f]+)\s+[0-9a-f]+\s+R_AARCH64_JUMP_SLOT\s+[0-9a-f]+\s+(\S+)',l)
    if m: got[int(m.group(1),16)]=m.group(2)
# plt stubs: each 16 bytes after plt header; parse from dis
lines=open(dis).read().splitlines()
plt={}
cur=None
for i,l in enumerate(lines):
    m=re.match(r'\s*([0-9a-f]+):\s+adrp\s+x16, 0x([0-9a-f]+)',l)
    if m and i+1<len(lines):
        m2=re.match(r'\s*[0-9a-f]+:\s+ldr\s+x17, \[x16(?:, #(\d+))?\]',lines[i+1])
        if m2:
            g=int(m.group(2),16)+int(m2.group(1) or 0)
            plt[int(m.group(1),16)]=got.get(g,'?')
regs={}
res=[]
for l in lines:
    m=re.match(r'\s*([0-9a-f]+):\s+adrp\s+(x\d+), 0x([0-9a-f]+)',l)
    if m: regs[m.group(2)]=int(m.group(3),16); res.append(l); continue
    m=re.match(r'\s*([0-9a-f]+):\s+add\s+(x\d+), (x\d+), #0x([0-9a-f]+)',l)
    if m and m.group(3) in regs:
        a=regs[m.group(3)]+int(m.group(4),16); s=rd(a)
        res.append(l+'    ; %#x %r'%(a,s)); regs[m.group(2)]=a; continue
    m=re.match(r'\s*([0-9a-f]+):\s+bl\s+0x([0-9a-f]+)',l)
    if m:
        t=int(m.group(2),16)
        res.append(l+('    ; '+plt[t] if t in plt else '')); continue
    res.append(l)
print('\n'.join(res))
