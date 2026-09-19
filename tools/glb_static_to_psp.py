from pathlib import Path
import argparse, struct, sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/_anim_vendor"))
import numpy as np
from PIL import Image
from pygltflib import GLTF2
from obj_to_psp import swizzle_rgba

DTYPES={5121:np.uint8,5123:np.uint16,5125:np.uint32,5126:np.float32}
SIZES={"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4}

def convert(source,destination,texture_output,scale):
    gltf=GLTF2().load_binary(str(source)); blob=gltf.binary_blob()
    def acc(i):
        a=gltf.accessors[i]; v=gltf.bufferViews[a.bufferView]; dt=np.dtype(DTYPES[a.componentType]).newbyteorder('<')
        return np.frombuffer(blob,dtype=dt,count=a.count*SIZES[a.type],offset=(v.byteOffset or 0)+(a.byteOffset or 0)).reshape(a.count,SIZES[a.type])
    primitive=gltf.meshes[0].primitives; positions=acc(primitive[0].attributes.POSITION).astype(np.float32)
    center=(positions.min(0)+positions.max(0))*.5; center[1]=positions[:,1].min()
    images=[Image.open(source.parent/item.uri).convert('RGBA') for item in gltf.images]
    width=sum(image.width for image in images); height=max(image.height for image in images)
    # PSP swizzling requires a multiple of 4 pixels horizontally and 8 vertically.
    width=(width+3)&~3; height=(height+7)&~7
    atlas=Image.new('RGBA',(width,height),(0,0,0,0)); offsets=[]; x=0
    for image in images: offsets.append(x); atlas.alpha_composite(image,(x,0)); x += image.width
    texture_output.write_bytes(swizzle_rgba(atlas.tobytes(),width,height))
    records=[]
    for p in primitive:
        pos=acc(p.attributes.POSITION).astype(np.float32); uv=acc(p.attributes.TEXCOORD_0); indices=acc(p.indices).reshape(-1)
        material=gltf.materials[p.material]
        texture_index=material.pbrMetallicRoughness.baseColorTexture.index
        image_index=gltf.textures[texture_index].source
        image=images[image_index]; offset=offsets[image_index]
        for index in indices:
            x,y,z=(pos[index]-center)*scale; u,v=uv[index]
            au=(offset+.5+max(0,min(1,float(u)))*(image.width-1))/width
            av=(.5+max(0,min(1,float(v)))*(image.height-1))/height
            records.append(struct.pack('<ffIfff',au,av,0xffffffff,x,y,z))
    destination.write_bytes(struct.pack('<4sIII',b'MKA4',len(records),24,(width<<16)|height)+b''.join(records))
    print(f'Wrote {len(records)} vertices')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path)
    p.add_argument('--texture-output',type=Path,required=True);p.add_argument('--scale',type=float,default=.5);a=p.parse_args()
    convert(a.source,a.destination,a.texture_output,a.scale)
