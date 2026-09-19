#!/usr/bin/env python3
"""Quick painter preview of the exact MKT geometry and swizzled texture."""
import argparse, math, struct
from pathlib import Path
from PIL import Image, ImageDraw

def unswizzle(data,w,h):
    out=bytearray(w*h*4); k=0
    for y in range(0,h,8):
        for x in range(0,w*4,16):
            for row in range(8):
                start=(y+row)*w*4+x; out[start:start+16]=data[k:k+16]; k+=16
    return bytes(out)

p=argparse.ArgumentParser();p.add_argument('model',type=Path);p.add_argument('output',type=Path);p.add_argument('--side',action='store_true');a=p.parse_args()
b=a.model.read_bytes();magic,count,stride,dims=struct.unpack_from('<4sIII',b);w,h=dims>>16,dims&65535
verts=[struct.unpack_from('<ffIfff',b,16+i*stride) for i in range(count)]
t=Image.frombytes('RGBA',(w,h),unswizzle(a.model.with_suffix('.rgba').read_bytes(),w,h))
out=Image.new('RGB',(640,480),(70,100,140));draw=ImageDraw.Draw(out)
tris=[]
for i in range(0,count,3):
    tri=verts[i:i+3]; projected=[]; depth=0
    for u,v,c,x,y,z in tri:
        if a.side: x,z=z,-x
        d=z+2.0; depth+=d
        projected.append((320+x/d*650,400-y/d*650,u,v))
    uc=sum(q[2] for q in projected)/3;vc=sum(q[3] for q in projected)/3
    color=t.getpixel((max(0,min(w-1,int(uc*w))),max(0,min(h-1,int(vc*h)))))[:3]
    tris.append((depth,[(q[0],q[1]) for q in projected],color))
for _,points,color in sorted(tris,reverse=True): draw.polygon(points,fill=color,outline=(20,20,20))
out.save(a.output)
