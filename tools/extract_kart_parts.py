#!/usr/bin/env python3
"""Extract body and four independently rotatable wheels from MKDS kart OBJs."""
import argparse, math, struct
from pathlib import Path
from PIL import Image
from obj_to_psp import swizzle_rgba

p=argparse.ArgumentParser();p.add_argument('obj',type=Path);p.add_argument('--prefix',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--scale',type=float,default=.5);a=p.parse_args()
positions=[];uvs=[];faces=[];material=''
for raw in a.obj.read_text(errors='replace').splitlines():
    f=raw.split()
    if not f:continue
    if f[0]=='v':positions.append(tuple(map(float,f[1:4])))
    elif f[0]=='vt':uvs.append(tuple(map(float,f[1:3])))
    elif f[0]=='usemtl':material=f[1]
    elif f[0]=='f':
        poly=[]
        for item in f[1:]:
            q=item.split('/');poly.append((int(q[0])-1,int(q[1])-1))
        for i in range(1,len(poly)-1):faces.append((material,(poly[0],poly[i],poly[i+1])))

xs,ys,zs=zip(*positions);origin=((min(xs)+max(xs))*.5,min(ys),(min(zs)+max(zs))*.5)
images={name:Image.open(a.obj.parent/f'{name}.png').convert('RGBA') for name in ('kart_body','kart_emblem','kart_tire')}
# The extracted DS tire atlas paints a saturated red/yellow disc on the hub.
# Neutralise only those saturated hub pixels into a metallic grey; tread and
# geometry remain original.
tire_pixels=[]
for r,g,b,alpha in images['kart_tire'].getdata():
    if alpha and r > 90 and (r > b * 1.45 or g > b * 1.45):
        tire_pixels.append((38,42,46,alpha))
    else:tire_pixels.append((r,g,b,alpha))
images['kart_tire'].putdata(tire_pixels)
a.output.mkdir(parents=True,exist_ok=True)

atlas=Image.new('RGBA',(64,32),(0,0,0,0));atlas.alpha_composite(images['kart_body'],(0,0));atlas.alpha_composite(images['kart_emblem'],(32,0))
records=[]
for mat,tri in faces:
    if mat=='kart_tire':continue
    for vi,ti in tri:
        x,y,z=positions[vi];u,v=uvs[ti];u-=math.floor(u);v-=math.floor(v);off=32 if mat=='kart_emblem' else 0
        records.append(struct.pack('<ffIfff',(off+.5+u*31)/64,(.5+(1-v)*31)/32,0xffffffff,(x-origin[0])*a.scale,(y-origin[1])*a.scale,(z-origin[2])*a.scale))
(a.output/f'{a.prefix}_body.mkt').write_bytes(struct.pack('<4sIII',b'MKA4',len(records),24,(64<<16)|32)+b''.join(records))
(a.output/f'{a.prefix}_body.rgba').write_bytes(swizzle_rgba(atlas.tobytes(),64,32))

tire_faces=[tri for mat,tri in faces if mat=='kart_tire'];groups={(sx,sz):[] for sx in (-1,1) for sz in (-1,1)}
for tri in tire_faces:
    cx=sum(positions[v][0] for v,t in tri)/3;cz=sum(positions[v][2] for v,t in tri)/3
    groups[(1 if cx>=origin[0] else -1,1 if cz>=origin[2] else -1)].append(tri)
for index,key in enumerate(((-1,-1),(1,-1),(-1,1),(1,1))):
    group=groups[key];used={v for tri in group for v,t in tri};pts=[positions[v] for v in used]
    center=tuple((min(q[d] for q in pts)+max(q[d] for q in pts))*.5 for d in range(3));rec=[]
    for tri in group:
        for vi,ti in tri:
            x,y,z=positions[vi];u,v=uvs[ti];u-=math.floor(u);v-=math.floor(v)
            rec.append(struct.pack('<ffIfff',u,1-v,0xffffffff,(x-center[0])*a.scale,(y-center[1])*a.scale,(z-center[2])*a.scale))
    stem=f'{a.prefix}_wheel_{index}';image=images['kart_tire']
    (a.output/f'{stem}.mkt').write_bytes(struct.pack('<4sIII',b'MKA4',len(rec),24,(image.width<<16)|image.height)+b''.join(rec))
    (a.output/f'{stem}.rgba').write_bytes(swizzle_rgba(image.tobytes(),image.width,image.height))
    place=tuple((center[d]-origin[d])*a.scale for d in range(3));radius=(max(q[1] for q in pts)-min(q[1] for q in pts))*.5*a.scale
    print(a.prefix,index,'position',*(round(q,6) for q in place),'radius',round(radius,6))
