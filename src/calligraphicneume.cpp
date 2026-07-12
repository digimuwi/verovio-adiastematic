/////////////////////////////////////////////////////////////////////////////
// Name:        calligraphicneume.cpp
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "calligraphicneume.h"

//----------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

//----------------------------------------------------------------------------

#include "atttypes.h"

namespace vrv {

//----------------------------------------------------------------------------
// Prototype constants (in pen pixels) - kept verbatim from the reference renderer
//----------------------------------------------------------------------------

namespace {

    // The vertical grid the whole gesture is built on. Adiastematic neumes are heightless, but they read
    // far more clearly when every component lands on a consistent set of vertical levels instead of
    // drifting by the accident of its stroke angle. One LEVEL is one interline - the gap between two staff
    // lines, 2 verovio drawing units (= 2 * s_unitPx pen px), the currency verovio naturally thinks in.
    // Each melodic stroke spans exactly one level from foot to head at its own angle, so however vertical
    // and diagonal strokes are mixed, an up-stroke then a down-stroke always returns to the level it left:
    // the foot/head levels stay aligned across the neume. @rellen scales the reach (see RellenFactor): l
    // lengthens it and s shortens it by the same ratio. The horizontal advance follows from the stroke's
    // @tilt aspect, so a diagonal that reaches one level is correspondingly longer than a vertical one but
    // keeps its 45-degree pen angle (and so its broad-nib thickness).
    constexpr double LEVEL_STEP = 2.0 * CalligraphicNeume::s_unitPx; // one interline
    constexpr double MED = LEVEL_STEP; // a one-level stroke; also the curl-radius reference below
    // Placement of a detached component (@con == g). A pitch-changing step moves along the melodic contour
    // (@intm), advancing past the previous stroke's forward reach so ascending (salicus) / descending
    // (climacus) components stack without colliding; a repeated same-pitch stroke instead slides tight
    // sideways (a bivirga). Adiastematic sources are heightless, so these are conventions, not intervals.
    constexpr double BREAK_GAP = 15.0; // pen-lift advance past a detached component's clearance
    constexpr double REPEAT_DX = 14.0; // horizontal slide of a repeated same-pitch stroke (bivirga, distropha)
    constexpr double NESTLE_GAP = 7.0; // perpendicular clearance between two @place-stacked parallel strokes
                                       // (a hairline above the broad nib, NIB_W = 6, so they still read apart)
    // Liquescent curl radii. A liquescent that carries its own melodic stroke ends in a small terminal
    // flourish; one with no @tilt is the whole note rendered as a hook off the previous nc, so its curl
    // is note-sized rather than a tiny ornament.
    constexpr double LIQ_CURL_R = MED * 0.30; // terminal curl at the tip of a liquescent's own stroke
    constexpr double LIQ_HOOK_R = MED * 0.50; // a whole-note hook (no lead-in stroke): note-sized
    // The little cursive loop of a looped connection (@con="l"): the pen winds a small crossing loop of
    // this radius at the joint between two components (Old Hispanic). Sized well under the liquescent's
    // terminal flourish so the eye stays a small ink-choked knot (the manuscript counter is about one
    // nib-width across) and the loop reads as a joint, not a note.
    constexpr double LOOP_JOIN_R = MED * 0.18;
    // Sideways nudge for a plain stroke that about-faces (travels back along its predecessor's line, e.g.
    // an s after an n): without it the two strokes retrace each other and collapse into one. Sized a touch
    // above the broad nib (NIB_W = 6) so the parallel pair reads as two distinct strokes, not one mass.
    constexpr double ANTIPARALLEL_SHIFT = 8.0;
    // Direction tests on the dot product of two unit travel vectors. A pair pointing nearly opposite
    // (dot <= ANTIPARALLEL_COS) about-faces and needs the sideways nudge above; a pair pointing nearly
    // the same way (dot >= PARALLEL_COS) is a same-pitch repeat (a bivirga) that slides tight sideways.
    constexpr double ANTIPARALLEL_COS = -0.9;
    constexpr double PARALLEL_COS = 0.9;

    constexpr double DEG = M_PI / 180.0;
    // The broad nib is held at a fixed 45-degree diagonal for the whole hand, its edge running NE-SW. A
    // stroke travelling along that edge (a virga: bottom -> NE) comes out thin; one travelling across it
    // (the start of a pes: -> SE) comes out broad. Pen space has +y down, so NE is (+x, -y).
    constexpr double NIB_ANGLE = -45.0 * DEG;
    constexpr double NIB_W = 6.0; // broadest (across the nib)
    constexpr double NIB_MIN = 1.8; // thinnest (along the nib)
    constexpr double EPISEMA_NIB = 0.55; // episemata are drawn with a finer nib than the note strokes
    // The scribe's episema is not a ruled bar: drawn freehand it bows into a shallow concave-up dish, a
    // low middle with the ends flicking up. Sized as a fraction of the accent's half-length.
    constexpr double EPISEMA_BOW = 0.30;
    // Chained accents (several stacked on one nc - a clivis foot's horizontal stroke crossed by an
    // upright) are drawn nearly straight, so the stepped shape reads as ruled strokes meeting squarely.
    constexpr double EPISEMA_BOW_CHAIN = 0.07;

    // How many levels a stroke reaches, from @rellen. The reach scales symmetrically about a normal stroke
    // (1.0): a long one (l) and a short one (s) are reciprocals of one ratio (3:2), so the normal reach is
    // their exact geometric mean. 3/2 keeps a long stroke a clear step above normal without the dramatic
    // doubling a leap (2.0) would give.
    double RellenFactor(bool longStroke, bool shortStroke)
    {
        return longStroke ? 3.0 / 2.0 : (shortStroke ? 2.0 / 3.0 : 1.0);
    }

    // @intm is a logical (melodic) attribute, but when @tilt is absent it supplies a sensible default
    // direction: u (up) -> ne, d (down) -> se. An explicit @tilt always wins.
    int IntmDefaultTilt(char intm)
    {
        if (intm == 'u') return COMPASSDIRECTION_ne;
        if (intm == 'd') return COMPASSDIRECTION_se;
        return COMPASSDIRECTION_NONE;
    }

    // The direction the pen jumps to lay down the next *detached* component (@con="g"), following the
    // melodic contour (@intm): u rises (ne), d falls (se), s stays level (e).
    int IntmGapTilt(char intm)
    {
        if (intm == 'u') return COMPASSDIRECTION_ne;
        if (intm == 'd') return COMPASSDIRECTION_se;
        if (intm == 's') return COMPASSDIRECTION_e;
        return COMPASSDIRECTION_NONE;
    }

    // The initial swing of an @s-shape: the compass direction the pen first moves as it starts the S.
    // Per the MEI definition this orients the letterform and its reflections - "w" the standard letter S
    // (initial swing west), "e" its left-right mirror (east), "s" the S turned 90° anti-clockwise
    // (south), "n" that mirror (north). The wave's wiggle is opened toward this direction, so the SIGN of
    // the swing is what tells a letterform from its mirror (w vs e, s vs n) and its axis tells an upright
    // S from one turned on its side. Pen space has +y down. Empty / unknown defaults to the standard S.
    CalligraphicNeume::PointF SShapeSwing(const std::string &orient)
    {
        if (orient == "e") return { 1.0, 0.0 };
        if (orient == "s") return { 0.0, 1.0 };
        if (orient == "n") return { 0.0, -1.0 };
        return { -1.0, 0.0 }; // "w" and default
    }

    // A rotated S ("s" / "n") lies on its side: its spine runs level rather than upright, and it reads a
    // touch longer and shallower than the upright S ("w" / "e"). Used to pick the default travel axis (when
    // no @tilt steers it) and the length / amplitude tuning.
    bool SShapeRotated(const std::string &orient)
    {
        return orient == "s" || orient == "n";
    }

    //----------------------------------------------------------------------------
    // Scribal ductus: the forward (italic) slant
    //----------------------------------------------------------------------------

    // The unit pull axis of a right hand: up and to the right. Pen space has +y down, so "up" is -y; the
    // diagonal (1, -1)/sqrt(2) is the direction the hand naturally drags the pen.
    constexpr CalligraphicNeume::PointF PULL_AXIS = { 0.7071067811865476, -0.7071067811865476 };

    // The local strength of the forward slant for a stroke travelling in direction @p t. A stroke aligned
    // with the pull (up-right, alignment +1) leans fully; one running against it (a descent, alignment
    // -1) leans by only (1 - bias); a directionless dab takes the neutral middle. This anisotropy is what
    // keeps a firm downstroke from leaning as drunkenly as the up-right flick.
    double SlantStrength(CalligraphicNeume::PointF t, double bias)
    {
        const double m = std::hypot(t.x, t.y);
        const double a = (m > 1e-9) ? (t.x * PULL_AXIS.x + t.y * PULL_AXIS.y) / m : 0.0;
        return 1.0 - bias * (1.0 - a) / 2.0;
    }

    // Shear one centreline point travelling in direction @p t toward the upper-right. Pen space +y is
    // down, so x -= strength * tan * y pulls points above the baseline (y < 0) right and lets points
    // below it drift left - the forward lean - by a direction-dependent amount.
    CalligraphicNeume::PointF SlantShear(
        CalligraphicNeume::PointF p, CalligraphicNeume::PointF t, CalligraphicNeume::Slant s)
    {
        return { p.x - SlantStrength(t, s.bias) * s.tan * p.y, p.y };
    }

    // Shear a whole centreline in place, taking each point's travel direction from its neighbours (the
    // same central difference the nib sweep uses). Because the tangent varies continuously along a
    // densified spine, so does the lean - even a sharp joint between an up-stroke and a down-stroke is
    // sheared without a seam.
    void SlantApply(std::vector<CalligraphicNeume::PointF> &pts, CalligraphicNeume::Slant s)
    {
        using PointF = CalligraphicNeume::PointF;
        const int n = (int)pts.size();
        if (n == 0) return;
        // Shear in place: SlantShear only moves a point's x, and the per-point tangent comes from the
        // ORIGINAL neighbours, so carry the previous point's pre-shear value forward (the point ahead is
        // still untouched as we sweep left-to-right) - no scratch copy of the whole spine is needed.
        PointF prev = pts[0];
        for (int i = 0; i < n; ++i) {
            const PointF cur = pts[i];
            const PointF a = (i > 0) ? prev : cur;
            const PointF b = pts[std::min(n - 1, i + 1)];
            pts[i] = SlantShear(cur, { b.x - a.x, b.y - a.y }, s);
            prev = cur;
        }
    }

} // namespace

//----------------------------------------------------------------------------
// Vector / curve primitives
//----------------------------------------------------------------------------

CalligraphicNeume::PointF CalligraphicNeume::TiltVec(int tilt)
{
    switch (tilt) {
        case COMPASSDIRECTION_n: return { 0.0, -1.0 };
        case COMPASSDIRECTION_ne: return { 0.72, -0.69 };
        case COMPASSDIRECTION_e: return { 1.0, 0.0 };
        case COMPASSDIRECTION_se: return { 0.69, 0.72 };
        case COMPASSDIRECTION_s: return { 0.0, 1.0 };
        case COMPASSDIRECTION_sw: return { -0.69, 0.72 };
        case COMPASSDIRECTION_w: return { -1.0, 0.0 };
        case COMPASSDIRECTION_nw: return { -0.72, -0.69 };
        default: return { 0.0, 0.0 };
    }
}

int CalligraphicNeume::EffectiveTilt(const NcInfo &nc)
{
    if (nc.tilt != COMPASSDIRECTION_NONE) return nc.tilt; // an explicit @tilt always wins
    // A quilisma is always the whole wavy note; it runs level, so it defaults to e rather than taking
    // the @intm direction (u would otherwise tilt it ne). The ascent of a quilismapes belongs to the
    // next nc, not to the wavy line itself.
    if (nc.quilisma) return COMPASSDIRECTION_e;
    return IntmDefaultTilt(nc.intm);
}

std::vector<CalligraphicNeume::PointF> CalligraphicNeume::Cubic(PointF p0, PointF p1, PointF p2, PointF p3, int n)
{
    std::vector<PointF> out;
    out.reserve(n + 1);
    for (int i = 0; i <= n; ++i) {
        const double t = (double)i / n, u = 1.0 - t;
        out.push_back({ u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
            u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y });
    }
    return out;
}

std::vector<CalligraphicNeume::PointF> CalligraphicNeume::RotateAround(
    const std::vector<PointF> &pts, double cx, double cy, double ang)
{
    const double c = std::cos(ang), s = std::sin(ang);
    std::vector<PointF> out;
    out.reserve(pts.size());
    for (const PointF &p : pts) {
        const double dx = p.x - cx, dy = p.y - cy;
        out.push_back({ cx + dx * c - dy * s, cy + dx * s + dy * c });
    }
    return out;
}

// Resample a centreline through a Catmull-Rom spline so a polyline with sharp joints (e.g. a round
// base meeting an ascent) becomes one flowing curve before the nib is swept along it.
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::Densify(
    const std::vector<PointF> &pts, double step, const std::vector<int> *ownersIn, std::vector<int> *ownersOut)
{
    if (ownersOut) ownersOut->clear();
    if (pts.size() < 3) {
        if (ownersOut && ownersIn) *ownersOut = *ownersIn;
        return pts;
    }
    // Centripetal Catmull-Rom (alpha = 0.5): unlike the uniform variant it never overshoots or
    // forms cusps at sharp corners, so offsetting the resampled centreline leaves no notch where two
    // strokes meet at an angle. Endpoints use reflected phantom neighbours for even knot spacing.
    auto lerp = [](PointF a, PointF b, double u) -> PointF { return { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u }; };

    std::vector<PointF> out;
    const int last = (int)pts.size() - 1;
    for (int i = 0; i < last; ++i) {
        const PointF p1 = pts[i];
        const PointF p2 = pts[i + 1];
        const PointF p0 = (i > 0) ? pts[i - 1] : PointF{ 2 * p1.x - p2.x, 2 * p1.y - p2.y };
        const PointF p3 = (i + 2 <= last) ? pts[i + 2] : PointF{ 2 * p2.x - p1.x, 2 * p2.y - p1.y };

        // Knot values spaced by sqrt(distance); clamp to a small epsilon for coincident points.
        const double t0 = 0.0;
        const double t1 = t0 + std::sqrt(std::max(std::hypot(p1.x - p0.x, p1.y - p0.y), 1e-6));
        const double t2 = t1 + std::sqrt(std::max(std::hypot(p2.x - p1.x, p2.y - p1.y), 1e-6));
        const double t3 = t2 + std::sqrt(std::max(std::hypot(p3.x - p2.x, p3.y - p2.y), 1e-6));

        const int n = std::max(1, (int)std::lround(std::hypot(p2.x - p1.x, p2.y - p1.y) / step));
        for (int j = 0; j < n; ++j) {
            const double t = t1 + (t2 - t1) * ((double)j / n);
            // Barry-Goldman recursive evaluation of the centripetal spline.
            const PointF a1 = lerp(p0, p1, (t - t0) / (t1 - t0));
            const PointF a2 = lerp(p1, p2, (t - t1) / (t2 - t1));
            const PointF a3 = lerp(p2, p3, (t - t2) / (t3 - t2));
            const PointF b1 = lerp(a1, a2, (t - t0) / (t2 - t0));
            const PointF b2 = lerp(a2, a3, (t - t1) / (t3 - t1));
            out.push_back(lerp(b1, b2, (t - t1) / (t2 - t1)));
            // Each sub-point of interval i belongs to the component it arrives at, so a component's
            // run of points starts exactly at the joint where its own stroke begins.
            if (ownersOut && ownersIn) ownersOut->push_back((*ownersIn)[i + 1]);
        }
    }
    out.push_back(pts[last]);
    if (ownersOut && ownersIn) ownersOut->push_back((*ownersIn)[last]);
    return out;
}

//----------------------------------------------------------------------------
// Broad-nib ribbon
//----------------------------------------------------------------------------

// The two offset edges of the broad-nib ribbon. The stroke width at each point follows the classic
// broad-pen law w proportional to |T x n̂|: the fixed -45 degree nib comes out thin where the stroke
// runs along its edge and broad where it runs across it, which is the calligraphic thick / thin. The
// width is otherwise uniform along the stroke - there is no along-stroke taper.
void CalligraphicNeume::NibEdges(
    const std::vector<PointF> &pts, double scale, std::vector<PointF> &left, std::vector<PointF> &right)
{
    const int n = (int)pts.size();
    left.assign(n, {});
    right.assign(n, {});
    if (n < 2) return;

    const PointF nib = { std::cos(NIB_ANGLE), std::sin(NIB_ANGLE) };
    for (int i = 0; i < n; ++i) {
        const PointF a = pts[std::max(0, i - 1)], b = pts[std::min(n - 1, i + 1)];
        double tx = b.x - a.x, ty = b.y - a.y;
        const double m = std::hypot(tx, ty);
        if (m > 0) {
            tx /= m;
            ty /= m;
        }
        const double sinA = std::fabs(tx * nib.y - ty * nib.x); // |T x n̂|
        const double w = ((NIB_MIN + (NIB_W - NIB_MIN) * sinA) * scale) / 2.0;
        left[i] = { pts[i].x - ty * w, pts[i].y + tx * w };
        right[i] = { pts[i].x + ty * w, pts[i].y - tx * w };
    }
}

// A few passes of an endpoint-preserving binomial ([1 2 1]/4) filter. The width law derives each
// edge point from a finite-difference tangent, which carries a little high-frequency noise; left
// unfiltered it shows as faint ripples along otherwise straight edges. Low-pass filtering the edge
// before it is fitted to a Bézier gives clean, smooth contours while preserving the overall shape
// (the endpoints - the stroke's hairline tips - are kept fixed).
void CalligraphicNeume::SmoothPolyline(std::vector<PointF> &p, int passes)
{
    const int n = (int)p.size();
    if (n < 3) return;
    for (int pass = 0; pass < passes; ++pass) {
        // Filter in place left-to-right, carrying the previous point's pre-filter value, so no scratch
        // copy of the edge is allocated; the endpoints (the stroke's hairline tips) are left fixed.
        PointF prev = p[0];
        for (int i = 1; i < n - 1; ++i) {
            const PointF cur = p[i];
            p[i] = { 0.25 * prev.x + 0.5 * cur.x + 0.25 * p[i + 1].x, 0.25 * prev.y + 0.5 * cur.y + 0.25 * p[i + 1].y };
            prev = cur;
        }
    }
}

// A punctum is a single dab of the broad nib: a compact, solid mark rather than a swept (and so
// tapered) stroke. It is the nib's footprint - a small lozenge elongated along the nib edge -
// rounded into a smooth blob so it reads as a deliberate point.
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::Punctum(PointF c)
{
    const PointF nib = { std::cos(NIB_ANGLE), std::sin(NIB_ANGLE) }; // the broad edge of the nib
    const PointF perp = { -nib.y, nib.x };
    constexpr double LA = 4.0; // half-length along the nib edge
    constexpr double SA = 2.6; // half-width across it
    const std::vector<PointF> diamond
        = { { c.x + nib.x * LA, c.y + nib.y * LA }, { c.x + perp.x * SA, c.y + perp.y * SA },
              { c.x - nib.x * LA, c.y - nib.y * LA }, { c.x - perp.x * SA, c.y - perp.y * SA } };
    return SmoothClosed(diamond);
}

// A smooth filled outline for the fixed nib swept along a whole centreline.
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::NibRibbon(const std::vector<PointF> &pts, double scale)
{
    std::vector<PointF> left, right;
    NibEdges(pts, scale, left, right);
    if (left.size() < 2) return {};
    SmoothPolyline(left, 2);
    SmoothPolyline(right, 2);

    // One closed loop: up one edge, back the other (reversed).
    std::vector<PointF> loop = left;
    loop.insert(loop.end(), right.rbegin(), right.rend());
    return SmoothClosed(loop);
}

// The two cubic Bézier control points for the segment p1->p2 of a centripetal Catmull-Rom spline
// (alpha = 0.5) through p0,p1,p2,p3. Centripetal parameterization keeps the tangents proportional to
// chord length, so the curve never overshoots or loops where points bunch up (e.g. the inner edge of
// a sharp concave joint) - the cause of the hairline notch a uniform Catmull-Rom would carve there.
void CalligraphicNeume::CentripetalControls(PointF p0, PointF p1, PointF p2, PointF p3, PointF &c1, PointF &c2)
{
    auto knot = [](double d) { return std::sqrt(std::max(d, 1e-6)); };
    const double t0 = 0.0;
    const double t1 = t0 + knot(std::hypot(p1.x - p0.x, p1.y - p0.y));
    const double t2 = t1 + knot(std::hypot(p2.x - p1.x, p2.y - p1.y));
    const double t3 = t2 + knot(std::hypot(p3.x - p2.x, p3.y - p2.y));
    // Non-uniform Catmull-Rom tangent at the middle point of a,b,c (knots ta,tb,tc).
    auto tangent = [](PointF a, PointF b, PointF c, double ta, double tb, double tc) -> PointF {
        return { (b.x - a.x) / (tb - ta) - (c.x - a.x) / (tc - ta) + (c.x - b.x) / (tc - tb),
            (b.y - a.y) / (tb - ta) - (c.y - a.y) / (tc - ta) + (c.y - b.y) / (tc - tb) };
    };
    const PointF m1 = tangent(p0, p1, p2, t0, t1, t2);
    const PointF m2 = tangent(p1, p2, p3, t1, t2, t3);
    const double h = (t2 - t1) / 3.0;
    c1 = { p1.x + m1.x * h, p1.y + m1.y * h };
    c2 = { p2.x - m2.x * h, p2.y - m2.y * h };
}

// Centripetal Catmull-Rom -> cubic Bézier through a closed loop of points; returns the flat
// control-point list [P0, C0a, C0b, P1, C1a, C1b, P2, ...] expected by DrawClosedBezierPath.
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::SmoothClosed(const std::vector<PointF> &p)
{
    const int n = (int)p.size();
    std::vector<PointF> out;
    if (n < 2) return out;
    out.reserve(1 + 3 * n);
    out.push_back(p[0]);
    for (int i = 0; i < n; ++i) {
        PointF c1, c2;
        CentripetalControls(p[(i - 1 + n) % n], p[i], p[(i + 1) % n], p[(i + 2) % n], c1, c2);
        out.push_back(c1);
        out.push_back(c2);
        out.push_back(p[(i + 1) % n]);
    }
    return out;
}

// Centripetal Catmull-Rom -> cubic Bézier through an OPEN polyline: the endpoints are preserved and
// reflected phantom neighbours give even knot spacing at the ends. Returns the flat control-point
// list [P0, C0a, C0b, P1, ...].
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::SmoothOpen(const std::vector<PointF> &p)
{
    const int n = (int)p.size();
    std::vector<PointF> out;
    if (n == 0) return out;
    out.push_back(p[0]);
    if (n == 1) return out;
    out.reserve(1 + 3 * (n - 1));
    for (int i = 0; i < n - 1; ++i) {
        const PointF p1 = p[i], p2 = p[i + 1];
        const PointF p0 = (i > 0) ? p[i - 1] : PointF{ 2 * p1.x - p2.x, 2 * p1.y - p2.y };
        const PointF p3 = (i + 2 <= n - 1) ? p[i + 2] : PointF{ 2 * p2.x - p1.x, 2 * p2.y - p1.y };
        PointF c1, c2;
        CentripetalControls(p0, p1, p2, p3, c1, c2);
        out.push_back(c1);
        out.push_back(c2);
        out.push_back(p2);
    }
    return out;
}

// One per-nc slice, re-using the globally smoothed edges so boundary tangents match the neighbours.
// The flat control-point list runs P0=left[a] -> left-edge Bézier segments -> left[b], a straight
// cubic across to right[b], -> right-edge Bézier segments reversed -> right[a]; DrawClosedBezierPath's
// implicit close draws the final straight butt right[a] -> left[a].
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::SliceRibbon(
    const std::vector<PointF> &leftCurve, const std::vector<PointF> &rightCurve, int a, int b)
{
    // SmoothOpen stores knot i at index 3*i, with segment i->i+1 controls at 3*i+1, 3*i+2.
    const int knots = ((int)leftCurve.size() + 2) / 3;
    if (b >= knots) b = knots - 1;
    if (a < 0 || b <= a) return {};

    std::vector<PointF> out;
    out.reserve(3 * (b - a) * 2 + 4);

    // Left edge a -> b.
    out.push_back(leftCurve[3 * a]);
    for (int i = a; i < b; ++i) {
        out.push_back(leftCurve[3 * i + 1]);
        out.push_back(leftCurve[3 * i + 2]);
        out.push_back(leftCurve[3 * i + 3]);
    }

    // Straight butt left[b] -> right[b] (control points spread evenly along the segment).
    const PointF lb = leftCurve[3 * b], rb = rightCurve[3 * b];
    out.push_back({ lb.x + (rb.x - lb.x) / 3.0, lb.y + (rb.y - lb.y) / 3.0 });
    out.push_back({ lb.x + 2.0 * (rb.x - lb.x) / 3.0, lb.y + 2.0 * (rb.y - lb.y) / 3.0 });
    out.push_back(rb);

    // Right edge b -> a, re-using each segment reversed (swap endpoints and control points).
    for (int i = b - 1; i >= a; --i) {
        out.push_back(rightCurve[3 * i + 2]);
        out.push_back(rightCurve[3 * i + 1]);
        out.push_back(rightCurve[3 * i]);
    }

    return out;
}

//----------------------------------------------------------------------------
// Stroke shapes
//----------------------------------------------------------------------------

CalligraphicNeume::Stroke CalligraphicNeume::ObliqueShape(double x, double y, PointF dir, double len)
{
    const PointF s = { x - dir.x * len * 0.5, y - dir.y * len * 0.5 };
    const PointF e = { x + dir.x * len * 0.5, y + dir.y * len * 0.5 };
    const PointF mid = { (s.x + e.x) / 2, (s.y + e.y) / 2 };
    return { { s, mid, e }, e };
}

// S-shaped oriscus / quassus stroke. It travels along @tilt (so tilt="se" makes the wave descend,
// exiting low so a following ascent can spring from it); the @s-shape value sets the initial swing -
// the direction the pen first moves - which orients the S and its mirror / rotated forms: "w" the
// standard letter S, "e" its mirror, "s" the S turned 90° anti-clockwise, "n" that mirror (see
// SShapeSwing). Without @tilt the spine runs perpendicular to that swing: upright for w/e, level for s/n.
CalligraphicNeume::Stroke CalligraphicNeume::Wave(double x, double y, const std::string &orient, int tilt)
{
    const bool rotated = SShapeRotated(orient);
    PointF dir = TiltVec(tilt);
    if (dir.x == 0.0 && dir.y == 0.0) dir = rotated ? TiltVec(COMPASSDIRECTION_e) : TiltVec(COMPASSDIRECTION_n);
    const double len = rotated ? 24.0 : 22.0;
    // centre the wave on (x, y) by backing the start off by half its travel
    return WaveFrom({ x - dir.x * len / 2, y - dir.y * len / 2 }, orient, tilt);
}

// An S-shaped wave starting at s, travelling along @tilt. Used for a connected oriscus / quassus
// inside a ligature (e.g. the second component of a virga strata), where the wave must spring from
// the previous stroke's tip rather than be centred on a cell.
CalligraphicNeume::Stroke CalligraphicNeume::WaveFrom(PointF s, const std::string &orient, int tilt)
{
    const bool rotated = SShapeRotated(orient);
    PointF dir = TiltVec(tilt);
    // No @tilt: the spine runs perpendicular to the initial swing - upright for an upright S, level
    // for one turned on its side.
    if (dir.x == 0.0 && dir.y == 0.0) dir = rotated ? TiltVec(COMPASSDIRECTION_e) : TiltVec(COMPASSDIRECTION_n);
    const double len = rotated ? 26.0 : 24.0;
    // The amplitude must clear the broad nib (NIB_W = 6 px) comfortably, otherwise the sweep absorbs
    // the wiggle into the ribbon width and the S stops reading as an S.
    const double amp = rotated ? 9.0 : 12.0;
    const PointF e = { s.x + dir.x * len, s.y + dir.y * len };
    // The wiggle axis is the @s-shape's initial swing, projected perpendicular to the travel so the S
    // still runs along @tilt but opens toward @s-shape. The sign carried by that swing is what
    // distinguishes a letterform from its mirror (w vs e, s vs n); a fixed quarter-turn of the travel
    // is the fallback when the swing is parallel to it (a degenerate projection).
    const PointF t = dir.Unit(TiltVec(COMPASSDIRECTION_n));
    const PointF swing = SShapeSwing(orient);
    PointF wig = (swing - t * swing.Dot(t)).Unit(PointF{ -t.y, t.x });
    const PointF c1 = { s.x + dir.x * len * 0.3 + wig.x * amp, s.y + dir.y * len * 0.3 + wig.y * amp };
    const PointF c2 = { e.x - dir.x * len * 0.3 - wig.x * amp, e.y - dir.y * len * 0.3 - wig.y * amp };
    return { Cubic(s, c1, c2, e), e };
}

// Ride a quilisma's wiggle on an arbitrary spine (see the header): @p spine is the axis the wave
// follows, resampled evenly by arc length so the @p waves crests spread evenly along its length
// however it bends, each sample displaced perpendicular to the LOCAL travel so the toothed line
// tracks the spine's own shape (a straight chord, a bow, an angled chevron alike).
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::Wavify(
    const std::vector<PointF> &spine, int waves, double amp)
{
    if (waves < 1) waves = 2; // default number of crests
    constexpr int PER = 10; // samples per crest
    const int n = waves * PER;
    // Phase the swing so it starts at a crest (-amp) and ends at a trough (+amp): an integer count of
    // crests with half a cycle to spare puts the lift exactly at the final trough, its tangent
    // momentarily along the axis, so the next nc's ascent springs cleanly out of that low point.
    const double cycles = (double)waves - 0.5;
    // Cumulative arc length of the spine, so t maps to a position an equal distance along it.
    const int m = (int)spine.size();
    std::vector<double> cum(std::max(m, 1), 0.0);
    for (int k = 1; k < m; ++k) cum[k] = cum[k - 1] + (spine[k] - spine[k - 1]).Len();
    const double total = (m > 0) ? cum[m - 1] : 0.0;
    std::vector<PointF> pts;
    if (m < 2 || total < 1e-9) { // degenerate spine: nothing to ride on
        pts.assign(std::max(1, m), spine.empty() ? PointF{} : spine[0]);
        return pts;
    }
    pts.reserve(n + 1);
    int k = 0; // current spine segment [k, k+1], advanced monotonically as t grows
    for (int i = 0; i <= n; ++i) {
        const double t = (double)i / n; // 0..1 along the spine
        const double sarc = t * total; // target arc length
        // Skip to the segment containing sarc; <= steps past any zero-length segment (the duplicated
        // apex knot of an angled chevron) so the tangent comes from a real leg, not the coincident pair.
        while (k < m - 2 && cum[k + 1] <= sarc) ++k;
        const PointF a = spine[k], b = spine[k + 1];
        const double segLen = cum[k + 1] - cum[k];
        const double u = (segLen > 1e-9) ? (sarc - cum[k]) / segLen : 0.0;
        const PointF p = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
        const PointF tan = (b - a).Unit(PointF{ 1.0, 0.0 }); // local travel of the spine here
        const PointF perp = { -tan.y, tan.x }; // the wiggle swings to either side of it
        const double off = -amp * std::cos(2.0 * M_PI * cycles * t); // crest -> ... -> trough
        pts.push_back({ p.x + perp.x * off, p.y + perp.y * off });
    }
    return pts;
}

// quilisma: a wavy flourish modelled on the liquescent (see the header). The WHOLE note is this wavy
// line - the broad nib swept over it draws the characteristic toothed quilisma - starting at the top
// of the first crest (so the stroke opens on a wave, not a half-swing up to one) and lifting at the
// bottom of the last trough, whence the next nc's ascent springs. "quilisma" means only "make the line
// wavy", so the wiggle rides whatever axis the stroke's own shape traces: a straight chord along @tilt
// by default, or - when the <nc> carries @curve / @angled - the very bow or right-angle chevron those
// would draw. @p waves (from @waves) sets the crest count and the line's length.
CalligraphicNeume::Stroke CalligraphicNeume::Quilisma(
    PointF s, int tilt, int waves, bool centred, int curveHand, bool hasCurve, bool angled, PointF tIn, PointF tOut)
{
    if (waves < 1) waves = 2; // default number of crests
    PointF dir = TiltVec(tilt); // the wavy line travels along @tilt...
    if (dir.x == 0.0 && dir.y == 0.0) dir = TiltVec(COMPASSDIRECTION_e); // ...level by default
    constexpr double WAVE_LEN = 13.0; // travel per crest (along the stroke)
    constexpr double AMP = 7.5; // perpendicular swing of the wiggle (rounder, taller humps)
    const double total = WAVE_LEN * waves; // the quilisma sizes by its crest count, not @rellen
    // A standalone wavy note (@p centred) sits on its anchor, so back the axis off by half its travel;
    // a connected or stepped flourish springs forward from @p s.
    const PointF foot = centred ? PointF{ s.x - dir.x * total / 2, s.y - dir.y * total / 2 } : s;
    // The axis the wiggle rides. With @curve / @angled it is the same bow / chevron a non-wavy stroke
    // would draw (built at the quilisma's own length), so "make the line wavy" composes with the shape;
    // otherwise a plain straight chord along @tilt, which makes Wavify reproduce the level/tilted
    // quilisma exactly - the local tangent is constant, so the perpendicular swing matches the old form.
    std::vector<PointF> spine;
    if (hasCurve) {
        spine = CurvedStroke(foot, tilt, curveHand, total, tIn, tOut, angled).pts;
    }
    else {
        spine = { foot, { foot.x + dir.x * total, foot.y + dir.y * total } };
    }
    std::vector<PointF> pts = Wavify(spine, waves, AMP);
    return { pts, pts.back() };
}

// stropha: a small clockwise hook / comma. Canonical orientation is tilt="e"; any other @tilt
// rotates the hook to that direction.
CalligraphicNeume::Stroke CalligraphicNeume::Comma(double x, double y, int tilt)
{
    std::vector<PointF> pts = Cubic({ x - 4, y - 9 }, { x + 9, y - 9 }, { x + 7, y + 6 }, { x - 3, y + 8 }, 16);
    if (tilt != COMPASSDIRECTION_NONE) {
        const PointF d = TiltVec(tilt);
        pts = RotateAround(pts, x, y, std::atan2(d.y, d.x)); // reference = e (0 rad)
    }
    return { pts, pts.back() };
}

// liquescent stroke (cephalicus / epiphonus): a melodic stroke of length @p stem (0 for a whole-note
// hook with no lead-in) tapering at its tip into an inward curl of radius @p r0 (the diminishing
// liquescence). See the header for the parameters.
CalligraphicNeume::Stroke CalligraphicNeume::Loop(PointF s, PointF dir, int curve, bool looped, double stem, double r0)
{
    PointF d = dir;
    const double dm = std::hypot(d.x, d.y);
    if (dm < 1e-9)
        d = TiltVec(COMPASSDIRECTION_se);
    else
        d = { d.x / dm, d.y / dm }; // a unit travel direction, whatever its source

    // 1) The note's own melodic stroke, so a stem-led liquescent reads as that stroke fading into the
    //    curl rather than a loop floating free of the ligature. A whole-note hook (@p stem == 0) has
    //    no lead-in: it springs straight from @p s and is entirely the curl.
    std::vector<PointF> pts = { s };
    if (stem > 1e-9) {
        const PointF tip = { s.x + d.x * stem, s.y + d.y * stem };
        pts.push_back({ (s.x + tip.x) / 2, (s.y + tip.y) / 2 });
        pts.push_back(tip);
    }
    const PointF tip = pts.back();

    // 2) The terminal curl spirals off the tip, tangent to the stroke (no kink); see Curl.
    const std::vector<PointF> curl = Curl(tip, d, curve, looped, r0);
    pts.insert(pts.end(), curl.begin(), curl.end());
    return { pts, pts.back() };
}

// The terminal liquescent curl: a spiral of starting radius @p r0 springing off @p tip, leaving it
// tangent to the travel direction @p d. Returns just the spiral points, to be appended after a lead-in
// stroke that already ends at @p tip (a straight stem for Loop, a bowed CurvedStroke for CurvedLoop).
// See the header for the parameters.
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::Curl(PointF tip, PointF d, int curve, bool looped, double r0)
{
    const double dm = std::hypot(d.x, d.y);
    if (dm < 1e-9)
        d = TiltVec(COMPASSDIRECTION_se);
    else
        d = { d.x / dm, d.y / dm }; // a unit travel direction, whatever its source

    // The curl's centre sits one radius to the curl side of the tip - left for an anticlockwise @curve,
    // right for a clockwise one - and the spiral leaves the tip tangent to @p d. @looped chooses the
    // ending:
    //   - open (looped == false): the spiral winds a little past a full turn and tapers inward to a
    //     near-point, an open scroll whose tail dies away inside the curl.
    //   - closed (looped == true): the spiral winds exactly one full turn at a near-constant radius, so
    //     its tail returns to touch the tip - the beginning of the liquescence itself for a whole-note
    //     hook - sealing the curl into a closed loop (a gentle mid-loop dip keeps it reading as a
    //     calligraphic teardrop rather than a bare circle).
    const double sign = (curve == curvatureDirection_CURVE_a) ? 1.0 : -1.0; // a = left, c = right of travel
    const PointF perp = { sign * d.y, -sign * d.x }; // unit perpendicular on the curl side
    const PointF ctr = { tip.x + perp.x * r0, tip.y + perp.y * r0 };
    const double a0 = std::atan2(tip.y - ctr.y, tip.x - ctr.x);
    const double sweep = (looped ? 1.0 : 1.35) * 2.0 * M_PI; // closed ring back to the tip vs. open scroll
    const int N = 48;
    std::vector<PointF> pts;
    pts.reserve(N);
    for (int i = 1; i <= N; ++i) {
        const double t = (double)i / N;
        const double ang = a0 - sign * sweep * t; // wind in the curl direction
        // Closed: r0 -> dip -> r0, so the last point lands back on the tip. Open: taper to a near-point.
        const double rr = looped ? r0 * (1.0 - 0.45 * std::sin(M_PI * t)) : r0 * std::pow(0.12, t);
        pts.push_back({ ctr.x + rr * std::cos(ang), ctr.y + rr * std::sin(ang) });
    }
    return pts;
}

// The looped connection (@con="l", Old Hispanic): a small cursive loop written at the joint between
// two connected components - the pen arrives at @p p along @p tIn, winds the reflex way round a
// circle of radius @p r and leaves tangent to @p tOut, crossing back over the entry path like the
// bottom loop of a cursive descender. See the header for the parameters.
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::LoopJoint(PointF p, PointF tIn, PointF tOut, double r)
{
    const PointF ti = tIn.Unit({ 0.0, 1.0 });
    const PointF to = tOut.Unit(ti);
    // The shortest signed rotation from the entry tangent to the exit tangent, as a pen-space angle
    // in (-pi, pi] (+y down, so a positive turn is clockwise on screen).
    const double residual = std::atan2(ti.x * to.y - ti.y * to.x, ti.Dot(to));
    // The compass-quantised joints sit at |residual| <= 3pi/4; only a curve-sampled entry tangent can
    // land between that and the about-face, so the threshold merely splits the two regimes cleanly.
    constexpr double kAboutFace = 7.0 * M_PI / 8.0;
    double sweep;
    if (std::fabs(residual) < kAboutFace) {
        // The generic joint takes the REFLEX turn: wind the long way round, opposite the shortest
        // rotation, sweeping 2pi - |residual| the other way. Winding against the turn is what makes
        // the mark a true cursive loop - the exit stroke crosses back over the entry path (a
        // descender's tail crossing its own descent), where winding the short way plus a full turn
        // would only retrace the circle and can never cross. It also hangs the belly on the scribe's
        // side: below a descent-into-ascent joint (a clockwise-on-screen g-tail), above an
        // ascent-into-descent one (an anticlockwise ell-top). A straight-through joint (residual 0)
        // winds positive - clockwise on screen, the ring tucked below / behind the advancing pen.
        sweep = (residual > 1e-9) ? residual - 2.0 * M_PI : residual + 2.0 * M_PI;
    }
    else {
        // An about-face (a descent looping straight into a parallel ascent) cannot cross whichever
        // way the pen winds - the limbs leave parallel, 2r apart - so the joint keeps a full ring
        // instead: one-and-a-half turns, the eye sealed between the limbs. It winds to the natural
        // side - clockwise on screen off a descent (the right-hand descender tail), anticlockwise
        // off an ascent (the ell-top) - tucking the ring behind the advancing pen either way.
        const double w = (ti.y < -1e-9) ? -1.0 : 1.0;
        double resW = residual; // the residual expressed as a rotation in the winding direction
        if (resW * w < 0.0) resW += w * 2.0 * M_PI;
        sweep = resW + w * 2.0 * M_PI;
    }
    const double w = (sweep < 0.0) ? -1.0 : 1.0; // winding sense (in pen space)
    // The loop's centre sits one radius to the winding side of the entry point, so the circle leaves
    // @p p tangent to the entry direction; the exit point then falls wherever the exit tangent is
    // reached, and the following stroke springs from there across the entry path.
    const PointF n = (w > 0.0) ? PointF{ -ti.y, ti.x } : PointF{ ti.y, -ti.x };
    const PointF ctr = { p.x + n.x * r, p.y + n.y * r };
    const double a0 = std::atan2(p.y - ctr.y, p.x - ctr.x);
    const int N = std::max(24, (int)std::lround(40.0 * std::fabs(sweep) / (2.0 * M_PI)));
    std::vector<PointF> pts;
    pts.reserve(N);
    for (int i = 1; i <= N; ++i) {
        const double t = (double)i / N;
        const double ang = a0 + sweep * t;
        pts.push_back({ ctr.x + r * std::cos(ang), ctr.y + r * std::sin(ang) });
    }
    return pts;
}

// A gentle, neighbour-aware curved stroke (see header). The chord runs s -> s + dir*len along
// @tilt; @curve sets the bow side. The end tangents are aligned to the incoming (@p tIn) and
// outgoing (@p tOut) travel directions so the stroke flows smoothly out of the previous component
// and into the next instead of scooping independently (the cause of the old over-curved look).
CalligraphicNeume::Stroke CalligraphicNeume::CurvedStroke(
    PointF s, int tilt, int hand, double len, PointF tIn, PointF tOut, bool angled)
{
    PointF d = TiltVec(tilt);
    if (d.x == 0.0 && d.y == 0.0) d = TiltVec(COMPASSDIRECTION_e);
    const PointF e = { s.x + d.x * len, s.y + d.y * len };
    const double px = -d.y, py = d.x; // chord perpendicular = bow axis
    const double sigma = (hand == curvatureDirection_CURVE_a) ? 1.0 : -1.0; // a anticlockwise, c clockwise

    if (angled) {
        // The angled counterpart of the bow (@angled): the centreline does not curve, it BREAKS at a
        // single apex to the @curve side, so the same one-sided deviation reads as a sharp corner (an
        // angular porrectus / torculus) instead of a rounded scoop. The corner is a true RIGHT ANGLE:
        // its two legs meet at 90°. By Thales' theorem a point that sees the chord at a right angle
        // lies on the circle with the chord as diameter; the symmetric such point sits half the chord
        // length out from the midpoint, so each leg makes 45° with the chord and the legs are
        // perpendicular to each other. The legs are sampled densely so the nib taper stays smooth, and
        // they meet at a duplicated apex knot: the centripetal resampling in InkRun passes a knot
        // through twice as a corner, so the angle survives instead of being rounded away with the spine.
        const double depth = len / 2.0; // half the chord -> a 90° apex
        constexpr int kLeg = 9; // samples per straight leg
        const PointF apex = { (s.x + e.x) / 2 + px * sigma * depth, (s.y + e.y) / 2 + py * sigma * depth };
        std::vector<PointF> pts;
        pts.reserve(2 * (kLeg + 1));
        for (int i = 0; i <= kLeg; ++i) {
            const double t = (double)i / kLeg;
            pts.push_back({ s.x + (apex.x - s.x) * t, s.y + (apex.y - s.y) * t });
        }
        for (int i = 0; i <= kLeg; ++i) {
            const double t = (double)i / kLeg;
            pts.push_back({ apex.x + (e.x - apex.x) * t, apex.y + (e.y - apex.y) * t });
        }
        return { pts, e };
    }

    tIn = tIn.Unit(d);
    tOut = tOut.Unit(d);
    // Lean the end tangents partway back toward this stroke's own travel (@tilt). With pure
    // neighbour tangents a stroke whose @tilt opposes its neighbours - an se descent wedged between
    // two ne ascents - gets swallowed: the chord still ends at e, but both control points pull the
    // bow the neighbours' way until the stroke's own limb is invisible (and @curve's hand only
    // decides whether it survives at all). Mixing kOwnLean of the chord direction back into each end
    // tangent keeps the joints smooth while letting the stroke assert its own gesture, so @curve's
    // hand bows a real limb instead of merely nudging a neighbour-shaped arc.
    constexpr double kOwnLean = 0.35; // fraction of the chord direction mixed into each end tangent
    tIn = (tIn + (d - tIn) * kOwnLean).Unit(d);
    tOut = (tOut + (d - tOut) * kOwnLean).Unit(d);

    // A gentle symmetric bow whose end tangents lean toward the neighbouring strokes.
    constexpr double kBowDepth = 9.0; // bow depth in pen px (the old deep arc used 13)
    constexpr double kHandleFrac = 0.34; // tangent handle length as a fraction of the chord
    PointF c1 = { s.x + tIn.x * len * kHandleFrac + px * sigma * kBowDepth,
        s.y + tIn.y * len * kHandleFrac + py * sigma * kBowDepth };
    PointF c2 = { e.x - tOut.x * len * kHandleFrac + px * sigma * kBowDepth,
        e.y - tOut.y * len * kHandleFrac + py * sigma * kBowDepth };
    // Keep each control point between the chord ends along the travel direction. When a neighbour
    // tangent opposes this stroke's own travel (a sharp reversal - a vertical s descent wedged between
    // ne ascents), the leaned handle would otherwise push the control point past its endpoint, so the
    // bow overshoots and curls back on itself - a cusp that inks as a blob at the joint. Clamping the
    // along-chord reach (the perpendicular bow is untouched) keeps the limb a clean arc; the run's
    // global smoothing still rounds the joint.
    auto clampAlong = [&](PointF p) -> PointF {
        const double a = (p.x - s.x) * d.x + (p.y - s.y) * d.y; // projection onto the chord axis
        const double c = std::clamp(a, 0.0, len);
        return { p.x + (c - a) * d.x, p.y + (c - a) * d.y };
    };
    c1 = clampAlong(c1);
    c2 = clampAlong(c2);
    return { Cubic(s, c1, c2, e), e };
}

// A liquescent whose melodic lead-in is itself a curved stroke (the <nc>'s @curve): the bow is built
// by CurvedStroke, then the terminal curl springs off the bow's exit tangent so the curve flows into
// the curl as one gesture. So an <nc> carrying both @curve and a <liquescent> child reads as a curved
// stroke ending in the curl, instead of the @curve being dropped for a straight stem. See the header.
CalligraphicNeume::Stroke CalligraphicNeume::CurvedLoop(PointF s, int tilt, int curveHand, double len, PointF tIn,
    PointF tOut, int curlHand, bool looped, double r0, bool angled)
{
    const Stroke lead = CurvedStroke(s, tilt, curveHand, len, tIn, tOut, angled);
    std::vector<PointF> pts = lead.pts;
    // The curl leaves the bow tangent to its exit direction (the last leg of the cubic), so curve and
    // curl join without a kink; fall back to the chord direction for a degenerate one-point lead-in.
    PointF d = TiltVec(tilt);
    if (pts.size() >= 2) d = (pts.back() - pts[pts.size() - 2]).Unit(d);
    const std::vector<PointF> curl = Curl(pts.back(), d, curlHand, looped, r0);
    pts.insert(pts.end(), curl.begin(), curl.end());
    return { pts, pts.back() };
}

// The run a stroke flows on into when its episema is drawn as a continuation rather than a separate accent
// - the foot / tail the pen lays down without lifting as it leaves the note (a clivis descent flicking into
// a tenuto, a stropha hook running on into a tail, a virga flagging off at the top). The pen leaves @p
// corner still travelling along the stroke's exit direction @p exitDir, overshoots a touch so the turn
// rounds rather than kinks, then settles onto a straight run in direction @p runDir (its caller sets this
// 90° to the base stroke for a default episema, level for an @form="h" one). Returns only the points PAST
// @p corner (already the stroke's last centreline point), so the caller appends them to extend the run.
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::EpisemaFoot(PointF corner, PointF exitDir, PointF runDir)
{
    const PointF t = exitDir.Unit(runDir); // travel arriving at the corner
    const PointF u = runDir.Unit(t); // direction the episema runs out in
    constexpr double FOOT_LEN = 13.0; // reach of the episema (about its full length)
    constexpr double FOOT_DROP = 3.5; // overshoot along the stroke before the turn - rounds the corner
    constexpr double kHandle = 0.40; // arrival handle, so the run settles cleanly onto u
    const PointF tip = corner + u * FOOT_LEN; // far end of the run
    const PointF c1 = corner + t * FOOT_DROP; // leave along the stroke
    const PointF c2 = tip - u * (FOOT_LEN * kHandle); // arrive running along u
    const std::vector<PointF> arc = Cubic(corner, c1, c2, tip, 14);
    return std::vector<PointF>(arc.begin() + 1, arc.end()); // drop corner: already the stroke's last point
}

//----------------------------------------------------------------------------
// Build
//----------------------------------------------------------------------------

// Ink one continuous gesture (a run of connected components) and cut it into per-nc slices. The
// ribbon edges are computed and smoothed over the whole run, then attributed to the component each
// cross-section came from; adjacent slices overlap by a few cross-sections so their straight butts
// are buried under the neighbouring ink and the union traces only the smooth edges.
void CalligraphicNeume::InkRun(const std::vector<Seg> &run, NeumeGeometry &geo, Slant slant)
{
    if (run.empty()) return;
    if (run[0].isDot) { // a punctum is isolated into its own run
        PointF c = run[0].pts[0]; // a dab has no travel direction: it is only carried by the slant
        if (!slant.IsNone()) c = SlantShear(c, { 0.0, 0.0 }, slant);
        geo.ncs[run[0].ncIndex].ribbon = Punctum(c);
        geo.strokes.push_back({ run[0].ncIndex, false, { c } });
        return;
    }
    if (run.size() == 1) {
        std::vector<PointF> pts = run[0].pts;
        if (!slant.IsNone()) SlantApply(pts, slant);
        geo.ncs[run[0].ncIndex].ribbon = NibRibbon(pts);
        std::vector<unsigned char> foot;
        if (run[0].footStart >= 0) {
            foot.assign(pts.size(), 0);
            for (size_t k = (size_t)run[0].footStart; k < foot.size(); ++k) foot[k] = 1;
        }
        geo.strokes.push_back({ run[0].ncIndex, false, pts, foot });
        return;
    }

    // Concatenate the centrelines into one spine, dropping the duplicated joint points and tracking
    // which component (run position) each spine point belongs to. The continuation-foot flag rides
    // packed into the label (owner*2 + foot): Densify only copies labels, never computes with them.
    std::vector<PointF> spine;
    std::vector<int> spineOwners;
    for (size_t si = 0; si < run.size(); ++si) {
        const Seg &s = run[si];
        size_t startIdx = 0;
        if (!spine.empty() && !s.pts.empty()) {
            const PointF last = spine.back();
            if (std::hypot(s.pts[0].x - last.x, s.pts[0].y - last.y) < 1.5) startIdx = 1;
        }
        for (size_t k = startIdx; k < s.pts.size(); ++k) {
            spine.push_back(s.pts[k]);
            const int isFoot = (s.footStart >= 0 && (int)k >= s.footStart) ? 1 : 0;
            spineOwners.push_back((int)si * 2 + isFoot);
        }
    }

    // Densify into one smooth curve (carrying ownership), then sweep the nib over the whole run so
    // the taper and tangents stay continuous across component boundaries.
    std::vector<int> owners;
    std::vector<PointF> dense = Densify(spine, 3.0, &spineOwners, &owners);
    std::vector<unsigned char> denseFoot(owners.size(), 0);
    bool anyFoot = false;
    for (size_t i = 0; i < owners.size(); ++i) {
        denseFoot[i] = (unsigned char)(owners[i] & 1);
        anyFoot = anyFoot || denseFoot[i];
        owners[i] >>= 1;
    }
    if (!anyFoot) denseFoot.clear();
    // Lean the smooth spine toward the upper-right BEFORE sweeping the nib, so the fixed nib meets the
    // leaned strokes at new angles. The per-point tangent makes the lean continuous across the joints.
    if (!slant.IsNone()) SlantApply(dense, slant);
    std::vector<PointF> left, right;
    NibEdges(dense, 1.0, left, right);
    // Low-pass the edges to remove finite-difference width noise (faint ripples) before fitting.
    SmoothPolyline(left, 2);
    SmoothPolyline(right, 2);
    // Smooth each long edge once over the whole run; slices re-use these Bézier segments so their
    // shared boundaries stay tangent-continuous.
    const std::vector<PointF> leftCurve = SmoothOpen(left);
    const std::vector<PointF> rightCurve = SmoothOpen(right);

    // First densified knot owned by each component, in document order. Points are attributed to the
    // component they arrive at, so a degenerate first component (whose only point is the joint that
    // starts the next stroke) would own no knot; since the gesture nonetheless begins with it, it
    // still takes the opening knot so it gets a (small) slice of its own.
    const int lastIdx = (int)left.size() - 1;
    std::vector<int> firstKnot(run.size(), -1);
    for (size_t i = 0; i < owners.size(); ++i) {
        if (firstKnot[owners[i]] < 0) firstKnot[owners[i]] = (int)i;
    }
    if (firstKnot[0] < 0) firstKnot[0] = 0;
    std::vector<std::pair<size_t, int>> contrib; // (run position, first knot)
    for (size_t si = 0; si < run.size(); ++si) {
        if (firstKnot[si] >= 0) contrib.push_back({ si, firstKnot[si] });
    }

    // Each slice spans its own component plus a margin past both boundaries. The overlap means every
    // joint - however sharp - is covered as the *interior* of both adjacent slices (so it is inked by
    // the true, possibly self-intersecting ribbon edges, never cut by a straight butt), while each
    // slice's straight butts land in the neighbour's locally-straight region and are buried under its
    // ink. The union therefore traces only the globally smoothed edges - exactly the single
    // continuous ribbon - at any joint angle, while each component still owns a fillable slice.
    constexpr int kMargin = 4;
    for (size_t p = 0; p < contrib.size(); ++p) {
        const int coreEnd = (p + 1 < contrib.size()) ? contrib[p + 1].second : lastIdx;
        const int a = std::max(0, contrib[p].second - kMargin);
        const int b = std::min(lastIdx, coreEnd + kMargin);
        geo.ncs[run[contrib[p].first].ncIndex].ribbon = SliceRibbon(leftCurve, rightCurve, a, std::max(a + 1, b));
    }
    geo.strokes.push_back({ run[0].ncIndex, false, dense, denseFoot });
}

// Episemata: each is a separate short accent stroke, swept with the same broad nib as the ribbon
// (its own nib) but sitting just clear of the note - historically the episema is a distinct pen
// stroke, not part of the neume's body. @place sets which side it sits on and the small gap to the
// ink; @form sets its orientation and where on the stroke it sits. An "above" accent marks the peak
// of the note's stroke: it is carried to the stroke's summit (its highest point) and laid flat along
// the horizontal tangent there, so on an arch it caps the top rather than the end-joint, which has
// slid down the shoulder (see the summit block below). Any other default accent orients itself by
// where its end falls: on a joint between this nc and a following connected one it lies along the
// joint's tangent (the smooth direction the pen carries through the corner - the mean of the two
// strokes' travel, so the following nc is taken into account); at a real end (no following joint) or
// in the middle of a stroke it lies across the stroke. @form="h" forces along the stroke, the
// explicit override. Stored with the owning nc.
void CalligraphicNeume::BuildEpisemata(
    const std::vector<std::vector<Seg>> &runs, const std::vector<NcInfo> &ncs, NeumeGeometry &geo, Slant slant)
{
    // Travel direction along a centreline's last segment, read off the sampled geometry so a bowed
    // @curve / @s-shape stroke is taken along its real tangent through the joint, not the straight
    // @tilt chord. Returns the zero vector when the stroke is too short to give a direction; callers
    // fall back to the nominal @tilt.
    auto tiltUnit = [](int tilt) -> PointF {
        const PointF t = TiltVec(tilt);
        return (t.x == 0.0 && t.y == 0.0) ? TiltVec(COMPASSDIRECTION_e) : t;
    };
    auto segDir = [](PointF from, PointF to) -> PointF { return (to - from).Unit(); };

    for (const std::vector<Seg> &run : runs)
        for (size_t si = 0; si < run.size(); ++si) {
            const Seg &s = run[si];
            const NcInfo &nc = ncs[s.ncIndex];
            if (nc.episemata.empty() || s.pts.empty()) continue;
            // A footed nc had its first episema inked back in Build as the stroke's continuation foot. A lone
            // foot is then complete; a chained one still needs its upright(s), drawn below crossing the foot's
            // outer tip - the centreline's last point, where the foot reached out to. See @ref startEi below.
            if (s.footEpisema && nc.episemata.size() <= 1) continue;
            const PointF end = s.pts.back();
            // The marked stroke's travel as it reaches the marked point: its actual end tangent.
            PointF along = (s.pts.size() >= 2) ? segDir(s.pts[s.pts.size() - 2], s.pts.back()) : PointF{ 0.0, 0.0 };
            if (along.x == 0.0 && along.y == 0.0) along = tiltUnit(s.tilt);
            const PointF across = { -along.y, along.x }; // perpendicular to the stroke

            // The default orientation: the joint tangent when this nc hands off to a following connected
            // one, otherwise across the stroke. The joint tangent is the unit mean of the two strokes'
            // tangents at the corner - the marked stroke's end tangent and the next stroke's start
            // tangent, both read from the sampled curve, so curving is honoured. A near-antiparallel pair
            // (a hairpin turn) has no meaningful mean, but the tangent there lies across the limbs anyway,
            // so it falls back to the perpendicular.
            PointF tangent = across;
            if (si + 1 < run.size()) {
                const Seg &nx = run[si + 1];
                PointF out = (nx.pts.size() >= 2) ? segDir(nx.pts.front(), nx.pts[1]) : PointF{ 0.0, 0.0 };
                if (out.x == 0.0 && out.y == 0.0) out = tiltUnit(nx.tilt);
                const PointF t = { along.x + out.x, along.y + out.y };
                const double tl = std::hypot(t.x, t.y);
                if (tl > 1e-6) tangent = { t.x / tl, t.y / tl };
            }

            // The summit of the marked stroke: its highest centreline point (pen +y is down). An "above"
            // episema marks the peak of the note's stroke, and on a stroke that arches over - rising to a
            // summit and then curving down past it before it hands off (a clivis's first limb) - that
            // summit sits in the stroke's interior, while the end-joint has already slid down the far
            // shoulder. So when the summit is a genuine interior peak the accent is anchored there rather
            // than at the end, lying flat along the horizontal tangent a summit has by definition - on top
            // of the curve, not tilted on its shoulder. A stroke that does not arch has its summit at the
            // end, so this collapses back to the end / joint placement.
            // Scan only the nc's own stroke: the prepended ink of a looped connection (@con="l") is
            // the joint, not the note, so neither the summit nor the mid-stroke anchor below may land
            // on it (an "above" accent must cap the note's stroke, never the connection loop).
            const int s0 = std::min(s.strokeStart, (int)s.pts.size() - 1);
            int apexIdx = s0;
            for (int q = s0 + 1; q < (int)s.pts.size(); ++q)
                if (s.pts[q].y < s.pts[apexIdx].y) apexIdx = q;
            const bool arched = (apexIdx > s0 && apexIdx + 1 < (int)s.pts.size());
            PointF summitTan = arched ? segDir(s.pts[apexIdx - 1], s.pts[apexIdx + 1]) : PointF{ 0.0, 0.0 };
            if (summitTan.x == 0.0 && summitTan.y == 0.0) summitTan = { 1.0, 0.0 };

            // Episemata chain. Usually an nc carries one accent, placed independently against the stroke
            // it marks. When several are stacked on the same nc they are CHAINED into one stepped gesture:
            // the first reaches out from the marked point toward the open side, and each one after it
            // stands vertically and CROSSES the far end of the accent before it. So a clivis's foot - a
            // horizontal episema with an upright crossing its outer end - reads as a single hand-drawn
            // shape rather than a heap of marks piled on the same point.
            bool linked = false;
            PointF linkEnd; // the far end of the previous accent in the chain (pen space, pre-slant)
            // A footed chain skips its first episema (already the drawn foot) and seeds the chain on the foot's
            // outer tip, so the upright(s) cross that tip just as a normal chain's uprights cross the first
            // bar's reaching-out end.
            size_t startEi = 0;
            if (s.footEpisema) {
                startEi = 1;
                linked = true;
                linkEnd = end; // the foot tip
            }
            for (size_t ei = startEi; ei < nc.episemata.size(); ++ei) {
                const EpisemaInfo &e = nc.episemata[ei];
                // The accent's orientation, half-length, and the two centreline ends a -> b.
                PointF ori;
                double HL;
                PointF a, b;
                // An end-cap accent: a lone, default-placed episema crossing the FREE end of a stroke (no
                // following joint to lean into, no @place offset to lift or slide it clear). It is hung from
                // its dish trough rather than its chord so the crossing point lands on the tip - see the
                // sweep below. Chained, lifted, summit and forced-horizontal accents keep the plain dish.
                bool endCap = false;
                if (!linked) {
                    // First (or only) accent: placed against the marked stroke per @form / @place.
                    const bool above = (e.place == EVENTREL_above || e.place == EVENTREL_above_left
                        || e.place == EVENTREL_above_right);
                    // Anchor an "above" accent on an arched stroke's summit (flat on top); otherwise on the
                    // end / joint as before. @form="h" (forced along-stroke) keeps its explicit orientation.
                    const bool onSummit = arched && above && (e.form != episemaVis_FORM_h);
                    // A left @place reaches OUT to the note's left as a separate accent (see episemaFoot). It
                    // stands vertical to THIS stroke - across its own end tangent - looking out to the left,
                    // and must never lean along a downstream joint tangent: on a virga strata the first
                    // stroke's head is the joint the strophicus springs from, so the joint tangent runs level
                    // into that following stroke and would bury the accent under the neume's body. So a left
                    // accent keeps `across`, ignoring any following joint. A forced-horizontal @form="h" bar
                    // keeps its own along-stroke orientation and placement (handled below), so it is excluded.
                    const bool leftReach = (e.place == EVENTREL_left) && (e.form != episemaVis_FORM_h);
                    // When the marked stroke hands off to a following one (a virga strata: the strophicus
                    // springs from the head), the head is occupied, so a left accent slides down to the
                    // middle of the rising stroke - clear of the neume body above, where the scribe set it -
                    // rather than crowding the joint.
                    const bool midStroke = leftReach && (si + 1 < run.size());
                    const PointF anchor
                        = onSummit ? s.pts[apexIdx] : (midStroke ? s.pts[(s0 + (int)s.pts.size()) / 2] : end);
                    ori = (e.form == episemaVis_FORM_h) ? along
                                                        : (leftReach ? across : (onSummit ? summitTan : tangent));
                    HL = (e.form == episemaVis_FORM_h) ? 7.0 : 6.0;
                    const double GAP = 4.0, NUDGE = 7.0;
                    // The vertical clearance of the accent from the marked point. On a summit the air gap is
                    // measured from the inked edge, not the centreline: the ribbon stands half a nib-width
                    // proud of its spine at the peak, so add that half-width (from the broad-nib law at the
                    // summit's horizontal tangent) to keep the same clean gap the end placement has.
                    double clear = GAP;
                    if (onSummit) {
                        const PointF nib = { std::cos(NIB_ANGLE), std::sin(NIB_ANGLE) };
                        const double sinA = std::fabs(ori.x * nib.y - ori.y * nib.x); // |T x n̂|
                        clear = GAP + (NIB_MIN + (NIB_W - NIB_MIN) * sinA) / 2.0;
                    }
                    // Centre the accent on the marked point, then set it clear of the ink in the accent's
                    // own frame so the point always stays at its midpoint instead of sliding to a corner:
                    // @place's vertical part (above / below) lifts it perpendicular to its length, its
                    // horizontal part (left / right) shifts it along its length to one side. @up is the
                    // accent normal oriented upward (pen -y); @axis runs along the accent toward +x so
                    // left / right read as expected.
                    PointF up = { -ori.y, ori.x };
                    if (up.y > 0.0) up = { -up.x, -up.y };
                    const PointF axis = (ori.x < 0.0) ? PointF{ -ori.x, -ori.y } : ori;
                    double lift = 0.0, shift = 0.0;
                    switch (e.place) {
                        case EVENTREL_above:
                        case EVENTREL_above_left:
                        case EVENTREL_above_right: lift = clear; break;
                        case EVENTREL_below:
                        case EVENTREL_below_left:
                        case EVENTREL_below_right: lift = -clear; break;
                        default: break;
                    }
                    switch (e.place) {
                        case EVENTREL_above_left:
                        case EVENTREL_below_left: shift = -NUDGE; break;
                        case EVENTREL_above_right:
                        case EVENTREL_below_right: shift = NUDGE; break;
                        case EVENTREL_left: shift = -GAP; break;
                        case EVENTREL_right: shift = GAP; break;
                        default: break;
                    }
                    // A lone accent stays centred on the marked point. When this nc carries a chain, the
                    // first accent instead slides one half-length along its axis toward the open side (+x,
                    // away from the neume body), so one end sits on the marked point and the bar reaches
                    // out into clear space - giving the upright that follows a far end to cross that is
                    // clear of the stroke.
                    const double away = (nc.episemata.size() > 1) ? HL : 0.0;
                    const double cx = anchor.x + up.x * lift + axis.x * (shift + away);
                    const double cy = anchor.y + up.y * lift + axis.y * (shift + away);
                    a = { cx - ori.x * HL, cy - ori.y * HL };
                    b = { cx + ori.x * HL, cy + ori.y * HL };
                    // Flag a default accent lying across the FREE end of this stroke (no following joint, no
                    // @place offset): it crosses the tip rather than sitting clear of it. Centred on the
                    // centreline end its freehand dish sags away from the tip, leaving the inked point to
                    // poke out past the accent (the tractulus's east stroke peeking out under its episema).
                    // The sweep below hangs such an accent from its dish trough instead, dropping it onto
                    // the tip to cap it.
                    endCap = (nc.episemata.size() == 1) && !onSummit && (e.form != episemaVis_FORM_h)
                        && (si + 1 >= run.size()) && (lift == 0.0) && (shift == 0.0);
                }
                else {
                    // A chained accent stands vertically (pen -y) and CROSSES the far end of the accent
                    // before it - the stroke that crosses the outer end of a clivis foot's horizontal
                    // episema. By default it is centred on that junction (equal above and below); @place
                    // then slides it one half-length clear so it sits above the junction (rising from it)
                    // or below it (hanging from it). Its length matches a vertical accent's (2 * HL).
                    ori = { 0.0, -1.0 };
                    HL = 6.0;
                    double placeLift = 0.0; // along ori (up): +HL puts the junction at the foot, -HL at the top
                    switch (e.place) {
                        case EVENTREL_above:
                        case EVENTREL_above_left:
                        case EVENTREL_above_right: placeLift = HL; break;
                        case EVENTREL_below:
                        case EVENTREL_below_left:
                        case EVENTREL_below_right: placeLift = -HL; break;
                        default: break;
                    }
                    const PointF c = { linkEnd.x + ori.x * placeLift, linkEnd.y + ori.y * placeLift };
                    a = { c.x - ori.x * HL, c.y - ori.y * HL }; // lower end
                    b = { c.x + ori.x * HL, c.y + ori.y * HL }; // upper end
                }
                // Sweep the broad nib along the accent's centreline at full, untapered width: a short
                // stroke in the same hand as the neume, set just clear of the ink. The centreline is not
                // a ruled bar but a shallow concave-up dish - the broad nib drawn freehand sags in the
                // middle and flicks up at the ends - so the accent reads as a hand-drawn stroke, not a
                // straight line.
                // Perpendicular to the accent's length, oriented visually downward (pen +y), so the bow
                // dips the middle below the chord while the ends ride up - a concave-up valley.
                PointF perp = { -ori.y, ori.x };
                if (perp.y < 0.0) perp = { -perp.x, -perp.y };
                // A lone accent keeps the freehand dish; chained accents are drawn nearly straight, so the
                // stepped clivis foot reads as ruled strokes meeting at a right angle rather than two
                // scoops. Their depth at mid-length is a fraction of the half-length.
                const double bow = HL * (nc.episemata.size() > 1 ? EPISEMA_BOW_CHAIN : EPISEMA_BOW);
                // An end-cap accent is hung from its dish TROUGH (the deepest point, dip = bow at mid - the
                // part that crosses the marked stroke) instead of from its chord. Subtracting the full dip
                // there translates the whole centreline by -perp*bow, dropping that trough exactly onto the
                // anchor (the stroke's tip) so the broad accent caps the point. The offset is exact - the
                // trough dip IS bow - so it needs no tuning and holds at any stroke angle. Apply it only
                // when the dish sags back toward the stroke (perp opposing the exit); a stroke whose dish
                // already bows outward - a vertical descent - caps cleanly and keeps the plain chord.
                const double trough = (endCap && perp.Dot(along) < 0.0) ? bow : 0.0;
                constexpr int kSamples = 10;
                std::vector<PointF> centre;
                centre.reserve(kSamples + 1);
                for (int k = 0; k <= kSamples; ++k) {
                    const double u = (double)k / kSamples;
                    const double dip = bow * 4.0 * u * (1.0 - u) - trough; // 0 at the ends, max at mid, less the trough
                    centre.push_back({ a.x + (b.x - a.x) * u + perp.x * dip, a.y + (b.y - a.y) * u + perp.y * dip });
                }
                // The accent is a separate pen stroke, so it rides the same forward slant as the ribbon,
                // which also carries its anchor across to meet the leaned stroke end it marks.
                if (!slant.IsNone()) SlantApply(centre, slant);
                geo.ncs[s.ncIndex].episemata.push_back(NibRibbon(centre, EPISEMA_NIB));
                geo.strokes.push_back({ s.ncIndex, true, centre });
                // The next link crosses this accent's far end: the endpoint reaching farthest into open
                // space - the rightmost (+x), or for an upright accent (equal x) its top. Recorded
                // pre-slant so the shear that follows leaves the chain joined.
                linkEnd = (std::fabs(a.x - b.x) > 1e-6) ? (a.x > b.x ? a : b) : (a.y < b.y ? a : b);
                linked = true;
            }
        }
}

CalligraphicNeume::NeumeGeometry CalligraphicNeume::Build(const std::vector<NcInfo> &ncs, double scale, Slant slant)
{
    NeumeGeometry geo;
    if (ncs.empty()) return geo;

    // 1) Walk the components, collecting each nc's centreline contribution and grouping them into
    //    runs. A run is a maximal sequence of connected components inked as one continuous pen
    //    gesture; @con="g" (gapped) starts a new run. The pen starts at the origin (0, 0); the
    //    caller anchors and flips the whole gesture afterwards.
    std::vector<std::vector<Seg>> runs;
    PointF pen = { 0.0, 0.0 }; // where the current gesture has reached
    PointF lastAnchor = { 0.0, 0.0 }; // anchor of the previous component, for placing detached ones
    PointF prevExitDir = { 0.0, 0.0 }; // unit exit tangent of the previous component (0 = none)

    // A bare component carries no shape attribute: it is a detached punctum, so it never takes part
    // in a tangent handoff (it begins a fresh gesture). The per-iteration `bare`/`brk` flags and the
    // neighbour lookahead both go through these, so the rule has a single definition.
    auto bareOf = [&](size_t k) -> bool {
        const NcInfo &n = ncs[k];
        // A looped component (@con="l") is by definition JOINED to its predecessor, so it is never a
        // detached punctum: even without a shape attribute it draws its connection loop and flows on
        // along the @intm default (level when even that is absent). On the first component the
        // connection has nothing to join, so bareness is judged by the shape attributes alone.
        if (k > 0 && n.looped) return false;
        return (n.tilt == COMPASSDIRECTION_NONE) && (n.curve == curvatureDirection_CURVE_NONE) && !n.angled
            && n.sShape.empty() && !n.hasNonEpisemaChild;
    };
    // Whether component k begins a fresh pen gesture (an explicit gap or a bare punctum).
    auto breaksBefore = [&](size_t k) -> bool { return k > 0 && (ncs[k].gapped || bareOf(k)); };
    // A continuation episema is written in one pen motion with its note: reaching the note, the pen flows
    // on - without lifting - into the episema, instead of the bar being set beside it as a separate accent
    // (the clivis whose descent flicks into a tenuto, the virga that flags off at the top, the stropha
    // whose hook runs on into a tail). It applies to an episema that reaches OUT from the note to one side
    // - not one that sits above / below it as a lifted cap - on a stroke the pen can flow cleanly off. The
    // reach is appended to the stroke's centreline below as a smooth turn into a level run (see
    // EpisemaFoot), so InkRun sweeps note and episema as one ribbon; a lone reach is then skipped by
    // BuildEpisemata, a chained one keeps the upright(s) that cross its tip.
    auto episemaFoot = [&](size_t k) -> bool {
        const NcInfo &n = ncs[k];
        if (n.episemata.empty()) return false;
        // Only shapes the pen can flow cleanly on from at a free end - a plain note or a strophicus hook -
        // not the wavy quilisma, the looping liquescent or an s-shaped oriscus wave (@s-shape).
        if (n.quilisma || n.liquescent || !n.sShape.empty()) return false;
        // The first episema must reach OUT from the note: an explicit left / right @place, an explicit
        // horizontal @form, or the default bar of a CHAIN, which reaches out to clear space for the upright
        // that crosses it. A lone default episema stays a centred tenuto, not a one-sided foot.
        const EpisemaInfo &first = n.episemata[0];
        // A LEFT-reaching episema is never written in one go with its note. The broad nib is dragged up
        // and to the right (PULL_AXIS); a tail flicking back to the left would run against that drag, which
        // the period hand avoids - it lifts the pen and re-lays the bar as a separate RIGHT-going accent
        // instead. So a left @place drops out of the continuation foot here and is drawn by BuildEpisemata
        // as a lifted accent to the note's left (shift = -GAP). Only a left @place ever produces a leftward
        // foot (side = -1 in the foot builder is set for EVENTREL_left alone).
        if (first.place == EVENTREL_left) return false;
        const bool sideways = (first.place == EVENTREL_right); // left already dropped out above
        const bool horizontal = (first.form == episemaVis_FORM_h);
        const bool chainReach = (first.place == EVENTREL_NONE && n.episemata.size() > 1);
        if (!sideways && !horizontal && !chainReach) return false;
        return (k + 1 >= ncs.size()) || breaksBefore(k + 1); // must end its run
    };
    // The travel direction of component k's stroke (for neighbour tangents); 0 for a bare dab.
    auto chordDirOf = [&](size_t k) -> PointF {
        if (bareOf(k)) return { 0.0, 0.0 };
        PointF dir = TiltVec(EffectiveTilt(ncs[k]));
        if (dir.x == 0.0 && dir.y == 0.0) dir = TiltVec(COMPASSDIRECTION_e);
        return dir;
    };
    // The sideways nudge that keeps a connected stroke travelling in direction @p dir from retracing
    // the stroke it springs from when the two about-face (travel nearly opposite). Returns {0,0} when
    // there is no about-face. The test is purely on the previous exit tangent, so it fires for an s
    // after an n whether or not either carries a @curve, but never after a curve that already eases
    // toward this stroke (its exit tangent then points the same way, not the opposite). Only the
    // second stroke moves, so the antiparallel pair separates; the offset carries forward to the rest.
    auto aboutFaceShift = [&](PointF dir) -> PointF {
        if (prevExitDir.x == 0.0 && prevExitDir.y == 0.0) return { 0.0, 0.0 };
        if (prevExitDir.Dot(dir) >= ANTIPARALLEL_COS) return { 0.0, 0.0 };
        PointF perp = { -dir.y, dir.x };
        // Bias the nudge rightward (and downward for a level stroke) so the parallel pair sits side by
        // side in reading order rather than drifting left.
        if (perp.x < -1e-9 || (std::fabs(perp.x) < 1e-9 && perp.y < 0.0)) perp = { -perp.x, -perp.y };
        return { perp.x * ANTIPARALLEL_SHIFT, perp.y * ANTIPARALLEL_SHIFT };
    };

    // The vertical reach of component k's stroke, in pen px: one level (an interline) scaled by @rellen.
    // This is the foot-to-head height the stroke must span whatever its angle, so feet and heads land on
    // the shared level grid. The horizontal advance is left to follow from the stroke's @tilt aspect.
    auto reachOf = [&](size_t k) -> double { return LEVEL_STEP * RellenFactor(ncs[k].longStroke, ncs[k].shortStroke); };
    // Length along @p dir needed to span that vertical reach at the stroke's own angle. A level (e/w)
    // stroke has no vertical component, so it just takes the reach as a plain horizontal length.
    auto lenFor = [&](size_t k, PointF dir) -> double {
        return (std::fabs(dir.y) > 1e-6) ? reachOf(k) / std::fabs(dir.y) : reachOf(k);
    };

    // The previous gesture's ink (the just-completed connected run) resolved in an explicit @place's gap
    // frame - the axis along @p gapDir and the cross axis across it. parMin / parMax bound its reach along
    // gapDir; perMid is the midline of its cross extent, where a placed component sits so it lands over the
    // neume's CENTRE (not its foot / ligature end). `any` is false for an empty run.
    struct GapFrame {
        PointF perp;
        double parMin, parMax, perMid;
        bool any;
    };
    auto placeFrame = [&](const std::vector<Seg> &run, PointF gapDir) -> GapFrame {
        const PointF perp = gapDir.Perp();
        bool any = false;
        double parMin = 0.0, parMax = 0.0, perMin = 0.0, perMax = 0.0;
        for (const Seg &seg : run) {
            for (const PointF &p : seg.pts) {
                const double par = p.Dot(gapDir), per = p.Dot(perp);
                if (!any) {
                    parMin = parMax = par, perMin = perMax = per, any = true;
                }
                else {
                    parMin = std::min(parMin, par), parMax = std::max(parMax, par);
                    perMin = std::min(perMin, per), perMax = std::max(perMax, per);
                }
            }
        }
        return { perp, parMin, parMax, (perMin + perMax) / 2.0, any };
    };

    // The tightest centre-to-centre offset along @p gapDir at which a parallel repeat - the previous run
    // translated (cross-aligned) onto the placed spot - still holds a @p gap perpendicular clearance from
    // its predecessor. Parallel shapes keep a constant perpendicular gap however far one slides along the
    // axis, so a fixed centre distance (REPEAT_DX) over-spaces a shallow shape (two flat chevrons stacked)
    // while crowding a steep one; solving per point-pair for the offset that yields a nib-sized gap nestles
    // them tight at any angle. For pair (a, b) with difference d, the translated a clears b by @p gap when
    // |d + t*gapDir| >= gap; the binding t is -(d.gapDir) + sqrt(gap^2 - d_perp^2) over pairs that can touch
    // (d_perp <= gap). The a == b pair alone forces t >= gap, so the result is never below the gap itself.
    auto nestleOffset = [&](const std::vector<Seg> &run, PointF gapDir, double gap) -> double {
        const double gap2 = gap * gap;
        double t = 0.0;
        for (const Seg &sa : run) {
            for (const PointF &a : sa.pts) {
                for (const Seg &sb : run) {
                    for (const PointF &b : sb.pts) {
                        const double dx = a.x - b.x, dy = a.y - b.y;
                        const double dg = dx * gapDir.x + dy * gapDir.y;
                        const double disc = gap2 - (dx * dx + dy * dy - dg * dg); // gap^2 - d_perp^2
                        if (disc <= 0.0) continue; // this pair is already more than a gap apart across the axis
                        t = std::max(t, -dg + std::sqrt(disc));
                    }
                }
            }
        }
        return t;
    };

    for (size_t i = 0; i < ncs.size(); ++i) {
        const NcInfo &nc = ncs[i];
        const int tilt = EffectiveTilt(nc);
        const bool hasTilt = (tilt != COMPASSDIRECTION_NONE);
        const bool hasExplicitTilt = (nc.tilt != COMPASSDIRECTION_NONE); // @tilt, not an @intm default
        // @angled draws a curved stroke as a sharp chevron instead of a bow, so it travels every path a
        // @curve does. With no explicit @curve it defaults to the clockwise (c) hand, so a bare @angled
        // reads as "@curve='c', but angled" - the curveHand used everywhere the bow's side is set.
        const bool hasCurve = (nc.curve != curvatureDirection_CURVE_NONE) || nc.angled;
        const int curveHand = (nc.curve != curvatureDirection_CURVE_NONE) ? nc.curve : curvatureDirection_CURVE_c;
        const bool hasSShape = !nc.sShape.empty();
        // A liquescent with no explicit @tilt has no melodic stroke of its own: the whole note is a
        // curl hooking off the previous nc. With @tilt it draws its own stroke and curls at the tip.
        // Only a connected liquescent can be a whole-note hook - a first or detached one has nothing
        // to hook onto, so it falls back to drawing its own stroke (handled at the call sites below).
        const bool wholeHook = nc.liquescent && !hasExplicitTilt;
        // A bare component carries no shape-determining attribute: no @tilt (an @intm default does not
        // count - it only places the dab), no @curve/@s-shape, no ornament child. With neither @tilt nor
        // @con it is simply a point, so it is rendered as a detached punctum stepping along the melodic
        // contour (@intm) - a run of plain ncs then reads as separate gapped puncta, a clearer picture of
        // the melody than one connected ligature.
        const bool bare = bareOf(i);
        const bool brk = breaksBefore(i); // detached: an explicit gap, or a bare punctum
        // Length that makes this stroke span exactly its @rellen-scaled level reach at its own @tilt
        // angle, so its foot and head land on the shared grid (a diagonal is correspondingly longer than
        // a vertical). The few non-melodic shapes that ignore `len` (quilisma, strophicus) are unaffected.
        const double len = lenFor(i, TiltVec(tilt));

        std::vector<PointF> pts = { pen };
        bool dot = false;
        int strokeStart = 0; // index into pts where the nc's own stroke begins (past a connection loop)

        if (i == 0 || brk) {
            // A fresh pen gesture. The first component sits at the origin; a detached one (@con="g") is
            // placed by stepping from the previous component's anchor along the melodic contour (@intm) or
            // in an explicit @place direction. The bivirga exception below is centred like the first.
            const PointF stepDir = chordDirOf(i);
            // The direction the pen jumps to set this detached component down: an explicit @place wins,
            // otherwise the @intm melodic contour supplies the default (u->ne, d->se, s->e). @place lets a
            // gap step where @intm cannot name it - straight above (n) or below (s) - or makes the implicit
            // @intm placement explicit.
            const bool hasPlace = (nc.place != COMPASSDIRECTION_NONE);
            const int gapTilt = hasPlace ? nc.place : IntmGapTilt(nc.intm);
            // A repeated same-pitch stroke parallel to its predecessor (a bivirga) is the exception: it
            // slides tight sideways and is centred like the first component, so the pair's feet share one
            // horizontal line and their tips another, instead of stacking up a diagonal. An explicit
            // @place opts out - it is honoured as a literal gap step, not second-guessed into a slide.
            const bool levelRepeat
                = brk && !hasPlace && (nc.intm == 's' || nc.intm == 0) && prevExitDir.Dot(stepDir) > PARALLEL_COS;
            // Centred on the anchor for the first component, a level repeat, and an explicit @place (which
            // sets the whole component down centred over the placed spot, like a first stroke relocated).
            // A @intm-stepped detached component is foot-anchored at the gap point so it continues up/down
            // the contour.
            const bool centred = !brk || levelRepeat || hasPlace;
            PointF anchor = { 0.0, 0.0 };
            if (levelRepeat) {
                anchor = { lastAnchor.x + REPEAT_DX, lastAnchor.y };
            }
            else if (brk) {
                PointF gapDir = TiltVec(gapTilt);
                if (gapDir.x == 0.0 && gapDir.y == 0.0) gapDir = TiltVec(COMPASSDIRECTION_e);
                if (hasPlace && !runs.empty()) {
                    // An explicit @place is set down relative to the whole previous gesture, centred on
                    // its cross-axis: "above" lands over the neume's centre, not its foot / ligature end.
                    // The @intm contour default keeps the foot-relative step below.
                    const GapFrame f = placeFrame(runs.back(), gapDir);
                    // A @place repeat of a single parallel stroke - two stacked chevrons, a punctum above
                    // a like punctum - follows the bivirga rule: parallel shapes keep a constant separation
                    // however far one slides along the gap, so they nestle tight (the upper's feet dropping
                    // in beside the lower's peak) instead of clearing the full peak-high reach. Only when the
                    // previous run is one stroke of the same tilt.
                    const bool parallelRepeat = f.any && runs.back().size() == 1 && !bare
                        && (tilt != COMPASSDIRECTION_NONE) && (EffectiveTilt(ncs[i - 1]) == tilt);
                    double par;
                    if (parallelRepeat) {
                        // Offset centre to centre by just enough that the two keep a nib-sized perpendicular
                        // gap (a hairline of white, so they still read as two strokes) - tight at any angle,
                        // where a fixed REPEAT_DX would over-space this shallow chevron.
                        par = (f.parMin + f.parMax) / 2.0 + nestleOffset(runs.back(), gapDir, NESTLE_GAP);
                    }
                    else {
                        // Clear the previous neume's far edge by BREAK_GAP. Since @place draws the component
                        // CENTRED on the anchor, add half its own reach along the gap, so its near edge still
                        // clears - matters only when the stroke travels along the gap (a virga placed
                        // straight above); a cross-running stroke (tilt="e" placed above) projects to zero.
                        const PointF td = TiltVec(tilt);
                        par = f.parMax + BREAK_GAP + 0.5 * len * std::fabs(td.x * gapDir.x + td.y * gapDir.y);
                    }
                    if (f.any) anchor = { gapDir.x * par + f.perp.x * f.perMid, gapDir.y * par + f.perp.y * f.perMid };
                }
                else {
                    // Step along the contour, clearing the previous stroke's forward reach (how far its
                    // ink extends along the gap direction) so an ascending salicus / descending climacus
                    // stacks without colliding. A punctum has no reach, so plain subpuncta step by
                    // BREAK_GAP alone.
                    const double reach
                        = std::max(0.0, (pen.x - lastAnchor.x) * gapDir.x + (pen.y - lastAnchor.y) * gapDir.y);
                    const double advance = reach + BREAK_GAP;
                    anchor = { lastAnchor.x + gapDir.x * advance, lastAnchor.y + gapDir.y * advance };
                }
            }
            // A bare component (no @tilt and no special shape) is a single pen dab; the anchor above
            // has already stepped it along @intm so a run of plain ncs reads as separate puncta.
            if (bare) {
                pts = { anchor }; // a punctum: a single pen dab
                dot = true;
                pen = anchor;
            }
            else if (nc.quilisma) {
                // A <quilisma>: the whole note is the wavy line, running along @tilt (level by default).
                // Any @curve / @angled bends the axis the wiggle rides (a wavy bow / chevron), easing
                // toward the next component if one follows in the same gesture.
                PointF cd = TiltVec(tilt);
                if (cd.x == 0.0 && cd.y == 0.0) cd = TiltVec(COMPASSDIRECTION_e);
                const PointF tOut = (i + 1 < ncs.size() && !breaksBefore(i + 1)) ? chordDirOf(i + 1) : cd;
                Stroke sh = Quilisma(anchor, tilt, nc.waves, centred, curveHand, hasCurve, nc.angled, cd, tOut);
                pts = sh.pts;
                pen = sh.exit;
            }
            else if (nc.liquescent) {
                // A fresh-gesture liquescent has no preceding stroke to hook onto, so it always draws
                // its own lead-in stroke and curls at the tip (the whole-note hook needs a previous nc).
                // The <nc>'s @curve bows that lead-in into a curved stroke flowing into the curl; the
                // curl hand comes from the <liquescent>'s own @curve, falling back to the <nc>'s @curve.
                const int curlHand
                    = (nc.liquescentCurve != curvatureDirection_CURVE_NONE) ? nc.liquescentCurve : curveHand;
                if (hasCurve) {
                    PointF cd = TiltVec(tilt);
                    if (cd.x == 0.0 && cd.y == 0.0) cd = TiltVec(COMPASSDIRECTION_e);
                    const PointF tOut = (i + 1 < ncs.size() && !breaksBefore(i + 1)) ? chordDirOf(i + 1) : cd;
                    Stroke sh = CurvedLoop(
                        anchor, tilt, curveHand, len, cd, tOut, curlHand, nc.liquescentLooped, LIQ_CURL_R, nc.angled);
                    pts = sh.pts;
                    pen = sh.exit;
                }
                else {
                    Stroke sh = Loop(anchor, TiltVec(tilt), nc.liquescentCurve, nc.liquescentLooped, len, LIQ_CURL_R);
                    pts = sh.pts;
                    pen = sh.exit;
                }
            }
            else if (hasSShape) {
                // Centred on the anchor when first or a level repeat; a stepped detached wave springs
                // forward from the gap anchor so it stacks up the contour (the oriscus of a salicus).
                Stroke sh = centred ? Wave(anchor.x, anchor.y, nc.sShape, tilt) : WaveFrom(anchor, nc.sShape, tilt);
                pts = sh.pts;
                pen = sh.exit;
            }
            else if (nc.strophicus) {
                Stroke sh = Comma(anchor.x, anchor.y, tilt); // stropha hook (a strophicus is always a comma)
                pts = sh.pts;
                pen = sh.exit;
            }
            else if (hasCurve) {
                // Standalone curved stroke: chord centred on the anchor, easing toward the next
                // component if one follows in the same gesture. Its chord honours @rellen like any
                // other stroke (short / medium / long).
                const double clen = len;
                PointF cd = TiltVec(tilt);
                if (cd.x == 0.0 && cd.y == 0.0) cd = TiltVec(COMPASSDIRECTION_e);
                // Centred on the anchor when first / a level repeat; a stepped detached curve starts at
                // the gap anchor and travels forward.
                const PointF cs = centred ? PointF{ anchor.x - cd.x * clen / 2, anchor.y - cd.y * clen / 2 } : anchor;
                PointF tOut = (i + 1 < ncs.size() && !breaksBefore(i + 1)) ? chordDirOf(i + 1) : cd;
                Stroke sh = CurvedStroke(cs, tilt, curveHand, clen, cd, tOut, nc.angled);
                pts = sh.pts;
                pen = sh.exit;
            }
            else if (hasTilt) {
                // Centred on the anchor when first or a level repeat (so two same-pitch virgae line up);
                // a stepped detached stroke is foot-anchored at the gap point and travels forward.
                const PointF d = TiltVec(tilt);
                const PointF c = centred ? anchor : PointF{ anchor.x + d.x * len / 2, anchor.y + d.y * len / 2 };
                Stroke sh = ObliqueShape(c.x, c.y, d, len);
                pts = sh.pts;
                pen = sh.exit;
            }
            else {
                pts = { anchor }; // an ornament-only component: nothing to ink, just hold the anchor
                pen = anchor;
            }
            lastAnchor = anchor;
        }
        else {
            // A connected following component: a calligraphic stroke steered by its own @curve and
            // @tilt (or @intm's default direction), starting where the previous component ended so
            // the ligature reads as one continuous gesture.
            PointF dir = TiltVec(tilt);
            if (dir.x == 0.0 && dir.y == 0.0) dir = TiltVec(COMPASSDIRECTION_e);
            // A looped connection (@con="l"): before this component's stroke, the pen winds through a
            // small cursive loop at the joint - arriving along the previous exit tangent, crossing its
            // own path and leaving along the direction this stroke actually departs in (see LoopJoint).
            // The stroke then springs from the loop's exit; the exit tangent IS its departure, so the
            // stroke builders below (the about-face nudge, the neighbour-tangent handoffs) all see a
            // clean straight join and simply flow on. The loop's ink belongs to this nc (prepended
            // below). A whole-note-hook liquescent is exempt: its curl already coils at this very
            // joint, so the curl IS the loop - a second ring would only knot the two into a blot.
            std::vector<PointF> loopPts;
            if (nc.looped && !wholeHook) {
                // Every stroke builder departs along the chord except an @angled chevron, whose first
                // leg leaves 45 degrees toward the bend side (see CurvedStroke's angled branch); the
                // loop must stop where THAT tangent is reached or the ring meets the leg in a kink.
                PointF depart = dir;
                if (nc.angled) {
                    const double sg = (curveHand == curvatureDirection_CURVE_a) ? 1.0 : -1.0;
                    depart = PointF{ dir.x - sg * dir.y, dir.y + sg * dir.x }.Unit(dir);
                }
                const PointF enter = (prevExitDir.x != 0.0 || prevExitDir.y != 0.0) ? prevExitDir : depart;
                loopPts = LoopJoint(pen, enter, depart, LOOP_JOIN_R);
                pen = loopPts.back();
                prevExitDir = depart;
            }
            if (nc.quilisma) {
                // Within a ligature the whole component is the wavy line, springing from the pen and
                // running along @tilt (level by default). Any @curve / @angled bends the axis it rides,
                // springing from the previous stroke's exit tangent and easing toward the next.
                const PointF tIn = (prevExitDir.x != 0.0 || prevExitDir.y != 0.0) ? prevExitDir : chordDirOf(i);
                const PointF tOut = (i + 1 < ncs.size() && !breaksBefore(i + 1)) ? chordDirOf(i + 1) : chordDirOf(i);
                Stroke sh = Quilisma(pen, tilt, nc.waves, false, curveHand, hasCurve, nc.angled, tIn, tOut);
                pts = sh.pts;
                pen = sh.exit;
            }
            else if (nc.liquescent) {
                if (wholeHook) {
                    // No @tilt: the whole note is a curl hooking off the previous nc. With no lead-in
                    // it springs straight from the pen, following the previous stroke's exit tangent
                    // into the curl so the hook flows out of the ligature instead of starting askew.
                    const PointF enter = (prevExitDir.x != 0.0 || prevExitDir.y != 0.0) ? prevExitDir : dir;
                    Stroke sh = Loop(pen, enter, nc.liquescentCurve, nc.liquescentLooped, 0.0, LIQ_HOOK_R);
                    pts = sh.pts;
                    pen = sh.exit;
                }
                else {
                    // A liquescent with its own melodic stroke (explicit @tilt). The <nc>'s @curve bows
                    // that lead-in - springing from the previous stroke's exit tangent and easing toward
                    // the next, with the same about-face nudge as a plain curved follow-up - then the curl
                    // springs off the bow's tip. The curl hand comes from the <liquescent>'s own @curve,
                    // falling back to the <nc>'s @curve.
                    const int curlHand
                        = (nc.liquescentCurve != curvatureDirection_CURVE_NONE) ? nc.liquescentCurve : curveHand;
                    if (hasCurve) {
                        const PointF off = aboutFaceShift(dir);
                        const PointF start = { pen.x + off.x, pen.y + off.y };
                        const PointF tIn = (prevExitDir.x != 0.0 || prevExitDir.y != 0.0) ? prevExitDir : chordDirOf(i);
                        const PointF tOut
                            = (i + 1 < ncs.size() && !breaksBefore(i + 1)) ? chordDirOf(i + 1) : chordDirOf(i);
                        Stroke sh = CurvedLoop(start, tilt, curveHand, len, tIn, tOut, curlHand, nc.liquescentLooped,
                            LIQ_CURL_R, nc.angled);
                        pts = sh.pts;
                        pen = sh.exit;
                    }
                    else {
                        Stroke sh = Loop(pen, dir, nc.liquescentCurve, nc.liquescentLooped, len, LIQ_CURL_R);
                        pts = sh.pts;
                        pen = sh.exit;
                    }
                }
            }
            else if (hasSShape) {
                Stroke sh = WaveFrom(pen, nc.sShape, tilt); // connected oriscus / quassus (virga strata)
                pts = sh.pts;
                pen = sh.exit;
            }
            else if (nc.strophicus) {
                Stroke sh = Comma(pen.x, pen.y, tilt); // stropha hook within a ligature
                pts = sh.pts;
                pen = sh.exit;
            }
            else if (hasCurve) {
                // Curved follow-up: spring from the previous stroke's exit tangent and ease toward
                // the next component's direction (a smooth join, not an independent scoop). If it
                // about-faces off its predecessor (a descending curve retracing the ascent it leaves)
                // it is nudged sideways like a plain stroke so the two limbs run parallel.
                const PointF off = aboutFaceShift(dir);
                const PointF start = { pen.x + off.x, pen.y + off.y };
                const PointF tIn = (prevExitDir.x != 0.0 || prevExitDir.y != 0.0) ? prevExitDir : chordDirOf(i);
                const PointF tOut = (i + 1 < ncs.size() && !breaksBefore(i + 1)) ? chordDirOf(i + 1) : chordDirOf(i);
                Stroke sh = CurvedStroke(start, tilt, curveHand, len, tIn, tOut, nc.angled);
                pts = sh.pts;
                pen = sh.exit;
            }
            else {
                // A plain straight stroke springing from the previous one. If it about-faces -
                // travelling (nearly) opposite to the stroke it leaves (an s after an n, a sw after a
                // ne) - it would otherwise retrace that stroke's line and the two would collapse into a
                // single mark. Nudge its whole centreline sideways so it runs parallel beside its
                // predecessor instead; the lateral gap survives InkRun's joint-merge and the run's
                // Catmull-Rom smoothing rounds the turn into a hook.
                const PointF off = aboutFaceShift(dir);
                const PointF start = { pen.x + off.x, pen.y + off.y };
                const PointF end = { start.x + dir.x * len, start.y + dir.y * len };
                pts = { start, { (start.x + end.x) / 2, (start.y + end.y) / 2 }, end };
                pen = end;
            }
            // Prepend the connection loop so the joint's ink is swept with - and attributed to - this
            // component. The stroke's first point usually repeats the loop's exit; the duplicate is
            // dropped so the joint knot is not doubled (a doubled knot reads as a deliberate corner
            // to the centripetal resampling in InkRun, and this join must stay smooth).
            if (!loopPts.empty()) {
                const bool dup = !pts.empty()
                    && std::hypot(pts.front().x - loopPts.back().x, pts.front().y - loopPts.back().y) < 1e-6;
                pts.insert(pts.begin(), loopPts.begin(), dup ? loopPts.end() - 1 : loopPts.end());
                strokeStart = (int)loopPts.size() - (dup ? 1 : 0);
            }
            lastAnchor = pen; // a later detached component gaps from the ligature's end
        }

        // Remember this component's exit tangent so a following curved stroke can spring from it
        // with a continuous direction (the heart of the smooth join). Read off `pts` before it is
        // moved into the run below.
        prevExitDir = { 0.0, 0.0 };
        if (pts.size() >= 2) {
            const PointF a = pts[pts.size() - 2], b = pts.back();
            const double mx = b.x - a.x, my = b.y - a.y, mm = std::hypot(mx, my);
            if (mm > 1e-9) prevExitDir = { mx / mm, my / mm };
        }

        // Episema foot: a continuation episema on this run-ending stroke is the tail of the same gesture,
        // not a separate accent. Extend the stroke's centreline with the foot - built from its true exit
        // direction, pre-slant - so InkRun sweeps stroke and foot as one ribbon. For a lone episema
        // BuildEpisemata then skips it; for a chain it crosses the foot's tip with the remaining upright(s).
        // The foot side is the first episema's @place (left, else right). prevExitDir is read above
        // (pre-foot) so a later detached component still gaps from the stroke, not the foot tip.
        bool footEpisema = false;
        int footStart = -1;
        if (!dot && episemaFoot(i)) {
            const EpisemaInfo &ep = nc.episemata[0];
            const int side = (ep.place == EVENTREL_left) ? -1 : 1; // left -> -1; right / default -> +1
            const PointF exitDir = (prevExitDir.x != 0.0 || prevExitDir.y != 0.0) ? prevExitDir : TiltVec(tilt);
            // The episema stands at 90° to its base stroke, reaching toward the @place side. A default
            // "vertical" episema takes the real stroke (its exit tangent); an explicit @form="h" takes a
            // nominal vertical stroke, so its 90° is a globally horizontal tail. runDir is that base's
            // quarter-turn, flipped to the side.
            const PointF base = (ep.form == episemaVis_FORM_h) ? PointF{ 0.0, 1.0 } : exitDir.Unit({ 0.0, 1.0 });
            PointF runDir = base.Perp();
            if (runDir.x * side < 0.0) runDir = runDir * -1.0;
            const std::vector<PointF> foot = EpisemaFoot(pen, exitDir, runDir);
            footStart = (int)pts.size();
            pts.insert(pts.end(), foot.begin(), foot.end());
            pen = pts.back();
            footEpisema = true;
        }

        // Start a new run on a gap, and isolate a punctum into its own run so its single dab is never
        // folded into a neighbouring continuous gesture.
        const bool prevWasDot = !runs.empty() && !runs.back().empty() && runs.back().back().isDot;
        if (runs.empty() || brk || dot || prevWasDot) runs.push_back({});
        runs.back().push_back({ std::move(pts), tilt, (int)i, dot, footEpisema, strokeStart, footStart });
    }

    // 2) Ink each run as one continuous gesture, cut into per-nc slices.
    geo.ncs.resize(ncs.size());
    for (std::vector<Seg> &run : runs) {
        InkRun(run, geo, slant);
    }

    // 3) Episemata: short accent strokes set just clear of the ink they mark (see BuildEpisemata).
    BuildEpisemata(runs, ncs, geo, slant);

    // 4) Anchor the gesture at its ink's left edge. Strokes are centred on their cells while
    //    building, so the raw gesture reaches left of the origin; the caller aligns the origin with
    //    the syllable's alignment point (where the syl text also starts), and ink left of it would
    //    reach back over the preceding neume - which after cast-off lives in its own measure, where
    //    no X-adjustment can push this one clear of it.
    double inkLeft = std::numeric_limits<double>::max();
    for (const NcGeometry &nc : geo.ncs) {
        for (const PointF &p : nc.ribbon) inkLeft = std::min(inkLeft, p.x);
        for (const std::vector<PointF> &epi : nc.episemata)
            for (const PointF &p : epi) inkLeft = std::min(inkLeft, p.x);
    }
    if (inkLeft != std::numeric_limits<double>::max()) {
        for (NcGeometry &nc : geo.ncs) {
            for (PointF &p : nc.ribbon) p.x -= inkLeft;
            for (std::vector<PointF> &epi : nc.episemata)
                for (PointF &p : epi) p.x -= inkLeft;
        }
        for (StrokePath &s : geo.strokes)
            for (PointF &p : s.pts) p.x -= inkLeft;
    }

    // 5) Scale from prototype pixels into verovio drawing units.
    for (NcGeometry &nc : geo.ncs) {
        for (PointF &p : nc.ribbon) p = { p.x * scale, p.y * scale };
        for (std::vector<PointF> &epi : nc.episemata)
            for (PointF &p : epi) p = { p.x * scale, p.y * scale };
    }
    for (StrokePath &s : geo.strokes)
        for (PointF &p : s.pts) p = { p.x * scale, p.y * scale };

    return geo;
}

} // namespace vrv
