import sys
def load(p):
    d={}
    for l in open(p):
        f=l.rstrip('\n').split('\t')
        d[f[1]]=(f[0],f[2],f[3])
    return d
s=load(sys.argv[1]); m=load(sys.argv[2])
ident=[k for k in s if k in m and s[k]==m[k]]
changed=[k for k in s if k in m and s[k]!=m[k]]
missing=[k for k in s if k not in m]
added=[k for k in m if k not in s]
print(f"stock={len(s)} mine={len(m)} identical={len(ident)} changed={len(changed)} missing={len(missing)} added={len(added)}")
for k in changed[:50]: print("CHANGED",k,s[k],m[k])
for k in missing[:50]: print("MISSING",k)
for k in added[:50]: print("ADDED",k)
