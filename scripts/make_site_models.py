#!/usr/bin/env python3
"""The website's 3D models (docs/assets/models/clock_tft.glb, clock_oled.glb) from the case's assembly STEP files.

hardware/enclosure/enclosure.py --assembly writes step/assembly_tft.step and assembly_oled.step: the closed clock with
the board and its parts, the display and both headers, the speaker and the case, each a named and coloured part.
This meshes each one, writes it as binary glTF (the case's frame, in metres: X right, Y up, Z toward the viewer), and
compresses it with gltfpack (meshopt, quantized; named nodes and materials kept, which the site's viewer needs to take
the case apart and name the parts). About 0.5 MB each.

Run it with CadQuery's OCP (Python 3.10 to 3.12) and Node.js (for gltfpack), after the assembly files changed:

    uv run --python 3.12 --with cadquery python scripts/make_site_models.py
"""
import os
import subprocess
import sys
import tempfile

from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.IFSelect import IFSelect_RetDone
from OCP.Message import Message_ProgressRange
from OCP.RWGltf import RWGltf_CafWriter
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TCollection import TCollection_AsciiString, TCollection_ExtendedString
from OCP.TColStd import TColStd_IndexedDataMapOfStringString
from OCP.TDF import TDF_LabelSequence
from OCP.TDocStd import TDocStd_Document
from OCP.XCAFDoc import XCAFDoc_DocumentTool

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STEP_DIR = os.path.join(ROOT, 'hardware', 'enclosure', 'step')
OUT_DIR = os.path.join(ROOT, 'docs', 'assets', 'models')
LINEAR_MM, ANGULAR_RAD = 0.08, 0.35   # meshing: fine enough for the star grille, small enough for the web
GLTFPACK = ['npx', '-y', 'gltfpack@0.22.0']


def step_to_glb(src, dst):
    doc = TDocStd_Document(TCollection_ExtendedString('MDTV-XCAF'))
    reader = STEPCAFControl_Reader()
    reader.SetColorMode(True)
    reader.SetNameMode(True)
    if reader.ReadFile(src) != IFSelect_RetDone or not reader.Transfer(doc):
        sys.exit(f'cannot read {src}')
    shapes = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    labels = TDF_LabelSequence()
    shapes.GetFreeShapes(labels)
    for i in range(1, labels.Length() + 1):
        BRepMesh_IncrementalMesh(shapes.GetShape_s(labels.Value(i)), LINEAR_MM, False, ANGULAR_RAD, True)
    if not RWGltf_CafWriter(TCollection_AsciiString(dst), True).Perform(
            doc, TColStd_IndexedDataMapOfStringString(), Message_ProgressRange()):
        sys.exit(f'cannot write {dst}')


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for display in ('tft', 'oled'):
            src = os.path.join(STEP_DIR, f'assembly_{display}.step')
            if not os.path.exists(src):
                sys.exit(f'{src} is missing: run hardware/enclosure/enclosure.py --assembly first')
            raw = os.path.join(tmp, f'{display}.glb')
            out = os.path.join(OUT_DIR, f'clock_{display}.glb')
            step_to_glb(src, raw)
            subprocess.run(GLTFPACK + ['-i', raw, '-o', out, '-cc', '-kn', '-km', '-vp', '14'], check=True)
            print(f'wrote {os.path.relpath(out, ROOT)} ({os.path.getsize(out) // 1024} KB)')


if __name__ == '__main__':
    main()
