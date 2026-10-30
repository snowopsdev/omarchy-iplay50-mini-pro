from pathlib import Path
import shutil,time,os
p=Path('/home/hypr/.config/chromium-flags.conf')
b=Path('/opt/omarchy-tablet/backups')/('chromium-video-'+time.strftime('%Y%m%dT%H%M%SZ',time.gmtime()))
b.mkdir(mode=0o700)
shutil.copy2(p,b/p.name)
lines=p.read_text().splitlines()
updates={
 '--enable-features=':'AcceleratedVideoDecoder',
 '--load-extension=':'/opt/omarchy-tablet/video/youtube-h264',
 '--disable-features=':'Vulkan,SkiaGraphite',
}
for prefix, additions in updates.items():
 found=[i for i,x in enumerate(lines) if x.startswith(prefix)]
 values=[v for i in found for v in lines[i][len(prefix):].split(',') if v]
 values.extend(v for v in additions.split(',') if v not in values)
 lines=[x for i,x in enumerate(lines) if i not in found]
 lines.append(prefix+','.join(dict.fromkeys(values)))
for value in ['--use-gl=angle','--use-angle=gles-egl','--ignore-gpu-blocklist','--disable-gpu-rasterization','--no-unsandboxed-zygote','--gpu-launcher=/opt/omarchy-tablet/video/bin/chromium-gpu-launcher']:
 key=value.split('=',1)[0]
 lines=[x for x in lines if x.split('=',1)[0]!=key]
 lines.append(value)
p.write_text('\n'.join(lines).rstrip()+'\n')
os.chown(p,1001,1001)
print('Browser flags backup:',b)
print(p.read_text())
