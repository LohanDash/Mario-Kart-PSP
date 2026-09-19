from pathlib import Path
import math
import struct

from PIL import Image
from obj_to_psp import swizzle_rgba

ROOT=Path(__file__).resolve().parents[1]
base=ROOT/'data/karts/standard_mr/Standard MR'
source=base/'kart_MR_a.obj'
positions=[]; texcoords=[]; faces=[]; material=''
for raw in source.read_text(errors='replace').splitlines():
    fields=raw.split()
    if not fields: continue
    if fields[0]=='v': positions.append(tuple(map(float,fields[1:4])))
    elif fields[0]=='vt': texcoords.append(tuple(map(float,fields[1:3])))
    elif fields[0]=='usemtl': material=fields[1]
    elif fields[0]=='f' and material=='kart_tire':
        face=[]
        for item in fields[1:]:
            parts=item.split('/'); face.append((int(parts[0])-1,int(parts[1])-1))
        for i in range(1,len(face)-1): faces.append((face[0],face[i],face[i+1]))

xs=[p[0] for p in positions]; ys=[p[1] for p in positions]; zs=[p[2] for p in positions]
global_center=((min(xs)+max(xs))*.5,min(ys),(min(zs)+max(zs))*.5)
groups={(sx,sz):[] for sx in (-1,1) for sz in (-1,1)}
for face in faces:
    cx=sum(positions[v][0] for v,t in face)/3; cz=sum(positions[v][2] for v,t in face)/3
    groups[(1 if cx>=0 else -1,1 if cz>=0 else -1)].append(face)

image=Image.open(base/'kart_tire.png').convert('RGBA')
rgba=swizzle_rgba(image.tobytes(),image.width,image.height)
out=ROOT/'data/karts/standard_mr'
for index,key in enumerate(((-1,-1),(1,-1),(-1,1),(1,1))):
    group=groups[key]; used={v for f in group for v,t in f}; pts=[positions[v] for v in used]
    center=tuple((min(p[a] for p in pts)+max(p[a] for p in pts))*.5 for a in range(3))
    records=[]
    for face in group:
        for vi,ti in face:
            x,y,z=positions[vi];u,v=texcoords[ti]
            records.append(struct.pack('<ffIfff',u,1-v,0xffffffff,
                (x-center[0])*.5,(y-center[1])*.5,(z-center[2])*.5))
    name=f'wheel_{index}'
    (out/f'{name}.mkt').write_bytes(struct.pack('<4sIII',b'MKA4',len(records),24,
        (image.width<<16)|image.height)+b''.join(records))
    (out/f'{name}.rgba').write_bytes(rgba)
    placed=((center[0]-global_center[0])*.5,(center[1]-global_center[1])*.5,
            (center[2]-global_center[2])*.5)
    print(index,key,len(records),tuple(round(v,6) for v in placed))

# Build the complete red Standard MR from the same OBJ.  Body, emblem and tires
# share one compact atlas; keeping this as one static original mesh is much more
# reliable than reconstructing or independently rotating its four wheels.
body_image=Image.open(base/'kart_body.png').convert('RGBA')
emblem_image=Image.open(base/'kart_emblem.png').convert('RGBA')
tire_image=Image.open(base/'kart_tire.png').convert('RGBA')
atlas=Image.new('RGBA',(128,32),(0,0,0,0))
atlas.paste(body_image,(0,0)); atlas.paste(emblem_image,(32,0)); atlas.paste(tire_image,(64,0))
body_records=[]
material=''
for raw in source.read_text(errors='replace').splitlines():
    fields=raw.split()
    if not fields: continue
    if fields[0]=='usemtl': material=fields[1]
    elif fields[0]=='f' and material in ('kart_body','kart_emblem','kart_tire'):
        face=[]
        for item in fields[1:]:
            parts=item.split('/'); face.append((int(parts[0])-1,int(parts[1])-1))
        for i in range(1,len(face)-1):
            for vi,ti in (face[0],face[i],face[i+1]):
                x,y,z=positions[vi]; u,v=texcoords[ti]
                # The DS tire material repeats its small texture.  Once packed
                # into an atlas it must wrap inside its own region, otherwise
                # it samples the body/emblem or transparent pixels.
                u -= math.floor(u)
                v -= math.floor(v)
                offset={'kart_body':0,'kart_emblem':32,'kart_tire':64}[material]
                texture_width=64 if material=='kart_tire' else 32
                atlas_u=(offset+0.5+u*(texture_width-1))/128.0
                atlas_v=(0.5+(1.0-v)*31.0)/32.0
                body_records.append(struct.pack('<ffIfff',atlas_u,atlas_v,0xffffffff,
                    (x-global_center[0])*.5,(y-global_center[1])*.5,(z-global_center[2])*.5))
(out/'kart.mkt').write_bytes(struct.pack('<4sIII',b'MKA4',len(body_records),24,(128<<16)|32)+b''.join(body_records))
(out/'kart.rgba').write_bytes(swizzle_rgba(atlas.tobytes(),128,32))
print('complete kart',len(body_records),'exact Standard MR red atlas')
