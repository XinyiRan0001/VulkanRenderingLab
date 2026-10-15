"""Generate original, redistributable static glTF/GLB loader fixtures (stdlib only)."""
import base64
import copy
import json
import math
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1] / 'assets/models/gltf-validation'
ROOT.mkdir(parents=True, exist_ok=True)

def png(name, pixel):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    scanlines = b''.join(b'\0' + b''.join(bytes(pixel(x, y)) for x in range(32)) for y in range(32))
    (ROOT / name).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 32, 32, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))

png('base.png', lambda x, y: (210, 170, 100, 255) if (x//4+y//4)%2 else (80, 110, 170, 255))
def normal(x, y):
    nx, ny = 0.45*math.sin(x*math.pi/8), 0.45*math.sin(y*math.pi/8)
    return tuple(round(127.5*(v+1)) for v in (nx, ny, math.sqrt(1-nx*nx-ny*ny))) + (255,)
png('normal.png', normal)
png('mr.png', lambda x, y: (255, 50 + x*6, 255 if y < 16 else 0, 255))
png('ao.png', lambda x, y: (100 + x*5,)*3 + (255,))
png('emissive.png', lambda x, y: (100, 30, 5, 255) if x%8 < 2 else (0,0,0,255))

# Each cube face has independent UVs, normals and tangent handedness.
positions, normals, tangents, uvs, indices = [], [], [], [], []
for n, t in [((0,0,1),(1,0,0)), ((0,0,-1),(-1,0,0)), ((1,0,0),(0,0,-1)), ((-1,0,0),(0,0,1)), ((0,1,0),(1,0,0)), ((0,-1,0),(1,0,0))]:
    b = (n[1]*t[2]-n[2]*t[1], n[2]*t[0]-n[0]*t[2], n[0]*t[1]-n[1]*t[0])
    start = len(positions)//3
    for u,v in [(0,0),(1,0),(1,1),(0,1)]:
        positions.extend((n[i]+(2*u-1)*t[i]+(2*v-1)*b[i])*0.5 for i in range(3))
        normals.extend(n); tangents.extend((*t,1)); uvs.extend((u,v))
    indices.extend(start+i for i in (0,1,2,0,2,3))

blob = bytearray(); views = []; accessors = []
for values, components, kind, fmt, component_type in [(positions,3,'VEC3','f',5126),(normals,3,'VEC3','f',5126),(uvs,2,'VEC2','f',5126),(tangents,4,'VEC4','f',5126),(indices,1,'SCALAR','H',5123)]:
    while len(blob)%4: blob.append(0)
    encoded = struct.pack('<'+fmt*len(values), *values)
    views.append(dict(buffer=0, byteOffset=len(blob), byteLength=len(encoded)))
    accessors.append(dict(bufferView=len(views)-1, componentType=component_type, count=len(values)//components, type=kind))
    if values is positions: accessors[-1].update(min=[-.5]*3,max=[.5]*3)
    blob.extend(encoded)
material = dict(pbrMetallicRoughness=dict(baseColorFactor=[0.8,0.9,1,1],baseColorTexture=dict(index=0),metallicFactor=0.8,roughnessFactor=0.9,metallicRoughnessTexture=dict(index=2)), normalTexture=dict(index=1,scale=0.7),occlusionTexture=dict(index=3,strength=0.6),emissiveTexture=dict(index=4),emissiveFactor=[0.3,0.2,0.1])
document = dict(asset=dict(version='2.0',generator='VulkanRenderingLab validation generator'),scene=0,scenes=[dict(nodes=[0])],nodes=[dict(translation=[0,0.8,0],children=[1,2,3]),dict(mesh=0,translation=[-1.1,0,0]),dict(mesh=0,scale=[0.7,1.3,0.7]),dict(mesh=0,translation=[1.1,0,0],scale=[-0.7,0.7,0.7])],meshes=[dict(primitives=[dict(attributes=dict(POSITION=0,NORMAL=1,TEXCOORD_0=2,TANGENT=3),indices=4,material=0)])],materials=[material],images=[dict(uri=n+'.png') for n in ('base','normal','mr','ao','emissive')],textures=[dict(source=i) for i in range(5)],buffers=[dict(uri='scene.bin',byteLength=len(blob))],bufferViews=views,accessors=accessors)
# A second material and primitive test per-draw descriptor selection.
document['materials'].append(dict(pbrMetallicRoughness=dict(baseColorFactor=[0.1,0.6,0.9,1],metallicFactor=0.1,roughnessFactor=0.3)))
second_mesh = copy.deepcopy(document['meshes'][0])
second_mesh['primitives'][0]['material'] = 1
document['meshes'].append(second_mesh)
document['nodes'][3]['mesh'] = 1
(ROOT/'scene.bin').write_bytes(blob)
(ROOT/'scene.gltf').write_text(json.dumps(document,indent=2))
glb = copy.deepcopy(document); del glb['buffers'][0]['uri']
for image in glb['images']:
    while len(blob)%4: blob.append(0)
    image_bytes = (ROOT/image.pop('uri')).read_bytes()
    glb['bufferViews'].append(dict(buffer=0,byteOffset=len(blob),byteLength=len(image_bytes)))
    image.update(bufferView=len(glb['bufferViews'])-1,mimeType='image/png')
    blob.extend(image_bytes)
glb['buffers'][0]['byteLength'] = len(blob)
while len(blob)%4: blob.append(0)
js = json.dumps(glb,separators=(',',':')).encode()
js += b' '*((-len(js))%4)
(ROOT/'scene.glb').write_bytes(struct.pack('<III',0x46546C67,2,28+len(js)+len(blob))+struct.pack('<I4s',len(js),b'JSON')+js+struct.pack('<I4s',len(blob),b'BIN\0')+blob)
for name, mutate in [
    ('unsupported-alpha',lambda d:d['materials'][0].update(alphaMode='BLEND')),
    ('missing-tangents',lambda d:d['meshes'][0]['primitives'][0]['attributes'].pop('TANGENT')),
    ('singular-transform',lambda d:d['nodes'][1].update(scale=[0,1,1])),
    ('invalid-index',lambda d:d['accessors'][4].update(count=999999))]:
    invalid = copy.deepcopy(document); mutate(invalid)
    (ROOT/(name+'.gltf')).write_text(json.dumps(invalid))
