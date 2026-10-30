import pathlib, subprocess, json
base=pathlib.Path('/vendor_dlkm/lib/modules')
loaded={x.split()[0] for x in pathlib.Path('/proc/modules').read_text().splitlines()}
files={p.stem.replace('-','_'):p for p in base.glob('*.ko')}
seen=set(); visiting=set(); ordered=[];missing=[]
def visit(name):
 name=name.replace('-','_')
 if name in loaded or name in seen:return
 if name in visiting:raise RuntimeError('cycle '+name)
 visiting.add(name)
 p=files.get(name)
 if p is None:missing.append(name);visiting.remove(name);seen.add(name);return
 dep=subprocess.check_output(['modinfo','-F','depends',str(p)],text=True).strip()
 for d in dep.split(','):
  if d:visit(d)
 visiting.remove(name);seen.add(name);ordered.append({'name':name,'path':str(p),'depends':dep})
for n in ['mtk_vcodec_common','vcp','mtk_vcu','mtk_vcodec_dec_v1']:visit(n)
print(json.dumps({'unloaded_order':ordered,'missing':missing},indent=2))
