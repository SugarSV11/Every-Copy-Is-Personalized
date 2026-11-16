#!/usr/bin/env python3
"""
Generates src/integration/living_models.inc.c: vertices, display lists and collision
for the AI's own level pieces, built from axis-aligned "slabs":

  BLOCK  a solid 200 x 200 x 200 block (origin bottom centre), wall texture on the
         sides, floor texture on top
  ROOM   a small building, 1040 x 1040 footprint: floor, 4 walls with a doorway on
         the -Z side, ceiling and a walkable roof

Textures are NOT stored in the ROM by these models: every gsDPSetTextureImage points
at segment 0x09, the texture bank the current level loaded (grass, snow, castle
inside, ...). The integration layer patches the offsets per bank at area load, so
the pieces always wear the textures of the level they appear in.

Collision uses the same 8-vertex / 12-triangle cube layout (and winding) as SM64's
breakable box, scaled per slab, so every slab is a closed solid.

Usage: python tools/gen_living_models.py   (writes the .inc.c next to living_sm64.c)
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "integration", "living_models.inc.c")

WALL, FLOOR, ROOF = 0, 1, 2
TEX_UNITS = 128  # world units per texture repeat
SHADE = {"top": 0xFF, "bottom": 0x70, "x": 0xC8, "z": 0xA8}

# breakable_box collision template: corners at +-100 / 0..200
BOX_VERTS = [(-1, 0, -1), (-1, 0, 1), (-1, 1, 1), (1, 0, 1), (1, 1, 1), (1, 0, -1), (1, 1, -1), (-1, 1, -1)]
BOX_TRIS = [(0, 1, 2), (1, 3, 4), (1, 4, 2), (5, 3, 1), (5, 1, 0), (6, 4, 3), (6, 3, 5),
            (7, 4, 6), (7, 2, 4), (0, 2, 7), (7, 6, 5), (7, 5, 0)]


def slab_faces(x0, x1, y0, y1, z0, z1, top_tex=WALL):
    """6 quads (4 vertices each, counter-clockwise seen from outside) + texture slot."""
    faces = []
    # +Y top, -Y bottom
    faces.append((top_tex, "top", [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)], ("x", "z")))
    faces.append((WALL, "bottom", [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], ("x", "z")))
    # +X, -X
    faces.append((WALL, "x", [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], ("z", "y")))
    faces.append((WALL, "x", [(x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0)], ("z", "y")))
    # +Z, -Z
    faces.append((WALL, "z", [(x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (x0, y0, z1)], ("x", "y")))
    faces.append((WALL, "z", [(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)], ("x", "y")))
    return faces


def tc(v, axes):
    m = {"x": v[0], "y": v[1], "z": v[2]}
    # 10.5 fixed point texels; 32-texel texture repeats every TEX_UNITS units
    return int(m[axes[0]] * 32 * 32 / TEX_UNITS), int(-m[axes[1]] * 32 * 32 / TEX_UNITS)


def emit_model(name, slabs):
    faces = []
    for s in slabs:
        faces += slab_faces(*s)
    vtx, dl = [], []
    patch = {}
    for slot in (WALL, FLOOR, ROOF):
        group = [f for f in faces if f[0] == slot]
        if not group:
            continue
        patch[slot] = len(dl)
        dl.append("    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 0x09000000),")
        dl.append("    gsDPLoadSync(),")
        dl.append("    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES)),")
        for _slot, kind, quad, axes in group:
            base = len(vtx)
            c = SHADE[kind]
            for v in quad:
                s, t = tc(v, axes)
                vtx.append(f"    {{{{{{ {v[0]}, {v[1]}, {v[2]} }}, 0, {{ {s}, {t} }}, {{ 0x{c:02X}, 0x{c:02X}, 0x{c:02X}, 0xFF }}}}}},")
            dl.append(f"    gsSPVertex({name}_vtx + {base}, 4, 0),")
            dl.append("    gsSP2Triangles(0, 1, 2, 0x0, 0, 2, 3, 0x0),")
    # collision
    cv, ct = [], []
    for (x0, x1, y0, y1, z0, z1, *_rest) in slabs:
        b = len(cv)
        for (sx, sy, sz) in BOX_VERTS:
            cv.append((x0 if sx < 0 else x1, y0 if sy == 0 else y1, z0 if sz < 0 else z1))
        ct += [(b + a, b + c, b + d) for (a, c, d) in BOX_TRIS]

    out = []
    out.append(f"static Vtx {name}_vtx[] = {{")
    out += vtx
    out.append("};")
    out.append(f"/* texture image commands to patch per level bank: wall, floor, roof (-1 = unused) */")
    out.append(f"#define {name.upper()}_TEXCMD_WALL {patch.get(WALL, -1) + 8}")
    out.append(f"#define {name.upper()}_TEXCMD_FLOOR {patch.get(FLOOR, -1) + 8 if FLOOR in patch else -1}")
    out.append(f"#define {name.upper()}_TEXCMD_ROOF {patch.get(ROOF, -1) + 8 if ROOF in patch else -1}")
    out.append(f"static Gfx {name}_dl[] = {{")
    out.append("    gsDPPipeSync(),")
    out.append("    gsDPSetCombineMode(G_CC_MODULATERGB, G_CC_MODULATERGB),")
    out.append("    gsSPClearGeometryMode(G_LIGHTING),")
    out.append("    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD),")
    out.append("    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),")
    out.append("    gsDPTileSync(),")
    out.append("    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0, G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD),")
    out.append("    gsDPSetTileSize(0, 0, 0, (32 - 1) << G_TEXTURE_IMAGE_FRAC, (32 - 1) << G_TEXTURE_IMAGE_FRAC),")
    out += dl
    out.append("    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),")
    out.append("    gsDPPipeSync(),")
    out.append("    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),")
    out.append("    gsSPSetGeometryMode(G_LIGHTING),")
    out.append("    gsSPEndDisplayList(),")
    out.append("};")
    out.append(f"static const Collision {name}_collision[] = {{")
    out.append("    COL_INIT(),")
    out.append(f"    COL_VERTEX_INIT({len(cv)}),")
    out += [f"    COL_VERTEX({x}, {y}, {z})," for (x, y, z) in cv]
    out.append(f"    COL_TRI_INIT(SURFACE_DEFAULT, {len(ct)}),")
    out += [f"    COL_TRI({a}, {b}, {c})," for (a, b, c) in ct]
    out.append("    COL_TRI_STOP(),")
    out.append("    COL_END(),")
    out.append("};")
    return out


def main():
    block = [(-100, 100, 0, 200, -100, 100, FLOOR)]
    W, T, H, DOOR_W, DOOR_H = 500, 60, 600, 160, 380
    room = [
        (-W, W, 0, 12, -W, W, FLOOR),                      # floor
        (-W, W, 0, H, W - T, W, WALL),                     # back wall (+Z)
        (-W, -W + T, 0, H, -W, W, WALL),                   # left wall
        (W - T, W, 0, H, -W, W, WALL),                     # right wall
        (-W, -DOOR_W, 0, H, -W, -W + T, WALL),             # front wall, left of the door
        (DOOR_W, W, 0, H, -W, -W + T, WALL),               # front wall, right of the door
        (-DOOR_W, DOOR_W, DOOR_H, H, -W, -W + T, WALL),    # lintel over the door
        (-W - 20, W + 20, H, H + 40, -W - 20, W + 20, ROOF),  # roof (walkable)
    ]
    lines = ["/* GENERATED by tools/gen_living_models.py - do not edit by hand */"]
    lines += emit_model("living_block", block)
    lines += emit_model("living_room", room)
    with open(OUT, "w", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
