#!/usr/bin/env python3
"""STEP-Geometrie mit Herstellerfarben ohne Grafikserver als PNG rendern.

Abhängigkeiten: cadquery==2.7.0, numpy, Pillow.
Aufruf aus dem Repository: python hardware/boards/render.py
"""
from pathlib import Path
import json

import cadquery as cq
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TDocStd import TDocStd_Document
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDF import TDF_Label, TDF_LabelSequence
from OCP.XCAFDoc import XCAFDoc_DocumentTool
from OCP.IFSelect import IFSelect_RetDone
from OCP.Interface import Interface_Static
from cadquery.occ_impl.importers.assembly import _get_shape_color

ROOT = Path(__file__).resolve().parent


def read_step(path):
    """Baugruppen flach lesen; mehrfach gleiche Bauteilnamen sind erlaubt."""
    reader = STEPCAFControl_Reader()
    reader.SetColorMode(True)
    reader.SetNameMode(False)
    reader.SetLayerMode(False)
    reader.SetSHUOMode(False)
    Interface_Static.SetIVal_s("read.stepcaf.subshapes.name", 0)
    if reader.ReadFile(str(path)) != IFSelect_RetDone:
        raise ValueError(f"STEP-Datei nicht lesbar: {path}")
    doc = TDocStd_Document(TCollection_ExtendedString("XmlXCAF"))
    if not reader.Transfer(doc):
        raise ValueError(f"STEP-Import fehlgeschlagen: {path}")
    tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    color_tool = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())
    roots = TDF_LabelSequence()
    tool.GetFreeShapes(roots)
    parts = []

    def walk(label, location):
        if tool.IsReference_s(label):
            target = TDF_Label()
            tool.GetReferredShape_s(label, target)
            walk(target, location * cq.Location(tool.GetLocation_s(label)))
        elif tool.IsAssembly_s(label):
            children = TDF_LabelSequence()
            tool.GetComponents_s(label, children)
            for child in children:
                walk(child, location)
        else:
            shape = tool.GetShape_s(label)
            if not shape.IsNull():
                color = _get_shape_color(shape, color_tool)
                parts.append((cq.Shape.cast(shape).moved(location), color))

    for root in roots:
        walk(root, cq.Location())
    return parts


def mesh(assembly):
    triangles, colors = [], []
    for index, (shape, color) in enumerate(assembly):
        vertices, faces = shape.tessellate(0.035, 0.15)
        if not faces:
            continue
        triangles.append(np.array([v.toTuple() for v in vertices])[faces])
        rgb = color.toTuple()[:3] if color else (0.65, 0.68, 0.72)
        colors.extend([rgb] * len(faces))
        if index % 20 == 0:
            print(f"Trianguliert: {index + 1}/{len(assembly)} Bauteile", flush=True)
    return np.concatenate(triangles), np.array(colors)


def view(triangles, colors, direction, size=1000, vertical=(0, 1, 0)):
    forward = np.array(direction, dtype=float)
    forward /= np.linalg.norm(forward)
    right = np.cross(vertical, forward)
    right /= np.linalg.norm(right)
    up = np.cross(forward, right)
    points = triangles @ np.array([right, up, forward]).T
    lo, hi = points.min(axis=(0, 1)), points.max(axis=(0, 1))
    scale = 0.86 * size / max((hi - lo)[:2])
    points[:, :, :2] = (points[:, :, :2] - (lo + hi)[:2] / 2) * scale + size / 2
    points[:, :, 1] = size - points[:, :, 1]
    normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    normals /= np.maximum(lengths[:, None], 1e-12)
    light = forward + np.array([-0.35, 0.4, 0.5])
    light /= np.linalg.norm(light)
    shade = 0.42 + 0.58 * np.abs(normals @ light)
    rgb = np.uint8(np.clip(colors * shade[:, None] * 255, 0, 255))
    pixels = np.full((size, size, 3), (242, 245, 248), dtype=np.uint8)
    depth = np.full((size, size), -np.inf)
    for tri, col in zip(points, rgb):
        x0, y0 = np.maximum(np.floor(tri[:, :2].min(axis=0)).astype(int), 0)
        x1, y1 = np.minimum(np.ceil(tri[:, :2].max(axis=0)).astype(int), size - 1)
        if x1 < x0 or y1 < y0:
            continue
        a, b, c = tri
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-10:
            continue
        y, x = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        x, y = x + 0.5, y + 0.5
        u = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / den
        v = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / den
        w = 1 - u - v
        z = u * a[2] + v * b[2] + w * c[2]
        region = depth[y0:y1 + 1, x0:x1 + 1]
        mask = (u >= -1e-7) & (v >= -1e-7) & (w >= -1e-7) & (z > region)
        region[mask] = z[mask]
        pixels[y0:y1 + 1, x0:x1 + 1][mask] = col
    return Image.fromarray(pixels)


def main(paths=None):
    report = ROOT / "geometry-check.json"
    results = json.loads(report.read_text()) if report.exists() else {}
    for path in paths or sorted(ROOT.glob("*.step")):
        print(f"Importiere {path.name}", flush=True)
        assembly = read_step(path)
        print(f"Importiert: {len(assembly)} Bauteile", flush=True)
        compound = cq.Compound.makeCompound([shape for shape, _ in assembly])
        bounds = compound.BoundingBox()
        triangles, colors = mesh(assembly)
        results[path.name] = {
            "parts": len(assembly),
            "solids": len(compound.Solids()),
            "triangles": len(triangles),
            "bounds_mm": [round(v, 4) for v in (bounds.xlen, bounds.ylen, bounds.zlen)],
        }
        assert len(triangles) and len(compound.Solids()), path
        print(f"Rastere {len(triangles)} Dreiecke", flush=True)
        # XIAO liegt im Herstellerkoordinatensystem in der XZ-Ebene.
        axis = "Y" if "xiao" in path.stem else "Z"
        directions = ((0.65, -0.85, 1.5), (-0.65, -0.85, -1.5))
        vertical = (0, 1, 0)
        if axis == "Y":
            directions = ((0.65, 1.5, 0.85), (-0.65, -1.5, 0.85))
            vertical = (0, 0, 1)
        # Zwei gegenüberliegende Ansichten zeigen beide Bestückungsseiten.
        canvas = Image.new("RGB", (2000, 1100), (242, 245, 248))
        for i, direction in enumerate(directions):
            canvas.paste(view(triangles, colors, direction, vertical=vertical), (i * 1000, 60))
        draw = ImageDraw.Draw(canvas)
        font = ImageFont.load_default(size=25)
        draw.text((40, 18), path.stem, fill=(25, 35, 48), font=font)
        draw.text((40, 1060), f"Ansicht +{axis}", fill=(55, 65, 78), font=font)
        draw.text((1040, 1060), f"Ansicht -{axis}", fill=(55, 65, 78), font=font)
        canvas.save(path.with_suffix(".png"))
        print(results[path.name], flush=True)
    report.write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", type=Path, help="Optional: einzelne STEP-Dateien")
    main(parser.parse_args().files)
