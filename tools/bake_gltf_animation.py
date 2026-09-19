from pathlib import Path
import json, math, struct, sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/_anim_vendor"))
import numpy as np
from PIL import Image
from pygltflib import GLTF2
from obj_to_psp import swizzle_rgba

SOURCE = ROOT / "assets/animation_work/mario_animated_glb/P_MR.glb"
OUT = ROOT / "data/characters/mario"
SCALE = 0.4

gltf = GLTF2().load_binary(str(SOURCE))
blob = gltf.binary_blob()

DTYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16,
          5125: np.uint32, 5126: np.float32}
SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

def accessor(index):
    a = gltf.accessors[index]; view = gltf.bufferViews[a.bufferView]
    dtype = np.dtype(DTYPES[a.componentType]).newbyteorder("<")
    count = SIZES[a.type]
    offset = (view.byteOffset or 0) + (a.byteOffset or 0)
    if view.byteStride and view.byteStride != dtype.itemsize * count:
        rows = [np.frombuffer(blob, dtype=dtype, count=count,
                              offset=offset + i * view.byteStride) for i in range(a.count)]
        result = np.asarray(rows)
    else:
        result = np.frombuffer(blob, dtype=dtype, count=a.count * count, offset=offset).reshape(a.count, count)
    if a.normalized:
        if np.issubdtype(dtype, np.unsignedinteger): result = result.astype(np.float32) / np.iinfo(dtype).max
        else: result = np.maximum(result.astype(np.float32) / np.iinfo(dtype).max, -1.0)
    return result

def quat_matrix(q):
    x,y,z,w = q / max(np.linalg.norm(q), 1e-8)
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),0],
                     [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),0],
                     [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),0],
                     [0,0,0,1]], dtype=np.float32)

def local_matrix(node, overrides=None):
    if node.matrix: return np.array(node.matrix, dtype=np.float32).reshape(4,4).T
    t=np.array(node.translation or [0,0,0],dtype=np.float32)
    r=np.array(node.rotation or [0,0,0,1],dtype=np.float32)
    s=np.array(node.scale or [1,1,1],dtype=np.float32)
    if overrides:
        t=overrides.get("translation",t); r=overrides.get("rotation",r); s=overrides.get("scale",s)
    m=quat_matrix(r); m[:3,:3] *= s[np.newaxis,:]; m[:3,3]=t
    return m

parents = {}
for i,n in enumerate(gltf.nodes):
    for c in n.children or []: parents[c]=i

def globals_for(overrides):
    cache={}
    def calc(i):
        if i not in cache:
            local=local_matrix(gltf.nodes[i],overrides.get(i))
            cache[i]=calc(parents[i]) @ local if i in parents else local
        return cache[i]
    return [calc(i) for i in range(len(gltf.nodes))]

skin=gltf.skins[0]
inverse_bind=accessor(skin.inverseBindMatrices).reshape(-1,4,4).transpose(0,2,1)
mesh_node=next(i for i,n in enumerate(gltf.nodes) if n.mesh is not None)

primitives=[]
for primitive in gltf.meshes[gltf.nodes[mesh_node].mesh].primitives:
    attrs=primitive.attributes
    primitives.append((accessor(attrs.POSITION).astype(np.float32),
                       accessor(attrs.TEXCOORD_0).astype(np.float32),
                       accessor(attrs.JOINTS_0).astype(np.int32),
                       accessor(attrs.WEIGHTS_0).astype(np.float32),
                       accessor(primitive.indices).reshape(-1).astype(np.int32), primitive.material))

def sample_sampler(anim, sampler, time, is_rotation=False):
    times=accessor(sampler.input).reshape(-1); values=accessor(sampler.output)
    if time <= times[0]: return values[0]
    if time >= times[-1]: return values[-1]
    hi=int(np.searchsorted(times,time)); lo=hi-1
    f=float((time-times[lo])/(times[hi]-times[lo]))
    first=values[lo]; second=values[hi]
    if is_rotation and np.dot(first,second) < 0.0: second=-second
    value=first*(1-f)+second*f
    if is_rotation: value /= max(np.linalg.norm(value),1e-8)
    return value

def bake_animation(name, frame_count):
    anim=next(a for a in gltf.animations if a.name==name)
    duration=max(float(accessor(s.input).reshape(-1)[-1]) for s in anim.samplers)
    frames=[]
    for frame in range(frame_count):
        time=duration*frame/frame_count
        overrides={}
        for channel in anim.channels:
            target=channel.target
            overrides.setdefault(target.node,{})[target.path]=sample_sampler(
                anim,anim.samplers[channel.sampler],time,target.path=="rotation")
        global_mats=globals_for(overrides); mesh_inv=np.linalg.inv(global_mats[mesh_node])
        skin_mats=[mesh_inv @ global_mats[joint] @ inverse_bind[i] for i,joint in enumerate(skin.joints)]
        records=[]
        for positions,uvs,joints,weights,indices,material in primitives:
            transformed=[]
            for vi,p in enumerate(positions):
                hp=np.array([p[0],p[1],p[2],1],dtype=np.float32); result=np.zeros(4,dtype=np.float32)
                for k in range(4): result += weights[vi,k] * (skin_mats[joints[vi,k]] @ hp)
                transformed.append(result[:3])
            frames.append((transformed,uvs,indices,material))
        # regroup the primitives belonging to this one animation frame
    grouped=[]
    per=len(primitives)
    for i in range(frame_count): grouped.append(frames[i*per:(i+1)*per])
    return grouped

drive=bake_animation("P_MR_drive",29)
win=bake_animation("P_MR_win",100)
all_frames=drive+win
base=np.concatenate([np.asarray(p[0]) for p in all_frames[0]],axis=0)
center_x=(base[:,0].min()+base[:,0].max())*.5; center_z=(base[:,2].min()+base[:,2].max())*.5; floor=base[:,1].min()

atlas=Image.new("RGBA",(128,32),(0,0,0,0))
face=Image.open(SOURCE.parent/"P_face_1.png").convert("RGBA"); body=Image.open(SOURCE.parent/"P_main.png").convert("RGBA")
atlas.alpha_composite(face,(0,0)); atlas.alpha_composite(body,(32,0))
(OUT/"mario_anim.rgba").write_bytes(swizzle_rgba(atlas.tobytes(),128,32))

packed=[]; vertex_count=None
for frame in all_frames:
    records=[]
    for transformed,uvs,indices,material in frame:
        ox,tw=(0,32) if material==0 else (32,64)
        for index in indices:
            p=transformed[index]; u,v=uvs[index]
            u=max(0.0,min(1.0,float(u))); v=max(0.0,min(1.0,float(v)))
            au=(ox+.5+u*(tw-1))/128.0; av=(.5+v*31)/32.0
            records.append(struct.pack("<ffIfff",au,av,0xffffffff,
                (p[0]-center_x)*SCALE,(p[1]-floor)*SCALE,(p[2]-center_z)*SCALE))
    vertex_count=vertex_count or len(records)
    assert len(records)==vertex_count
    packed.extend(records)

header=struct.pack("<4sIIIII",b"MKA5",len(all_frames),vertex_count,24,(128<<16)|32,len(drive))
(OUT/"mario_anim.mka").write_bytes(header+b"".join(packed))
print(f"Wrote {len(all_frames)} frames x {vertex_count} vertices")
