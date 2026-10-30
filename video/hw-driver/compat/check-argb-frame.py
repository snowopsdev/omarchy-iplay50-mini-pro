#!/usr/bin/env python3
"""Compare an actual packed AR24 capture against independent FFmpeg BGRA.

No tablet access. Supply independently software-decoded visible I420 reference
and the exact matrix/range observed from the vendor read-only color descriptor.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess

p=argparse.ArgumentParser()
p.add_argument('capture',type=pathlib.Path)
p.add_argument('reference_i420',type=pathlib.Path)
p.add_argument('--coded-width',required=True,type=int)
p.add_argument('--coded-height',required=True,type=int)
p.add_argument('--width',required=True,type=int)
p.add_argument('--height',required=True,type=int)
p.add_argument('--stride',type=int)
p.add_argument('--left',type=int,default=0)
p.add_argument('--top',type=int,default=0)
p.add_argument('--format',choices=('AR24','BGR4'),default='AR24')
p.add_argument('--matrix',required=True,type=int,choices=(1,5,6))
p.add_argument('--full-range',required=True,type=int,choices=(0,1))
p.add_argument('--tolerance',type=int,default=1)
p.add_argument('--output',type=pathlib.Path)
a=p.parse_args()
w,h,cw,ch=a.width,a.height,a.coded_width,a.coded_height
stride=a.stride or cw*4
if not (0<w<=cw<=8192 and 0<h<=ch<=8192 and w%2==h%2==0 and
        cw*4<=stride<=32768 and stride%4==0 and 0<=a.left<=cw-w and
        0<=a.top<=ch-h and 0<=a.tolerance<=255):
    p.error('invalid visible/coded geometry, stride, crop, or tolerance')
actual_raw=a.capture.read_bytes()
reference=a.reference_i420.read_bytes()
if len(actual_raw)<stride*ch or len(reference)!=w*h*3//2:
    p.error('short coded AR24 capture or incorrect visible I420 reference size')
actual=b''.join(actual_raw[(y+a.top)*stride+a.left*4:
                          (y+a.top)*stride+(a.left+w)*4] for y in range(h))
filt=(f'scale=in_color_matrix={"bt709" if a.matrix==1 else "bt601"}:'
      f'in_range={"full" if a.full_range else "limited"}:out_range=full:'
      'flags=neighbor+accurate_rnd+bitexact+full_chroma_int')
cmd=['ffmpeg','-hide_banner','-loglevel','error','-f','rawvideo',
     '-pixel_format','yuv420p','-video_size',f'{w}x{h}','-i','pipe:0',
     '-vf',filt,'-frames:v','1','-f','rawvideo','-pix_fmt','bgra','pipe:1']
expected=subprocess.run(cmd,input=reference,capture_output=True,check=True).stdout
if len(expected)!=len(actual):
    raise RuntimeError('independent FFmpeg output length mismatch')
channels={}
for i,name in enumerate('BGRA'):
    delta=[abs(x-y) for x,y in zip(actual[i::4],expected[i::4])]
    channels[name]={'max_abs':max(delta),'mean_abs':sum(delta)/len(delta),
                    'samples_different':sum(d!=0 for d in delta)}
passed=(all(channels[k]['max_abs']<=a.tolerance for k in 'BGR') and
        channels['A']['max_abs']==0 and set(actual[3::4])=={255})
sha=lambda data:hashlib.sha256(data).hexdigest()
result={'pass':passed,'byte_order':'actual BGRA', 'v4l2_format':a.format,
        'matrix_coefficients':a.matrix,'full_range':a.full_range,
        'coded_size':[cw,ch],'visible_crop':[a.left,a.top,w,h],
        'stride':stride,'tolerance':a.tolerance,'channels':channels,
        'capture_sha256':sha(actual_raw),'actual_visible_sha256':sha(actual),
        'reference_i420_sha256':sha(reference),'ffmpeg_bgra_sha256':sha(expected),
        'ffmpeg_filter':filt}
text=json.dumps(result,indent=2)+'\n'
if a.output: a.output.write_text(text)
print(text,end='')
raise SystemExit(0 if passed else 1)
