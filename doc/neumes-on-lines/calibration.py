#!/usr/bin/env python3
"""Check where calligraphic neumes land on a staff against the measured facsimile examples.

For every row of calibration.csv this renders the family's MEI file with verovio
(--neume-calligraphic --xml-id-seed 1), finds the <nc>'s ink in the SVG (the <g id="..."> verovio
writes for each nc), measures it against the staff lines of its own staff segment, and compares the
measurement with the target. Python 3 standard library only.

Units: one step = half the distance between two adjacent staff lines. The expected pitch height
comes from the MEI (clef in effect + pname/oct), placed on the rendered staff lines.

Checks (all in steps, "offset" positive = above the pitch):
  centre        offset of the middle of the ink's vertical extent (dots, squares, waists)
  head          offset of the top of the ink lowered by half a nib (a stroke whose pitch is its
                upper end: virga head, arch top, pes head)
  foot          offset of the bottom of the ink raised by half a nib (a stroke whose pitch is its
                lower end: clivis descent, pes foot, quilisma belly)
  extent_above  how far the ink reaches above the pitch (flag, corner, epiphonus overshoot)
  extent_below  how far the ink reaches below the pitch (virga stem, cephalicus overshoot)
A row passes when |measured - target| <= tol.

Half a nib (default 0.23 step = 3 of the 13 pen px of a step, half the default 6 px nib) stands for
the ink that lies beyond the centreline at a stroke's end; tolerances in calibration.csv absorb the
rest (the nib angle makes the end of a stroke a slanted butt).

A connected gesture is cut into per-nc slices that overlap by a few spine knots (kMargin in
CalligraphicNeume::InkRun), so the ink of one nc is taken as its slice minus the overlap: the slice's
first/last --margin-knots edge knots are dropped at an end that is shared with another nc's slice.

Usage: calibration.py [--verovio PATH] [--resources DIR] [--family NAME ...] [--fails-only]
                      [--keep-svg DIR] [--hand TOKEN]
Exit status: 0 if every row passes, 1 if any row fails, 2 on a usage or render error.
"""

import argparse
import csv
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
MEI_NS = 'http://www.music-encoding.org/ns/mei'
XML_ID = '{http://www.w3.org/XML/1998/namespace}id'
SVG_NS = 'http://www.w3.org/2000/svg'
STEPS = 'cdefgab'


# ---------------------------------------------------------------------------------------------
# MEI: the staff position of every nc (steps above the bottom line), following clef changes
# ---------------------------------------------------------------------------------------------

def diatonic(pname, octv):
    return int(octv) * 7 + STEPS.index(pname)


def clef_bottom_line(clef):
    """Diatonic index of the pitch on the bottom staff line under this clef."""
    shape, line = clef.get('shape'), int(clef.get('line'))
    ref = {'C': diatonic('c', 4), 'F': diatonic('f', 3), 'G': diatonic('g', 4)}[shape]
    dis = clef.get('dis')
    if dis:
        shift = {'8': 7, '15': 14, '22': 21}[dis]
        ref += shift if clef.get('dis.place') == 'above' else -shift
    return ref - 2 * (line - 1)


def nc_positions(mei_path):
    """{nc xml:id: staff position in steps above the bottom line}."""
    root = ET.parse(mei_path).getroot()
    pos = {}
    bottom = None
    for el in root.iter():
        tag = el.tag.split('}')[-1]
        if tag == 'clef':
            bottom = clef_bottom_line(el)
        elif tag == 'nc' and el.get(XML_ID) and el.get('pname'):
            if bottom is None:
                raise ValueError(f'{mei_path}: nc {el.get(XML_ID)} before any clef')
            pos[el.get(XML_ID)] = diatonic(el.get('pname'), el.get('oct')) - bottom
    return pos


# ---------------------------------------------------------------------------------------------
# SVG: staff lines and per-nc ink
# ---------------------------------------------------------------------------------------------

NUM = r'-?\d+(?:\.\d+)?'


def parse_path(d):
    """Absolute M/L/C/Z path -> list of anchors and list of segments (p0, c1, c2, p1)."""
    toks = re.findall(r'[MLCZmlcz]|' + NUM, d)
    anchors, segs = [], []
    i, cmd, cur, start = 0, None, None, None
    while i < len(toks):
        t = toks[i]
        if t.isalpha():
            if t.islower():
                raise ValueError('relative path commands are not handled: ' + d[:40])
            cmd = t
            i += 1
            if cmd == 'Z':
                if cur is not None and start is not None and cur != start:
                    segs.append((cur, cur, start, start))
                cur = start
            continue
        if cmd == 'M':
            cur = start = (float(toks[i]), float(toks[i + 1]))
            anchors.append(cur)
            i += 2
            cmd = 'L'
        elif cmd == 'L':
            p = (float(toks[i]), float(toks[i + 1]))
            segs.append((cur, cur, p, p))
            cur = p
            anchors.append(p)
            i += 2
        elif cmd == 'C':
            c1 = (float(toks[i]), float(toks[i + 1]))
            c2 = (float(toks[i + 2]), float(toks[i + 3]))
            p = (float(toks[i + 4]), float(toks[i + 5]))
            segs.append((cur, c1, c2, p))
            cur = p
            anchors.append(p)
            i += 6
        else:
            raise ValueError('unexpected path data: ' + d[:40])
    if len(anchors) > 1 and anchors[-1] == anchors[0]:
        anchors.pop()
    return anchors, segs


def sample(seg, n=8):
    p0, c1, c2, p1 = seg
    out = []
    for k in range(n + 1):
        t = k / n
        u = 1 - t
        out.append((u ** 3 * p0[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t ** 3 * p1[0],
                    u ** 3 * p0[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t ** 3 * p1[1]))
    return out


def own_ink_points(path_d, shared, margin):
    """Points on the outline of one nc's own ink: the slice minus the overlap with its neighbours.

    A slice is [left[a]..left[b], right[b]..right[a]] (CalligraphicNeume::SliceRibbon). An end whose
    first edge knot also belongs to another nc's slice overlaps it by `margin` knots, which are
    dropped. Paths that are not slices (dots, single strokes) are taken whole."""
    anchors, segs = parse_path(path_d)
    n = len(anchors)
    whole = [p for s in segs for p in sample(s)]
    if margin <= 0 or n % 2 or n < 4:
        return whole
    half = n // 2
    start_shared = anchors[0] in shared
    end_shared = anchors[half - 1] in shared
    if not (start_shared or end_shared):
        return whole
    lo = margin if start_shared else 0
    hi = half - 1 - (margin if end_shared else 0)
    if hi <= lo:
        return whole

    def knot(idx):  # relative knot index of anchor idx along the spine
        return idx if idx < half else n - 1 - idx

    keep = {i for i in range(n) if lo <= knot(i) <= hi}
    index = {a: i for i, a in enumerate(anchors)}
    pts = []
    for s in segs:
        i0, i1 = index.get(s[0]), index.get(s[3])
        if i0 in keep and i1 in keep:
            pts.extend(sample(s))
    return pts or whole


def svg_measure(svg_path, ids, margin):
    """{nc id: (ink top y, ink bottom y, bottom line y, step)}; missing ids are left out."""
    tree = ET.parse(svg_path)
    parent = {c: p for p in tree.iter() for c in p}
    by_id = {el.get('id'): el for el in tree.iter() if el.get('id')}

    def cls(el):
        return (el.get('class') or '').split()

    def ancestor(el, name):
        while el is not None and name not in cls(el):
            el = parent.get(el)
        return el

    def paths(el):
        return [p.get('d') for p in el.iter(f'{{{SVG_NS}}}path') if p.get('d')]

    out = {}
    for i in ids:
        el = by_id.get(i)
        if el is None:
            continue
        staff = ancestor(el, 'staff')
        neume = ancestor(el, 'neume')
        ys = []
        for p in staff.findall(f'{{{SVG_NS}}}path'):
            m = re.match(r'M\s*(' + NUM + r')\s+(' + NUM + r')\s+L\s*(' + NUM + r')\s+(' + NUM + r')$',
                         p.get('d', '').strip())
            if m and m.group(2) == m.group(4):
                ys.append(float(m.group(2)))
        ys = sorted(set(ys))
        if not ys:
            continue
        step = (ys[-1] - ys[0]) / (2 * (len(ys) - 1)) if len(ys) > 1 else None
        shared = set()
        if neume is not None:
            for other in neume.iter(f'{{{SVG_NS}}}g'):
                if 'nc' in cls(other) and other is not el:
                    for d in paths(other):
                        shared.update(parse_path(d)[0])
        pts = []
        for d in paths(el):
            pts.extend(own_ink_points(d, shared, margin))
        if not pts:
            out[i] = None
            continue
        out[i] = (min(p[1] for p in pts), max(p[1] for p in pts), ys[-1], step)
    return out


# ---------------------------------------------------------------------------------------------

def measure(kind, top, bottom, pitch_y, step, half_nib):
    if kind == 'centre':
        return (pitch_y - (top + bottom) / 2) / step
    if kind == 'head':
        return (pitch_y - top) / step - half_nib
    if kind == 'foot':
        return (pitch_y - bottom) / step + half_nib
    if kind == 'extent_above':
        return (pitch_y - top) / step
    if kind == 'extent_below':
        return (bottom - pitch_y) / step
    raise ValueError('unknown check ' + kind)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--verovio', default='/home/user/verovio-adiastematic/build/verovio')
    ap.add_argument('--resources', default='/home/user/verovio-adiastematic/data')
    ap.add_argument('--csv', default=os.path.join(HERE, 'calibration.csv'))
    ap.add_argument('--family', nargs='*', help='only these families')
    ap.add_argument('--hand', help='pass --neume-hand TOKEN to verovio (overrides each file)')
    ap.add_argument('--half-nib', type=float, default=0.23, help='half a nib in steps (default 0.23)')
    ap.add_argument('--margin-knots', type=int, default=4,
                    help='slice overlap in edge knots (kMargin in InkRun; default 4, 0 = whole slices)')
    ap.add_argument('--step', type=float, default=90.0,
                    help='SVG units per step on a one-line staff (default 90: unit 9 at the default scale)')
    ap.add_argument('--fails-only', action='store_true', help='print only failing rows')
    ap.add_argument('--keep-svg', help='write the rendered SVGs to this directory')
    args = ap.parse_args()

    rows = list(csv.DictReader(open(args.csv, encoding='utf-8')))
    families = []
    for r in rows:
        if r['family'] not in families and (not args.family or r['family'] in args.family):
            families.append(r['family'])
    if not os.path.exists(args.verovio):
        print(f'verovio binary not found: {args.verovio}', file=sys.stderr)
        return 2

    outdir = args.keep_svg or tempfile.mkdtemp(prefix='neume-calibration-')
    os.makedirs(outdir, exist_ok=True)
    n_pass = n_fail = 0
    per_family = {}
    for fam in families:
        mei = os.path.join(HERE, fam + '.mei')
        svg = os.path.join(outdir, fam + '.svg')
        cmd = [args.verovio, '--neume-calligraphic', '--xml-id-seed', '1', '-r', args.resources,
               '-o', svg, mei]
        if args.hand:
            cmd[1:1] = ['--neume-hand', args.hand]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0 or not os.path.exists(svg):
            print(f'{fam}: verovio failed\n{res.stdout}{res.stderr}', file=sys.stderr)
            return 2
        pos = nc_positions(mei)
        fam_rows = [r for r in rows if r['family'] == fam]
        ink = svg_measure(svg, {r['nc_id'] for r in fam_rows}, args.margin_knots)
        fp = ff = 0
        print(f'== {fam}')
        for r in fam_rows:
            i, kind = r['nc_id'], r['check']
            target, tol = float(r['target_steps']), float(r['tol_steps'])
            got = ink.get(i)
            if i not in pos or got is None:
                status, val = 'FAIL', None
                note = 'nc not found in the MEI' if i not in pos else 'no ink found in the SVG'
            else:
                top, bottom, bottom_line, step = got
                step = step or args.step
                pitch_y = bottom_line - pos[i] * step
                val = measure(kind, top, bottom, pitch_y, step, args.half_nib)
                status = 'PASS' if abs(val - target) <= tol + 1e-9 else 'FAIL'
                note = ''
            if status == 'PASS':
                fp += 1
            else:
                ff += 1
            if status == 'FAIL' or not args.fails_only:
                shown = f'{val:+.2f}' if val is not None else '  n/a'
                print(f'{status}  {i:<24} {r["pitch"]:<4} {kind:<13} measured {shown}  target '
                      f'{target:+.2f} +-{tol:.2f}  {r["pitch_point"]}{"  [" + note + "]" if note else ""}')
        per_family[fam] = (fp, ff)
        n_pass += fp
        n_fail += ff
    print()
    for fam, (fp, ff) in per_family.items():
        print(f'{fam:<16} {fp:>4} pass {ff:>4} fail')
    print(f'{"total":<16} {n_pass:>4} pass {n_fail:>4} fail')
    if not args.keep_svg:
        for f in os.listdir(outdir):
            os.remove(os.path.join(outdir, f))
        os.rmdir(outdir)
    return 1 if n_fail else 0


if __name__ == '__main__':
    sys.exit(main())
