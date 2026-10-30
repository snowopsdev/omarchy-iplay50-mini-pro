#!/usr/bin/env python3
"""Independent FFmpeg color/packing checks; no tablet access or DMA ioctls."""
import argparse,collections,ctypes,hashlib,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--library',type=pathlib.Path,default=ROOT/'libtablet-m21s-argb-host.so')
parser.add_argument('--output',type=pathlib.Path,default=ROOT/'argb-host-validation.json')
args=parser.parse_args()
lib=ctypes.CDLL(str(args.library.resolve())); convert=lib.tablet_nv12_to_argb
convert.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint32,ctypes.c_uint,ctypes.c_uint]
convert.restype=ctypes.c_int
yuv=ctypes.CDLL('libyuv.so'); baseline=yuv.NV12ToARGBMatrix
baseline.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
baseline.restype=ctypes.c_int
MODES=[(6,0,'kYuvI601Constants'),(6,1,'kYuvJPEGConstants'),(1,0,'kYuvH709Constants'),(1,1,'kYuvF709Constants')]
def sha(data): return hashlib.sha256(data).hexdigest()
def stats(actual,reference):
    assert len(actual)==len(reference)
    output={}
    for i,ch in enumerate('BGRA'):
        delta=[abs(a-b) for a,b in zip(actual[i::4],reference[i::4])]
        output[ch]={'max_abs':max(delta),'mean_abs':sum(delta)/len(delta),'histogram':dict(sorted(collections.Counter(delta).items()))}
    return output
def call(data,w,h,matrix,full,default=False):
    src=ctypes.create_string_buffer(data);dst=ctypes.create_string_buffer(w*h*4)
    if default:
        symbol=next(s for m,f,s in MODES if (m,f)==(matrix,full))
        constants=ctypes.addressof(ctypes.c_uint8.in_dll(yuv,symbol))
        result=baseline(src,w,ctypes.addressof(src)+w*h,w,dst,w*4,constants,w,h)
    else: result=convert(src,len(data),w,h,w,dst,len(dst),w*4,matrix,full)
    assert result==0
    return dst.raw[:w*h*4]
def ffmpeg(data,w,h,count,matrix,full,engine):
    if engine=='zscale':
        filt=f'zscale=matrixin={"709" if matrix==1 else "470bg"}:rangein={"full" if full else "limited"}:matrix=gbr:range=full:filter=point,format=gbrp'
    else:
        filt=f'scale=in_color_matrix={"bt709" if matrix==1 else "bt601"}:in_range={"full" if full else "limited"}:out_range=full:flags=neighbor+accurate_rnd+bitexact+full_chroma_int'
    cmd=['ffmpeg','-hide_banner','-loglevel','error','-f','rawvideo','-pixel_format','nv12','-video_size',f'{w}x{h}','-i','pipe:0','-vf',filt,'-frames:v',str(count),'-f','rawvideo','-pix_fmt','bgra','pipe:1']
    p=subprocess.run(cmd,input=data,capture_output=True,check=True)
    assert len(p.stdout)==w*h*4*count
    return p.stdout,filt
results={'library_sha256':sha(args.library.read_bytes()),'helper_source_sha256':sha((ROOT/'m21s-argb.c').read_bytes()),'byte_order':sys.byteorder,'cases':[]}
assert sys.byteorder=='little','BGRA byte-order claim is little-endian only'
# Each frame has uniform chroma, avoiding any chroma-siting/resampler ambiguity.
# Every Y code is covered, with both legal and super-black/white input samples.
w,h=256,2
pairs=[(u,v) for u in (0,16,64,128,192,240,255) for v in (0,16,64,128,192,240,255)]
frames=[bytes(range(256))*2+bytes([u,v])*(w//2) for u,v in pairs]
joined=b''.join(frames)
for matrix,full,name in MODES:
    actual=b''.join(call(f,w,h,matrix,full) for f in frames)
    original=b''.join(call(f,w,h,matrix,full,True) for f in frames)
    reference,filt=ffmpeg(joined,w,h,len(frames),matrix,full,'zscale')
    measured=stats(actual,reference)
    # One code value is the measured quantization bound on this fixed sweep,
    # not a universal color-management equivalence assertion.
    assert all(measured[ch]['max_abs']<=1 for ch in 'BGR')
    assert measured['A']['max_abs']==0 and set(actual[3::4])=={255}
    swapped=b''.join(actual[i+2:i+3]+actual[i+1:i+2]+actual[i:i+1]+actual[i+3:i+4] for i in range(0,len(actual),4))
    assert max(x['max_abs'] for x in stats(swapped,reference).values())>200
    results['cases'].append({'pattern':'49 uniform UV pairs with full Y sweep','matrix':matrix,'full_range':full,'baseline_constant':name,'width':w,'height':h,'frames':len(frames),'input_sha256':sha(joined),'ffmpeg_filter':filt,'actual_sha256':sha(actual),'reference_sha256':sha(reference),'accurate_helper_vs_ffmpeg':measured,'packaged_constant_vs_ffmpeg':stats(original,reference),'swapped_R_B_negative_control_rejected':True})
# Existing independently software-decoded testsrc2 first frame used in M21S proof.
i420=(ROOT.parent/'hw-driver/sample/first-frame.yuv420p').read_bytes();w,h=320,240
assert len(i420)==w*h*3//2
y=i420[:w*h];u=i420[w*h:w*h*5//4];v=i420[w*h*5//4:]
nv12=y+bytes(c for pair in zip(u,v) for c in pair)
for matrix,full,name in MODES:
    actual=call(nv12,w,h,matrix,full)
    reference,filt=ffmpeg(nv12,w,h,1,matrix,full,'scale')
    measured=stats(actual,reference)
    assert all(measured[ch]['max_abs']<=1 for ch in 'BGR')
    assert measured['A']['max_abs']==0
    results['cases'].append({'pattern':'software decoded testsrc2 first frame','matrix':matrix,'full_range':full,'input_sha256':sha(nv12),'ffmpeg_filter':filt,'actual_sha256':sha(actual),'reference_sha256':sha(reference),'accurate_helper_vs_ffmpeg':measured})
# Source/destination padding and outer guards, including non-vector width.
geometry=[]
for w,h in [(2,2),(258,2),(320,240)]:
    stride=w+16;out_stride=w*4+16
    data=bytes([128])*(stride*h*3//2);src=ctypes.create_string_buffer(data)
    size=out_stride*h;guard=32;raw=ctypes.create_string_buffer(bytes([0xa7])*(size+2*guard),size+2*guard)
    destination=ctypes.addressof(raw)+guard
    assert convert(src,len(data),w,h,stride,destination,size,out_stride,1,0)==0
    assert raw.raw[:guard]==bytes([0xa7])*guard and raw.raw[-guard:]==bytes([0xa7])*guard
    for row in range(h): assert raw.raw[guard+row*out_stride+w*4:guard+(row+1)*out_stride]==bytes([0xa7])*16
    assert src.raw[:len(data)]==data
    geometry.append({'width':w,'height':h,'padding_and_guards_intact':True})
results['geometry']=geometry
# Error exits must precede any destination mutation.
w,h=4,2;src=ctypes.create_string_buffer(64);dst=ctypes.create_string_buffer(bytes([0xa7])*64)
base=[ctypes.addressof(src),12,w,h,w,ctypes.addressof(dst),32,16,1,0]
invalid=[('null_src',0,0,-22),('short_src',1,11,-28),('odd_width',2,3,-22),('zero_height',3,0,-22),('odd_src_stride',4,5,-22),('null_dst',5,0,-22),('short_dst',6,31,-28),('short_dst_stride',7,12,-22),('unknown_matrix',8,2,-95),('unknown_range',9,2,-22),('src_pointer_overflow',0,(1<<(8*ctypes.sizeof(ctypes.c_void_p)))-8,-75),('overlap',5,ctypes.addressof(src)+1,-22)]
checks=[]
for name,index,value,expected in invalid:
    values=base.copy();values[index]=value;before=dst.raw
    actual=convert(*values);assert actual==expected,(name,actual,expected)
    assert dst.raw==before
    checks.append({'case':name,'result':actual,'destination_unchanged':True})
results['invalid_arguments']=checks
results['ffmpeg_version']=subprocess.run(['ffmpeg','-hide_banner','-version'],capture_output=True,text=True,check=True).stdout.splitlines()[0]
results['pass']=True
args.output.write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps({'pass':True,'color_cases':len(results['cases']),'geometry_cases':len(geometry),'invalid_cases':len(checks),'output':str(args.output)}))
