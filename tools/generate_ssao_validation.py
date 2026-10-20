"""Generate original embedded glTF fixtures for raw SSAO acceptance (stdlib only)."""
import base64
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "assets/models/ssao-validation"
ROOT.mkdir(parents=True, exist_ok=True)

def scene(name, corner, tilt=0, thin=False):
    positions = [(-4, 0, -4), (-4, 0, 4), (4, 0, 4), (4, 0, -4)]
    normals = [(0, 1, 0)] * 4
    indices = [0, 1, 2, 0, 2, 3]
    if corner:
        positions += [(0, 0, -4), (0, 2, -4), (0, 2, 4), (0, 0, 4)]
        normals += [(1, 0, 0)] * 4
        indices += [4, 5, 6, 4, 6, 7]
    if thin:
        def front_quad(x0, x1, y0, y1, z):
            start = len(positions)
            positions.extend([(x0,y0,z),(x1,y0,z),(x1,y1,z),(x0,y1,z)])
            normals.extend([(0,0,1)] * 4)
            indices.extend(start+i for i in (0,1,2,0,2,3))
        # Background and strips have equal normals but different depth layers.
        front_quad(-3,3,0,2,-1)
        for x in (-.6,0,.6):
            front_quad(x-.025,x+.025,0,1.6,.7)
    if tilt:
        c,s = math.cos(math.radians(tilt)),math.sin(math.radians(tilt))
        def rotate(v):
            x,y,z=v
            return (c*x-s*y,s*x+c*y,z)
        positions=[rotate(v) for v in positions]
        normals=[rotate(v) for v in normals]
    blob = bytearray()
    views, accessors = [], []
    for values, components, kind, fmt, component_type in [
        ([v for p in positions for v in p], 3, "VEC3", "f", 5126),
        ([v for n in normals for v in n], 3, "VEC3", "f", 5126),
        (indices, 1, "SCALAR", "I", 5125),
    ]:
        encoded = struct.pack("<" + fmt * len(values), *values)
        views.append(dict(buffer=0, byteOffset=len(blob), byteLength=len(encoded)))
        accessors.append(dict(bufferView=len(views)-1, componentType=component_type,
                              count=len(values)//components, type=kind))
        blob.extend(encoded)
    accessors[0].update(min=[min(p[i] for p in positions) for i in range(3)],
                        max=[max(p[i] for p in positions) for i in range(3)])
    document = dict(asset=dict(version="2.0", generator="VulkanRenderingLab SSAO fixture"),
                    scene=0, scenes=[dict(nodes=[0])], nodes=[dict(mesh=0)],
                    meshes=[dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=1), indices=2)])],
                    buffers=[dict(uri="data:application/octet-stream;base64," + base64.b64encode(blob).decode(), byteLength=len(blob))],
                    bufferViews=views, accessors=accessors)
    (ROOT / (name + ".gltf")).write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")

scene("plane", False)
scene("corner", True)

scene("tilted-corner", True, tilt=20)
scene("thin-layers", False, thin=True)
