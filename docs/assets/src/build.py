#!/usr/bin/env python3
"""Builds the README's figures into docs/assets/, each in a light and a dark variant.

    swiftc -O textpath.swift -o textpath      (once; macOS only: it uses CoreText)
    python3 build.py

Everything is drawn from the numbers in this file, so a new measurement means: change the number, run this again.
Lettering is Helvetica and Courier converted to outlines by ./textpath, because an SVG shown through <img> cannot
load fonts and would otherwise fall back to whatever the reader's machine has.
"""
import json
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)

THEMES = {
    # the printed page: ink on paper, plus the instrument-marking code used only for data
    "light": dict(ink="#1f2328", muted="#59636e", hair="#d1d9e0", paper="#ffffff",
                  green="#1a7f37", yellow="#bf8700", red="#cf222e", tabred="#cf222e", yellowink="#9a6700"),
    # the instrument face: light markings on the dark ground
    "dark": dict(ink="#f0f6fc", muted="#9198a1", hair="#3d444d", paper="#0d1117",
                 green="#3fb950", yellow="#d29922", red="#f85149", tabred="#da3633", yellowink="#d29922"),
}

HB, H, C, CB = "Helvetica-Bold", "Helvetica", "Courier", "Courier-Bold"


def n(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


class Type:
    """Talks to ./textpath: glyph positions and outlines for a run of text."""

    def __init__(self):
        self.proc = subprocess.Popen([os.path.join(HERE, "textpath")], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, text=True)
        self.outlines, self.cache = {}, {}

    def layout(self, font, text):
        key = (font, text)
        if key not in self.cache:
            self.proc.stdin.write(json.dumps({"font": font, "text": text}) + "\n")
            self.proc.stdin.flush()
            r = json.loads(self.proc.stdout.readline())
            self.outlines.update(r["new"])
            self.cache[key] = r
        return self.cache[key]

    def width(self, text, font, size, track=0.0):
        r = self.layout(font, text)
        return r["width"] * size / 1000 + track * size * max(len(r["glyphs"]) - 1, 0)


T = Type()


class Doc:
    def __init__(self, w, h, theme, label):
        self.w, self.h, self.t, self.label = w, h, THEMES[theme], label
        self.parts, self.used = [], {}

    def add(self, s):
        self.parts.append(s)

    def text(self, x, y, s, font=H, size=14, fill=None, anchor="start", track=0.0):
        fill = fill or self.t["ink"]
        r = T.layout(font, s)
        width = T.width(s, font, size, track)
        x0 = x - (width if anchor == "end" else width / 2 if anchor == "middle" else 0)
        uses = []
        for i, (key, gx) in enumerate(r["glyphs"]):
            if not T.outlines.get(key):
                continue
            gid = self.used.setdefault(key, f"g{len(self.used)}")
            uses.append(f'<use href="#{gid}" x="{n(gx + i * track * 1000)}"/>')
        self.add(f'<g transform="translate({n(x0)} {n(y)}) scale({size / 1000:.5g})" fill="{fill}">{"".join(uses)}</g>')
        return width

    def rect(self, x, y, w, h, fill="none", stroke=None, sw=1.5, r=0, dash=None, extra=""):
        a = f'<rect x="{n(x)}" y="{n(y)}" width="{n(w)}" height="{n(h)}" fill="{fill}"'
        if r:
            a += f' rx="{n(r)}"'
        if stroke:
            a += f' stroke="{stroke}" stroke-width="{n(sw)}"'
        if dash:
            a += f' stroke-dasharray="{dash}"'
        self.add(a + extra + "/>")

    def line(self, x1, y1, x2, y2, stroke=None, sw=1.0, dash=None, cap=None):
        a = f'<line x1="{n(x1)}" y1="{n(y1)}" x2="{n(x2)}" y2="{n(y2)}" stroke="{stroke or self.t["ink"]}" stroke-width="{n(sw)}"'
        if dash:
            a += f' stroke-dasharray="{dash}"'
        if cap:
            a += f' stroke-linecap="{cap}"'
        self.add(a + "/>")

    def leader(self, x1, x2, y, stroke=None):
        """A row of dots between a label and its value, the handbook's way of joining the two."""
        if x2 - x1 > 6:
            self.line(x1, y, x2, y, stroke or self.t["muted"], 1.5, "0.01 5", "round")

    def path(self, d, fill="none", stroke=None, sw=1.5, join="round"):
        a = f'<path d="{d}" fill="{fill}"'
        if stroke:
            a += f' stroke="{stroke}" stroke-width="{n(sw)}" stroke-linejoin="{join}" stroke-linecap="round"'
        self.add(a + "/>")

    def arrow(self, x, y, direction, color, size=7):
        """A solid arrowhead whose tip is at (x, y)."""
        s, hw = size, size * 0.36
        pts = {"right": [(x, y), (x - s, y - hw), (x - s, y + hw)], "left": [(x, y), (x + s, y - hw), (x + s, y + hw)],
               "down": [(x, y), (x - hw, y - s), (x + hw, y - s)], "up": [(x, y), (x - hw, y + s), (x + hw, y + s)]}[direction]
        self.add(f'<polygon points="{" ".join(n(px) + "," + n(py) for px, py in pts)}" fill="{color}"/>')

    def svg(self):
        defs = "".join(f'<path id="{gid}" d="{T.outlines[key]}"/>' for key, gid in self.used.items())
        return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {self.w} {self.h}" width="{self.w}" height="{self.h}" '
                f'role="img" aria-label="{self.label}"><title>{self.label}</title><defs>{defs}</defs>{"".join(self.parts)}</svg>\n')

    def save(self, name):
        with open(os.path.join(OUT, name), "w") as f:
            f.write(self.svg())


# ----------------------------------------------------------------------------------------------- the cover
# Lettering sizes are chosen for a phone: the README column there is about 360 px wide, so an 880-wide cover is shown at
# 0.41 of its size and a 600-wide figure at 0.6. Nothing below is smaller than 10 px after that.
# The handbook is written for the type, every Mac with Apple silicon, and says so in the line under the title. The plate
# below it is the one airframe that has flown: the configuration every number in the handbook was measured on.
PLATE = [("TESTED ON", "MacBook Pro 16-inch, M4 Pro"), ("MEMORY", "24 GB"), ("MACOS", "26.2"),
         ("WINE", "WineHQ 11.0, CrossOver 26.3"), ("GAME", "LIVE 4.10.2"), ("MOD", "sc-offline 0.7.7")]
PLACARDS = ["OFFLINE OPERATION ONLY", "RESEARCH AND STUDY ONLY"]     # the two standing rules, one placard each
ISSUE = "ISSUE 5, 12 NOV 2026"


def cover(theme):
    d = Doc(880, 700, theme, "sc-offline-mac operating handbook: Star Citizen, offline, on Macs with Apple silicon. "
            "Tested on one machine so far: a MacBook Pro 16-inch, M4 Pro, with 24 GB, macOS 26.2, Wine (WineHQ 11.0, CrossOver 26.3), "
            "game LIVE 4.10.2, mod sc-offline 0.7.7. Offline operation only. Research and study only.")
    t = d.t
    ink, red = t["ink"], t["red"]
    d.rect(1, 1, 878, 698, stroke=ink, sw=2)
    d.text(38, 116, "sc-offline-mac", HB, 74, track=-0.02)
    d.text(40, 160, "OPERATING HANDBOOK", HB, 25, track=0.12)
    d.text(40, 208, "Star Citizen, offline,", H, 27)
    d.text(40, 242, "on Macs with Apple silicon", H, 27)
    y = 326
    for label, value in PLATE:
        w = d.text(40, y, label, CB, 24)
        d.text(232, y, value, C, 24)
        d.leader(40 + w + 12, 220, y - 7)
        y += 37
    y = 546
    for words in PLACARDS:
        d.rect(40, y, 466, 56, fill=ink)
        d.text(40 + 233, y + 37, words, HB, 25, fill=t["paper"], anchor="middle", track=0.08)
        y += 66
    d.text(840, 648, ISSUE, C, 24, fill=t["muted"], anchor="end")

    # three-view of the machine tested; its two dimensions are the two numbers the whole kit is about. The page is the
    # same on every Mac with Apple silicon; the memory is this machine's, and the label says so.
    ox = 566                                                                 # front view, lid open
    d.rect(ox + 22, 44, 232, 152, stroke=ink, sw=2, r=10)
    d.rect(ox + 29, 51, 218, 136, stroke=ink, sw=1, r=4)
    d.rect(ox + 124, 51, 28, 8, fill=ink, r=3)
    d.rect(ox, 198, 276, 10, stroke=ink, sw=2, r=5)
    d.path(f"M{ox + 120} 198 Q{ox + 138} 205 {ox + 156} 198", stroke=ink, sw=1.4)
    for x in (ox, ox + 276):                                                 # span = memory, drawn as the red line
        d.line(x, 216, x, 262, red, 1.2)
    d.line(ox + 10, 232, ox + 266, 232, red, 1.8)
    d.arrow(ox + 1, 232, "left", red, 10)
    d.arrow(ox + 275, 232, "right", red, 10)
    d.text(ox + 138, 262, "MEMORY 24 GB", CB, 24, fill=red, anchor="middle")
    d.text(ox + 138, 288, "AS TESTED", CB, 24, fill=red, anchor="middle")
    sx, sy = 700, 484                                                        # side view, with the page as its thickness
    d.rect(sx, sy, 126, 8, stroke=ink, sw=2, r=4)
    d.path(f"M{sx + 4} {sy + 1} L{sx - 24} {sy - 84}", stroke=ink, sw=7)
    d.path(f"M{sx + 4} {sy + 1} L{sx - 24} {sy - 84}", stroke=t["paper"], sw=3)
    d.line(sx + 150, sy - 26, sx + 150, sy - 8, ink, 1.4)
    d.arrow(sx + 150, sy, "down", ink, 9)
    d.line(sx + 150, sy + 16, sx + 150, sy + 30, ink, 1.4)
    d.arrow(sx + 150, sy + 8, "up", ink, 9)
    for yy in (sy, sy + 8):
        d.line(sx + 130, yy, sx + 160, yy, ink, 1)
    d.text(840, sy - 62, "PAGE", CB, 24, anchor="end")
    d.text(840, sy - 36, "16 KB", CB, 24, anchor="end")
    d.save(f"cover-{theme}.svg")


# ------------------------------------------------------------------------------------------ section tabs
def tab(theme, number):
    t = THEMES[theme]
    d = Doc(32, 32, theme, f"Section {number}.")
    emergency = number == 3
    d.rect(0, 0, 32, 32, fill=t["tabred"] if emergency else t["ink"])
    d.text(16, 23.2, str(number), HB, 20, fill="#ffffff" if emergency else t["paper"], anchor="middle")
    d.save(f"tab-{number}-{theme}.svg")


def hatch(theme):
    """The red hatched edge that handbooks print on their emergency pages; used once, under that heading."""
    t = THEMES[theme]
    d = Doc(880, 14, theme, "")
    d.add(f'<defs><pattern id="h" width="14" height="14" patternUnits="userSpaceOnUse" patternTransform="rotate(-45)">'
          f'<rect width="7" height="14" fill="{t["tabred"]}"/></pattern></defs><rect width="880" height="14" fill="url(#h)"/>')
    d.save(f"hatch-{theme}.svg")


# ------------------------------------------------------------------------------- figure 1-1: the systems
W, S = 600, 17      # the figure measure, and the one lettering size used inside figures

LAYERS = [  # (what you provide, its note, [(kit part, what it does)])
    ("STAR CITIZEN", "with the sc-offline mod", [("macfix", "repairs the mod's window handle")]),
    ("WINE", "WineHQ's build or CrossOver", [("winegrid", "4 KB commits stop costing 16 KB"),
                                                   ("vk1wrap", "answers two Windows-only extensions")]),
    ("MOLTENVK", "Vulkan on Metal", [("mvkshim", "presents the GPU, rescues pipelines"),
                                     ("moltenvk patch", "one shared pool of scratch buffers")]),
    ("ROSETTA 2", "the game sees 4 KB pages", []),
    ("MACOS", "16 KB pages, compressor, swap", [("squeezer", "moves cold data out to swap"),
                                                ("sandbox", "no network for the game")]),
    ("MAC", "Apple silicon; tested on M4 Pro, 24 GB", []),
]


def systems(theme):
    head, row, gap = 44, 38, 10
    heights = [head + len(k) * row + (8 if k else 0) for _, _, k in LAYERS]
    h = sum(heights) + (len(LAYERS) - 1) * gap + 74
    d = Doc(W, h, theme, "Figure 1-1. Where each part of the kit sits between the game and the Mac: macfix at the game, "
            "winegrid and vk1wrap in Wine, mvkshim and the MoltenVK patch at MoltenVK, the squeezer and the sandbox in macOS.")
    t = d.t
    y = 1
    for (name, note, kit), bh in zip(LAYERS, heights):
        d.rect(1, y, W - 2, bh, stroke=t["ink"], sw=2)
        d.text(18, y + 29, name, HB, S, track=0.05)
        d.text(W - 18, y + 29, note, H, S, fill=t["muted"], anchor="end")
        for i, (part, does) in enumerate(kit):
            ky = y + head + i * row
            d.rect(18, ky, 176, row - 8, fill=t["ink"])
            d.text(18 + 88, ky + 21, part, CB, S, fill=t["paper"], anchor="middle")
            d.text(208, ky + 21, does, H, S)
        y += bh + gap
    y += 8
    d.rect(1, y, 30, 20, fill=t["ink"])
    d.text(42, y + 16, "built and installed by the scripts here", H, S, fill=t["muted"])
    d.rect(2, y + 31, 28, 18, stroke=t["ink"], sw=2)
    d.text(42, y + 46, "your own copy", H, S, fill=t["muted"])
    d.save(f"fig-1-1-systems-{theme}.svg")


# -------------------------------------------------------------------------- figure 5-1: frame rate by scene
SCENES = [("Spaceport hall", 36, 53), ("Ship terminal screen", 25, 37), ("Hab", 20, 35), ("Hangar, in the seat", 25, 29),
          ("Hangar door open onto the city", 21, 25), ("Flying over the city, night", 18, 21),
          ("Terrace outside the habs, night", 16, 21)]
AS_FOUND = ("Any scene, as found", 3, 6)
TARGET = 25   # the owner's minimum: the green arc starts here
TEST_MACHINE = "MacBook Pro 16-inch, M4 Pro, 24 GB"      # the only machine measured so far; every figure with numbers names it
CONDITIONS = [TEST_MACHINE, "Lean profile, 1728 x 1117, Orison", "Runs m21 to m23, 9 November 2026"]


def performance(theme):
    x0, x1, rh = 20, 580, 52
    key = 42 + 22 * len(CONDITIONS) + 26                 # baseline of the colour key, under the conditions
    top = key + 38
    px = (x1 - x0) / 60
    rows = SCENES + [AS_FOUND]
    bottom = top + len(rows) * rh + 14
    d = Doc(W, bottom + 72, theme, f"Figure 5-1. Frame rate by scene, measured on a {TEST_MACHINE}: " +
            "; ".join(f"{s} {a} to {b}" for s, a, b in rows) + " frames per second. The target is 25 and above.")
    t = d.t
    d.text(x0, 18, "CONDITIONS", HB, S, track=0.05)
    for i, line in enumerate(CONDITIONS):
        d.text(x0, 42 + 22 * i, line, C, S)
    # the arc, straightened: yellow below the target, green from the target up
    tx = x0 + TARGET * px
    d.text(x0, key, "YELLOW: BELOW TARGET", CB, S, fill=t["yellowink"])
    d.text(tx + 8, key, "GREEN: 25 AND ABOVE", CB, S, fill=t["green"])
    d.rect(x0, key + 10, TARGET * px, 9, fill=t["yellow"])
    d.rect(tx, key + 10, (60 - TARGET) * px, 9, fill=t["green"])
    y = top
    for i, (scene, lo, hi) in enumerate(rows):
        found = i == len(rows) - 1
        if found:
            d.line(x0, y + 4, x1, y + 4, t["hair"], 1.5)
            y += 14
        d.text(x0, y + 17, scene, H, S, fill=t["muted"] if found else t["ink"])
        by = y + 26
        for v in range(0, 61, 10):                       # the scale, drawn only behind the bar so it never crosses lettering
            d.line(x0 + v * px, by - 3, x0 + v * px, by + 19, t["hair"], 1.5)
        d.line(tx, by - 3, tx, by + 19, t["green"], 1.5, "3 3")
        bx, bw = x0 + lo * px, (hi - lo) * px
        if found:
            d.rect(bx, by, bw, 16, stroke=t["ink"], sw=1.6, dash="3 2.4")
        else:
            d.rect(bx, by, bw, 16, fill=t["ink"])
        label = f"{lo} to {hi}"
        lw = T.width(label, CB, S)
        lx = bx + bw + 9 if bx + bw + 9 + lw <= x1 + 14 else bx - 9 - lw
        d.rect(lx - 4, by - 2, lw + 8, 20, fill=t["paper"])
        d.text(lx, by + 14, label, CB, S)
        y += rh
    for v in range(0, 61, 10):
        d.text(x0 + v * px, bottom + 24, str(v), C, S, fill=t["muted"], anchor="start" if v == 0 else "end" if v == 60 else "middle")
    d.text(x1, bottom + 54, "FRAMES PER SECOND", HB, S, fill=t["muted"], anchor="end", track=0.05)
    d.save(f"fig-5-1-frame-rate-{theme}.svg")


# ----------------------------------------------------------------------- figure 6-1: the loading graph
def loading(theme):
    x0, x1, limit = 20, 580, 24
    px = (x1 - x0) / 60
    d = Doc(W, 430, theme, "Figure 6-1. Loading graph for the test machine, a Mac with 24 GB installed. As found the game "
            "needed 44 to 59 GB of memory, at 3 to 6 frames per second. With winegrid and the patched MoltenVK it needs 26 to "
            "33 GB, at 10 to 30. With the squeezer 24 GB stays in memory and 10 to 13 GB of data the game never touches "
            "again sits in swap, at 16 to 53.")
    t = d.t
    lx, base = x0 + limit * px, 356
    d.line(lx, 34, lx, base, t["red"], 2.6)            # the red line: what is installed in the machine tested
    d.text(lx + 10, 24, "RED LINE: TEST MACHINE, 24 GB", CB, S, fill=t["red"])
    rows = [("AS FOUND", "3 to 6 FPS", 44, 59, "range", "needs 44 to 59 GB", None),
            ("WASTE REMOVED", "10 to 30 FPS", 26, 33, "range", "needs 26 to 33 GB", None),
            ("COLD DATA MOVED TO SWAP", "16 to 53 FPS", 24, 35.5, "swap", "24 GB in memory", "10 to 13 GB in swap")]
    y = 78
    for title, result, solid, end, kind, inside, after in rows:
        tw = T.width(title, HB, S, 0.05)
        d.rect(x0 - 2, y - 30, tw + 8, 24, fill=t["paper"])           # the red line passes behind the lettering
        d.text(x0, y - 12, title, HB, S, track=0.05)
        rw = T.width(result, CB, S)
        d.rect(x1 - rw - 4, y - 30, rw + 6, 24, fill=t["paper"])
        d.text(x1, y - 12, result, CB, S, anchor="end")
        bh = 34
        d.rect(x0, y, solid * px, bh, fill=t["ink"])
        x, w = x0 + solid * px, (end - solid) * px
        if kind == "range":        # the measured spread: the real figure lies somewhere in here
            d.rect(x, y, w, bh, fill=t["ink"], extra=' fill-opacity="0.38"')
        else:                      # on disk, not in memory
            d.rect(x + 1, y + 1, w - 2, bh - 2, stroke=t["ink"], sw=2, dash="5 3.5")
        d.text(x0 + 12, y + 23, inside, CB, S, fill=t["paper"])
        if after:
            d.text(x + w + 10, y + 23, after, C, S, fill=t["muted"])
        y += 96
    d.line(x0, base, x1, base, t["ink"], 2)
    for v in range(0, 61, 10):
        gx = x0 + v * px
        d.line(gx, base, gx, base + 7, t["ink"], 2)
        d.text(gx, base + 28, str(v), C, S, fill=t["muted"], anchor="start" if v == 0 else "end" if v == 60 else "middle")
    d.text(x1, base + 60, "GB OF MEMORY NEEDED", HB, S, fill=t["muted"], anchor="end", track=0.05)
    d.save(f"fig-6-1-loading-{theme}.svg")


if __name__ == "__main__":
    for theme in THEMES:
        cover(theme)
        for number in range(1, 10):
            tab(theme, number)
        systems(theme)
        performance(theme)
        loading(theme)
        hatch(theme)
    made = sorted(f for f in os.listdir(OUT) if f.endswith(".svg"))
    print(f"{len(made)} figures in {OUT}: {sum(os.path.getsize(os.path.join(OUT, f)) for f in made) // 1024} KB")
