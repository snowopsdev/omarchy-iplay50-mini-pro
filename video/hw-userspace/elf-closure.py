#!/usr/bin/env python3
"""Read ELF DT_NEEDED closure from explicitly provided local acquisition roots.
Never executes target ELF files. Search order is command line order, but every
same-name conflict is reported; Bionic runtime namespace resolution is NOT proven.
"""
import argparse, hashlib, json, re, subprocess
from collections import deque
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('entry',type=Path)
p.add_argument('--root',action='append',type=Path,required=True)
p.add_argument('--provided-soname',action='append',default=[],help='Explicit runtime-supplied virtual SONAME, e.g. ld-android.so from Bionic linker')
a=p.parse_args()
index={}
for root in a.root:
    for f in sorted(root.rglob('*')):
        if f.is_file() and ('.so' in f.name or f.name in ('linker','linker64')):
            index.setdefault(f.name,[]).append(f)
q=deque([a.entry]);seen=set();objects=[];missing={}
while q:
    f=q.popleft()
    if f in seen:continue
    seen.add(f)
    data=f.read_bytes()
    r=subprocess.run(['llvm-readelf','-h','-l','-d','-n',str(f)],capture_output=True,text=True,check=True).stdout
    needed=re.findall(r'\(NEEDED\).*?\[(.*?)\]',r)
    interp=re.search(r'Requesting program interpreter: (.*?)\]',r)
    build=re.search(r'Build ID: (\w+)',r)
    props={}
    for line in r.splitlines():
        for key in ('Class','Machine'):
            if line.strip().startswith(key+':'):props[key]=line.split(':',1)[1].strip()
    objects.append({'path':str(f),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'elf':props,'build_id':build.group(1) if build else None,'interpreter':interp.group(1) if interp else None,'needed':needed})
    for n in needed:
        if n in a.provided_soname:continue
        choices=index.get(n,[])
        if not choices:missing.setdefault(n,[]).append(str(f))
        else:q.append(choices[0])
conflicts={name:[str(f) for f in fs] for name,fs in index.items() if len(fs)>1 and any(name in o['needed'] for o in objects)}
print(json.dumps({'entry':str(a.entry),'roots':[str(x) for x in a.root],'provided_sonames':a.provided_soname,'objects':objects,'missing':missing,'conflicts':conflicts,'limitations':['DT_NEEDED only; dlopen branches are not covered','Search order approximates a proposed flat private namespace, not Android installed linker namespaces','No ELF or vendor daemon execution was performed']},indent=2))
raise SystemExit(bool(missing))
