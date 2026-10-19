"""Generate original embedded glTF fixtures for raw SSAO acceptance (stdlib only)."""
import base64
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "assets/models/ssao-validation"
ROOT.mkdir(parents=True, exist_ok=True)

def scene(name, corner):
    positions = [(-4, 0, -4), (-4, 0, 4), (4, 0, 4), (4, 0, -4)]
    normals = [(0, 1, 0)] * 4
    indices = [0, 1, 2, 0, 2, 3]
    if corner:
        positions += [(0, 0, -4), (0, 2, -4), (0, 2, 4), (0, 0, 4)]
        normals += [(1, 0, 0)] * 4
        indices += [4, 5, 6, 4, 6, 7]
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
    accessors[0].update(min=[-4, 0, -4], max=[4, 2 if corner else 0, 4])
    document = dict(asset=dict(version="2.0", generator="VulkanRenderingLab SSAO fixture"),
                    scene=0, scenes=[dict(nodes=[0])], nodes=[dict(mesh=0)],
                    meshes=[dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=1), indices=2)])],
                    buffers=[dict(uri="data:application/octet-stream;base64," + base64.b64encode(blob).decode(), byteLength=len(blob))],
                    bufferViews=views, accessors=accessors)
    (ROOT / (name + ".gltf")).write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")

scene("plane", False)
scene("corner", True)
