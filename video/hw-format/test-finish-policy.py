#!/usr/bin/env python3
"""Host-only build and regression tests for the conditional GL finish shim."""
import hashlib,json,os,pathlib,subprocess
ROOT=pathlib.Path(__file__).resolve().parent
PROJECT=ROOT.parents[1]  # repository root (video/hw-format -> ..)
H=ROOT/'finish-harness'
commands=[]
def run_build(args):
 commands.append(args)
 subprocess.run(args,cwd=PROJECT,check=True,capture_output=True,text=True)
def compile_shared(source,output,extra=()):
 run_build(['cc','-std=c11','-O2','-fPIC','-shared','-Wall','-Wextra','-Werror',str(source),*map(str,extra),'-o',str(output)])
compile_shared(ROOT/'chromium-gl-finish-loader.c',ROOT/'chromium-gl-finish-loader-host.so',['-ldl','-pthread'])
compile_shared(H/'finish-policy.c',H/'finish-policy.so')
for letter,n in [('a',1),('b',2)]: compile_shared(H/'provider.c',H/f'provider-{letter}.so',[f'-DPROVIDER_ID={n}'])
compile_shared(H/'next-a.c',H/'next-a.so',['-ldl'])
compile_shared(H/'next-b.c',H/'next-b.so')
compile_shared(H/'local-dep.c',H/'local-dep.so')
compile_shared(H/'local-caller.c',H/'local-caller.so',[f'-L{H}','-l:local-dep.so','-Wl,-rpath,$ORIGIN','-ldl'])
for name,out in [('loader-main.c','loader-run'),('policy-main.c','policy-run')]:
 run_build(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(H/name),'-ldl','-o',str(H/out)])
compile_shared(PROJECT/'video/hw-driver/compat/mt6789-v4l2-bridge.c',H/'actual-bridge-host.so',[ROOT/'m21s-convert.c',ROOT/'m21s-argb.c','-lyuv','-ldl','-pthread'])
compile_shared(PROJECT/'graphics/shim/aq-software-primary.c',H/'actual-aq-host.so',['-ldl'])
compile_shared(PROJECT/'graphics/shim/drm-no-cursor.c',H/'actual-drm-host.so',['-ldl'])
loader=ROOT/'chromium-gl-finish-loader-host.so'
scope=[H/'next-a.so',H/'next-b.so']
base=[loader,*scope]
mock=[loader,H/'finish-policy.so',*scope]
actual=[loader,H/'actual-bridge-host.so',H/'actual-aq-host.so',H/'actual-drm-host.so',*scope]
actual_after=[H/'actual-bridge-host.so',loader,H/'actual-aq-host.so',H/'actual-drm-host.so',*scope]
cases=[
 ('missing-query-standalone-finish',base,'1','--type=gpu-process',1,None,False),
 ('missing-query-cleared-env-finish',base,'1','--type=gpu-process',1,None,True),
 ('renderer-bypass',base,'0','--type=renderer',1,None,False),
 ('disabled-bypass',base,'0','--type=gpu-process',0,None,False),
 ('query-zero-real-flush',mock,'0','--type=gpu-process',1,'0',False),
 ('query-one-real-finish',mock,'1','--type=gpu-process',1,'1',False),
 ('query-zero-cleared-env-real-flush',mock,'0','--type=gpu-process',1,'0',True),
 ('actual-bridge-no-capture-loader-first',actual,'0','--type=gpu-process',1,None,False),
 ('actual-bridge-no-capture-loader-after-bridge',actual_after,'0','--type=gpu-process',1,None,False),
]
results=[]
def test(name,stack,args,enabled,policy,bridge=False,always=False):
 env=dict(os.environ,TABLET_CHROMIUM_GL_FINISH=str(enabled),GALLIUM_DRIVER='llvmpipe',LIBGL_ALWAYS_SOFTWARE='1',LD_PRELOAD=':'.join(map(str,stack)))
 env.pop('TABLET_TEST_FINISH_POLICY',None)
 env.pop('TABLET_CHROMIUM_GL_FINISH_ALWAYS',None)
 if always: env['TABLET_CHROMIUM_GL_FINISH_ALWAYS']='1'
 if policy is not None: env['TABLET_TEST_FINISH_POLICY']=policy
 if bridge: env.update(TABLET_V4L2_BRIDGE='1',TABLET_V4L2_BRIDGE_PROCESS='gpu')
 proc=subprocess.run(args,cwd=H,env=env,capture_output=True,text=True,timeout=15)
 results.append(dict(case=name,command=args,preload=list(map(str,stack)),returncode=proc.returncode,stdout=proc.stdout,stderr=proc.stderr))
 print(name,'PASS' if proc.returncode==0 else 'FAIL')
 assert proc.returncode==0,(name,proc.stdout,proc.stderr)
for name,stack,expect,role,enabled,policy,clear in cases:
 args=['./loader-run',expect,role]+(['clear-env'] if clear else [])
 test(name,stack,args,enabled,policy,name.startswith('actual-bridge'))
test('dynamic-switch-and-nested-guard',mock,['./policy-run','--type=gpu-process'],1,'0')
# TABLET_CHROMIUM_GL_FINISH_ALWAYS=1: query 0 / real bridge without capture
# still get real glFinish (no partially rasterised llvmpipe frames).
test('always-query-zero-real-finish',mock,['./loader-run','1','--type=gpu-process'],1,'0',always=True)
test('always-query-zero-cleared-env-real-finish',mock,['./loader-run','1','--type=gpu-process','clear-env'],1,'0',always=True)
test('always-actual-bridge-no-capture-finish',actual,['./loader-run','1','--type=gpu-process'],1,None,True,always=True)
test('always-renderer-bypass',base,['./loader-run','0','--type=renderer'],1,None,always=True)
# The real bridge export wins via normal ELF symbol resolution; no codecs are
# opened on the host. Its own device lifecycle tests exercise allocated NV12.
symbols=subprocess.run(['readelf','-Ws',str(loader)],capture_output=True,text=True,check=True).stdout
assert any('WEAK' in line and 'UND' in line and 'tablet_v4l2_capture_requires_finish' in line for line in symbols.splitlines())
assembly=subprocess.run(['objdump','-d',str(loader),'--disassemble=dlsym'],capture_output=True,text=True,check=True).stdout
result={'pass':True,'scope':'host-only no codec/device operations','source_sha256':hashlib.sha256((ROOT/'chromium-gl-finish-loader.c').read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(loader.read_bytes()).hexdigest(),'bridge_source_sha256':hashlib.sha256((PROJECT/'video/hw-driver/compat/mt6789-v4l2-bridge.c').read_bytes()).hexdigest(),'build_commands':commands,'tests':results,'weak_query_symbol':[line.strip() for line in symbols.splitlines() if 'tablet_v4l2_capture_requires_finish' in line],'dlsym_trampoline_disassembly':assembly}
(ROOT/'conditional-finish-host-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('All',len(results),'cases passed')
