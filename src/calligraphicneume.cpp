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
// doubles it (a leap), s halves it (a small step). The horizontal advance follows from the stroke's
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
// Liquescent curl radii. A liquescent that carries its own melodic stroke ends in a small terminal
// flourish; one with no @tilt is the whole note rendered as a hook off the previous nc, so its curl
// is note-sized rather than a tiny ornament.
constexpr double LIQ_CURL_R = MED * 0.30; // terminal curl at the tip of a liquescent's own stroke
constexpr double LIQ_HOOK_R = MED * 0.50; // a whole-note hook (no lead-in stroke): note-sized
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

// How many levels a stroke reaches, from @rellen: a normal stroke spans one level, a long one (l) two
// (a leap), a short one (s) half (a small step).
double RellenFactor(bool longStroke, bool shortStroke)
{
    return longStroke ? 2.0 : (shortStroke ? 0.5 : 1.0);
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

//----------------------------------------------------------------------------
// Scribal ductus: attack & release weighting, and the forward (italic) slant
//----------------------------------------------------------------------------

// Hermite smoothstep on [0, 1] (clamped): the classic 3t^2 - 2t^3 ease, used to shape the stroke's
// attack and release shoulders.
double SmoothStep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Scribal attack & release. A real pen LANDS firm - a blunt attack, not a hairline birth - and LIFTS
// slowly into a hairline, so the along-stroke weight is asymmetric: a short attack shoulder rising
// from ATTACK_LAND to full, then a long release shoulder tapering down to RELEASE_LIFT.
constexpr double ATTACK_LAND = 0.85; // weight the instant the pen lands
constexpr double ATTACK_LEN = 0.18; // fraction of the gesture over which the attack settles to full
constexpr double RELEASE_LIFT = 0.15; // weight the instant the pen leaves (the lifted tail)
constexpr double RELEASE_LEN = 0.55; // fraction of the gesture over which the release tapers away

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
CalligraphicNeume::PointF SlantShear(CalligraphicNeume::PointF p, CalligraphicNeume::PointF t, CalligraphicNeume::Slant s)
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
    if (nc.tilt != COMPASSDIRECTION_NONE) return nc.tilt; // an explicit @tilt: the quilismapes ascent
    // A quilisma with no @tilt is the whole wavy note: it runs level, so it defaults to e rather than
    // taking the @intm direction (u would otherwise tilt it ne). With a @tilt the wiggle is only a
    // preamble and that @tilt (handled above) steers the ascent the note then makes.
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
// law w proportional to |T x n̂|, modulated by an along-stroke taper. Computed over the whole
// gesture so the taper and the tangents are continuous before any per-nc slicing.
void CalligraphicNeume::NibEdges(const std::vector<PointF> &pts, double scale, std::vector<PointF> &left,
    std::vector<PointF> &right, bool taperStart, bool taperEnd)
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
        const double tc = std::clamp((double)i / (n - 1), 0.0, 1.0);
        // Scribal attack & release. The pen lands firm and lifts slowly, so the along-stroke weight is
        // the product of two shoulders: a short attack rising from ATTACK_LAND to full, and a long
        // release tapering down to the RELEASE_LIFT hairline. taperStart/taperEnd drop the matching
        // shoulder to hold that end at full width (an episema accent keeps both ends square).
        const double attack = taperStart ? ATTACK_LAND + (1.0 - ATTACK_LAND) * SmoothStep(tc / ATTACK_LEN) : 1.0;
        const double release = taperEnd ? RELEASE_LIFT + (1.0 - RELEASE_LIFT) * SmoothStep((1.0 - tc) / RELEASE_LEN) : 1.0;
        const double cap = attack * release;
        const double w = ((NIB_MIN + (NIB_W - NIB_MIN) * sinA) * scale * cap) / 2.0;
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
            p[i] = { 0.25 * prev.x + 0.5 * cur.x + 0.25 * p[i + 1].x,
                0.25 * prev.y + 0.5 * cur.y + 0.25 * p[i + 1].y };
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
    const std::vector<PointF> diamond = { { c.x + nib.x * LA, c.y + nib.y * LA },
        { c.x + perp.x * SA, c.y + perp.y * SA }, { c.x - nib.x * LA, c.y - nib.y * LA },
        { c.x - perp.x * SA, c.y - perp.y * SA } };
    return SmoothClosed(diamond);
}

// A smooth filled outline for the fixed nib swept along a whole centreline.
std::vector<CalligraphicNeume::PointF> CalligraphicNeume::NibRibbon(
    const std::vector<PointF> &pts, double scale, bool taperStart, bool taperEnd)
{
    std::vector<PointF> left, right;
    NibEdges(pts, scale, left, right, taperStart, taperEnd);
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
// exiting low so a following ascent can spring from it); the @s-shape value sets the wiggle (flat
// for "n"). Without @tilt it defaults to a vertical S ("n" -> horizontal).
CalligraphicNeume::Stroke CalligraphicNeume::Wave(double x, double y, const std::string &orient, int tilt)
{
    const std::string o = orient.empty() ? "w" : orient;
    PointF dir = TiltVec(tilt);
    if (dir.x == 0.0 && dir.y == 0.0) dir = (o == "n") ? TiltVec(COMPASSDIRECTION_e) : TiltVec(COMPASSDIRECTION_n);
    const double len = (o == "n") ? 24.0 : 22.0;
    // centre the wave on (x, y) by backing the start off by half its travel
    return WaveFrom({ x - dir.x * len / 2, y - dir.y * len / 2 }, orient, tilt);
}

// An S-shaped wave starting at s, travelling along @tilt. Used for a connected oriscus / quassus
// inside a ligature (e.g. the second component of a virga strata), where the wave must spring from
// the previous stroke's tip rather than be centred on a cell.
CalligraphicNeume::Stroke CalligraphicNeume::WaveFrom(PointF s, const std::string &orient, int tilt)
{
    const std::string o = orient.empty() ? "w" : orient;
    PointF dir = TiltVec(tilt);
    if (dir.x == 0.0 && dir.y == 0.0) dir = (o == "n") ? TiltVec(COMPASSDIRECTION_e) : TiltVec(COMPASSDIRECTION_n);
    const double len = (o == "n") ? 26.0 : 24.0;
    // The amplitude must clear the broad nib (NIB_W = 6 px) comfortably, otherwise the sweep absorbs
    // the wiggle into the ribbon width and the S stops reading as an S.
    const double amp = (o == "n") ? 9.0 : 12.0;
    const PointF e = { s.x + dir.x * len, s.y + dir.y * len };
    const double px = -dir.y, py = dir.x; // perpendicular: the wiggle axis
    const PointF c1 = { s.x + dir.x * len * 0.3 + px * amp, s.y + dir.y * len * 0.3 + py * amp };
    const PointF c2 = { e.x - dir.x * len * 0.3 - px * amp, e.y - dir.y * len * 0.3 - py * amp };
    return { Cubic(s, c1, c2, e), e };
}

// quilisma: a wavy horizontal flourish modelled on the liquescent (see the header). The wiggle ALWAYS
// runs level (east) - the broad nib swept over it draws the toothed quilisma - landing at the top of
// the first crest and dipping from there (so the stroke opens on a wave, not a half-swing up to one)
// and lifting at the bottom of the last trough. @p rise switches the two readings of a <quilisma>:
// false makes the WHOLE note the wavy line; true makes it a PREAMBLE that then sweeps up out of the
// last trough toward @p tilt over @p len - the ascent of a quilismapes, internal to this one nc.
CalligraphicNeume::Stroke CalligraphicNeume::Quilisma(PointF s, int tilt, int waves, double len, bool rise, bool centred)
{
    if (waves < 1) waves = 2; // default number of crests
    const PointF east = TiltVec(COMPASSDIRECTION_e); // the wiggle always runs level, whatever the @tilt
    constexpr double WAVE_LEN = 13.0; // travel per crest (along the stroke)
    constexpr double AMP = 7.5; // perpendicular swing of the wiggle (rounder, taller humps)
    constexpr int PER = 10; // samples per crest
    const double total = WAVE_LEN * waves;
    // A standalone wavy note (@p centred) sits on its anchor, so back the preamble off by half its
    // length; a connected or stepped flourish springs forward from @p s.
    const PointF foot = centred ? PointF{ s.x - east.x * total / 2, s.y } : s;
    const int n = waves * PER;
    // Phase the swing so it starts at a crest (top, -AMP) and ends at a trough (bottom, +AMP): an
    // integer count of crests with half a cycle to spare puts the lift exactly at the final trough,
    // its tangent momentarily level, so the ascent that follows springs cleanly out of the low point.
    const double cycles = (double)waves - 0.5;
    std::vector<PointF> pts;
    pts.reserve(n + 1 + 25);
    for (int i = 0; i <= n; ++i) {
        const double t = (double)i / n; // 0..1 along the travel
        const double off = -AMP * std::cos(2.0 * M_PI * cycles * t); // top -> ... -> bottom
        pts.push_back({ foot.x + east.x * total * t, foot.y + off }); // east's perpendicular is straight down
    }
    if (rise) {
        // The note's own ascent, springing out of the final trough toward @tilt. The start tangent is
        // level (continuing the wavy baseline) and the end tangent runs along @tilt, so the swoosh
        // leaves shallow and steepens as it climbs.
        PointF dir = TiltVec(tilt);
        if (dir.x == 0.0 && dir.y == 0.0) dir = east;
        const PointF base = pts.back();
        const PointF end = { base.x + dir.x * len, base.y + dir.y * len };
        constexpr double kFoot = 0.45; // horizontal foot handle: holds the sweep low before it lifts
        constexpr double kHead = 0.40; // head handle along the note's own rise
        const PointF c1 = { base.x + east.x * len * kFoot, base.y + east.y * len * kFoot };
        const PointF c2 = { end.x - dir.x * len * kHead, end.y - dir.y * len * kHead };
        const std::vector<PointF> arc = Cubic(base, c1, c2, end, 24);
        pts.insert(pts.end(), arc.begin() + 1, arc.end()); // skip the base point, already in pts
    }
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
    if (dm < 1e-9) d = TiltVec(COMPASSDIRECTION_se);
    else d = { d.x / dm, d.y / dm }; // a unit travel direction, whatever its source

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
    if (dm < 1e-9) d = TiltVec(COMPASSDIRECTION_se);
    else d = { d.x / dm, d.y / dm }; // a unit travel direction, whatever its source

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

// A gentle, neighbour-aware curved stroke (see header). The chord runs s -> s + dir*len along
// @tilt; @curve sets the bow side. The end tangents are aligned to the incoming (@p tIn) and
// outgoing (@p tOut) travel directions so the stroke flows smoothly out of the previous component
// and into the next instead of scooping independently (the cause of the old over-curved look).
CalligraphicNeume::Stroke CalligraphicNeume::CurvedStroke(
    PointF s, int tilt, int hand, double len, PointF tIn, PointF tOut)
{
    PointF d = TiltVec(tilt);
    if (d.x == 0.0 && d.y == 0.0) d = TiltVec(COMPASSDIRECTION_e);
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
    const PointF e = { s.x + d.x * len, s.y + d.y * len };
    const double px = -d.y, py = d.x; // chord perpendicular = bow axis
    const double sigma = (hand == curvatureDirection_CURVE_a) ? 1.0 : -1.0; // a anticlockwise, c clockwise

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
CalligraphicNeume::Stroke CalligraphicNeume::CurvedLoop(
    PointF s, int tilt, int curveHand, double len, PointF tIn, PointF tOut, int curlHand, bool looped, double r0)
{
    const Stroke lead = CurvedStroke(s, tilt, curveHand, len, tIn, tOut);
    std::vector<PointF> pts = lead.pts;
    // The curl leaves the bow tangent to its exit direction (the last leg of the cubic), so curve and
    // curl join without a kink; fall back to the chord direction for a degenerate one-point lead-in.
    PointF d = TiltVec(tilt);
    if (pts.size() >= 2) d = (pts.back() - pts[pts.size() - 2]).Unit(d);
    const std::vector<PointF> curl = Curl(pts.back(), d, curlHand, looped, r0);
    pts.insert(pts.end(), curl.begin(), curl.end());
    return { pts, pts.back() };
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
        return;
    }
    if (run.size() == 1) {
        std::vector<PointF> pts = run[0].pts;
        if (!slant.IsNone()) SlantApply(pts, slant);
        geo.ncs[run[0].ncIndex].ribbon = NibRibbon(pts);
        return;
    }

    // Concatenate the centrelines into one spine, dropping the duplicated joint points and tracking
    // which component (run position) each spine point belongs to.
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
            spineOwners.push_back((int)si);
        }
    }

    // Densify into one smooth curve (carrying ownership), then sweep the nib over the whole run so
    // the taper and tangents stay continuous across component boundaries.
    std::vector<int> owners;
    std::vector<PointF> dense = Densify(spine, 3.0, &spineOwners, &owners);
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
        const PointF end = s.pts.back();
        // The marked stroke's travel as it reaches the marked point: its actual end tangent.
        PointF along = (s.pts.size() >= 2) ? segDir(s.pts[s.pts.size() - 2], s.pts.back())
                                           : PointF{ 0.0, 0.0 };
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
        int apexIdx = 0;
        for (int q = 1; q < (int)s.pts.size(); ++q)
            if (s.pts[q].y < s.pts[apexIdx].y) apexIdx = q;
        const bool arched = (apexIdx > 0 && apexIdx + 1 < (int)s.pts.size());
        PointF summitTan = arched ? segDir(s.pts[apexIdx - 1], s.pts[apexIdx + 1]) : PointF{ 0.0, 0.0 };
        if (summitTan.x == 0.0 && summitTan.y == 0.0) summitTan = { 1.0, 0.0 };

        for (const EpisemaInfo &e : nc.episemata) {
            const bool above = (e.place == EVENTREL_above || e.place == EVENTREL_above_left
                || e.place == EVENTREL_above_right);
            // Anchor an "above" accent on an arched stroke's summit (flat on top); otherwise on the
            // end / joint as before. @form="h" (forced along-stroke) keeps its explicit orientation.
            const bool onSummit = arched && above && (e.form != episemaVis_FORM_h);
            const PointF anchor = onSummit ? s.pts[apexIdx] : end;
            const PointF ori = (e.form == episemaVis_FORM_h) ? along : (onSummit ? summitTan : tangent);
            const double HL = (e.form == episemaVis_FORM_h) ? 7.0 : 6.0;
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
            // Centre the accent on the marked point, then set it clear of the ink in the accent's own
            // frame so the point always stays at its midpoint instead of sliding to a corner: @place's
            // vertical part (above / below) lifts it perpendicular to its length, its horizontal part
            // (left / right) shifts it along its length to one side. @up is the accent normal oriented
            // upward (pen -y); @axis runs along the accent toward +x so left / right read as expected.
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
            const double cx = anchor.x + up.x * lift + axis.x * shift;
            const double cy = anchor.y + up.y * lift + axis.y * shift;
            // Sweep the broad nib along the accent's centreline at full, untapered width: a short
            // stroke in the same hand as the neume, set just clear of the ink. The centreline is not
            // a ruled bar but a shallow concave-up dish - the broad nib drawn freehand sags in the
            // middle and flicks up at the ends - so the accent reads as a hand-drawn stroke, not a
            // straight line.
            const PointF a = { cx - ori.x * HL, cy - ori.y * HL };
            const PointF b = { cx + ori.x * HL, cy + ori.y * HL };
            // Perpendicular to the accent's length, oriented visually downward (pen +y), so the bow
            // dips the middle below the chord while the ends ride up - a concave-up valley.
            PointF perp = { -ori.y, ori.x };
            if (perp.y < 0.0) perp = { -perp.x, -perp.y };
            const double bow = HL * EPISEMA_BOW; // depth of the dish at mid-length
            constexpr int kSamples = 10;
            std::vector<PointF> centre;
            centre.reserve(kSamples + 1);
            for (int k = 0; k <= kSamples; ++k) {
                const double u = (double)k / kSamples;
                const double dip = bow * 4.0 * u * (1.0 - u); // parabola: 0 at the ends, max at mid
                centre.push_back({ a.x + (b.x - a.x) * u + perp.x * dip,
                                   a.y + (b.y - a.y) * u + perp.y * dip });
            }
            // The accent is a separate pen stroke, so it rides the same forward slant as the ribbon,
            // which also carries its anchor across to meet the leaned stroke end it marks.
            if (!slant.IsNone()) SlantApply(centre, slant);
            geo.ncs[s.ncIndex].episemata.push_back(NibRibbon(centre, EPISEMA_NIB, false, false));
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
        return (n.tilt == COMPASSDIRECTION_NONE) && (n.curve == curvatureDirection_CURVE_NONE) && n.sShape.empty()
            && !n.hasNonEpisemaChild;
    };
    // Whether component k begins a fresh pen gesture (an explicit gap or a bare punctum).
    auto breaksBefore = [&](size_t k) -> bool { return k > 0 && (ncs[k].gapped || bareOf(k)); };
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

    for (size_t i = 0; i < ncs.size(); ++i) {
        const NcInfo &nc = ncs[i];
        const int tilt = EffectiveTilt(nc);
        const bool hasTilt = (tilt != COMPASSDIRECTION_NONE);
        const bool hasExplicitTilt = (nc.tilt != COMPASSDIRECTION_NONE); // @tilt, not an @intm default
        const bool hasCurve = (nc.curve != curvatureDirection_CURVE_NONE);
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

        if (i == 0 || brk) {
            // A fresh pen gesture. The first component sits at the origin; a detached one (@con="g") is
            // placed by stepping from the previous component's anchor along the melodic contour (@intm).
            // A repeated same-pitch stroke parallel to its predecessor (a bivirga) is the exception: it
            // slides tight sideways and is centred like the first component, so the pair's feet share one
            // horizontal line and their tips another, instead of stacking up a diagonal.
            const PointF stepDir = chordDirOf(i);
            const bool levelRepeat = brk && (nc.intm == 's' || nc.intm == 0)
                && prevExitDir.Dot(stepDir) > PARALLEL_COS;
            // Centred on the anchor for the first component and a level repeat; a stepped detached
            // component is foot-anchored at the gap point so it continues up/down the contour.
            const bool centred = !brk || levelRepeat;
            PointF anchor = { 0.0, 0.0 };
            if (levelRepeat) {
                anchor = { lastAnchor.x + REPEAT_DX, lastAnchor.y };
            }
            else if (brk) {
                // Step along the contour, clearing the previous stroke's forward reach (how far its ink
                // extends along the gap direction) so an ascending salicus / descending climacus stacks
                // without colliding. A punctum has no reach, so plain subpuncta step by BREAK_GAP alone.
                PointF gapDir = TiltVec(IntmGapTilt(nc.intm));
                if (gapDir.x == 0.0 && gapDir.y == 0.0) gapDir = TiltVec(COMPASSDIRECTION_e);
                const double reach
                    = std::max(0.0, (pen.x - lastAnchor.x) * gapDir.x + (pen.y - lastAnchor.y) * gapDir.y);
                const double advance = reach + BREAK_GAP;
                anchor = { lastAnchor.x + gapDir.x * advance, lastAnchor.y + gapDir.y * advance };
            }
            // A bare component (no @tilt and no special shape) is a single pen dab; the anchor above
            // has already stepped it along @intm so a run of plain ncs reads as separate puncta.
            if (bare) {
                pts = { anchor }; // a punctum: a single pen dab
                dot = true;
                pen = anchor;
            }
            else if (nc.quilisma) {
                // A <quilisma>: the whole note is the wavy line when the nc has no @tilt; with a @tilt
                // the wiggle is a preamble that then rises in that direction (the liquescent model).
                const bool rise = hasExplicitTilt && std::fabs(TiltVec(tilt).y) > 1e-6;
                Stroke sh = Quilisma(anchor, tilt, nc.waves, len, rise, centred);
                pts = sh.pts;
                pen = sh.exit;
            }
            else if (nc.liquescent) {
                // A fresh-gesture liquescent has no preceding stroke to hook onto, so it always draws
                // its own lead-in stroke and curls at the tip (the whole-note hook needs a previous nc).
                // The <nc>'s @curve bows that lead-in into a curved stroke flowing into the curl; the
                // curl hand comes from the <liquescent>'s own @curve, falling back to the <nc>'s @curve.
                const int curlHand
                    = (nc.liquescentCurve != curvatureDirection_CURVE_NONE) ? nc.liquescentCurve : nc.curve;
                if (hasCurve) {
                    PointF cd = TiltVec(tilt);
                    if (cd.x == 0.0 && cd.y == 0.0) cd = TiltVec(COMPASSDIRECTION_e);
                    const PointF tOut = (i + 1 < ncs.size() && !breaksBefore(i + 1)) ? chordDirOf(i + 1) : cd;
                    Stroke sh
                        = CurvedLoop(anchor, tilt, nc.curve, len, cd, tOut, curlHand, nc.liquescentLooped, LIQ_CURL_R);
                    pts = sh.pts;
                    pen = sh.exit;
                }
                else {
                    Stroke sh = Loop(anchor, TiltVec(tilt), nc.liquescentCurve, nc.liquescentLooped, len, LIQ_CURL_R);
                    pts = sh.pts;
                    pen = sh.exit;
                }
            }
            else if (hasSShape || nc.oriscus) {
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
                Stroke sh = CurvedStroke(cs, tilt, nc.curve, clen, cd, tOut);
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
            if (nc.quilisma) {
                // Within a ligature the wiggle springs from the pen; with a @tilt it then rises in that
                // direction, otherwise the whole component is the wavy line (the liquescent model).
                const bool rise = hasExplicitTilt && std::fabs(TiltVec(tilt).y) > 1e-6;
                Stroke sh = Quilisma(pen, tilt, nc.waves, len, rise, false);
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
                        = (nc.liquescentCurve != curvatureDirection_CURVE_NONE) ? nc.liquescentCurve : nc.curve;
                    if (hasCurve) {
                        const PointF off = aboutFaceShift(dir);
                        const PointF start = { pen.x + off.x, pen.y + off.y };
                        const PointF tIn = (prevExitDir.x != 0.0 || prevExitDir.y != 0.0) ? prevExitDir : chordDirOf(i);
                        const PointF tOut
                            = (i + 1 < ncs.size() && !breaksBefore(i + 1)) ? chordDirOf(i + 1) : chordDirOf(i);
                        Stroke sh
                            = CurvedLoop(start, tilt, nc.curve, len, tIn, tOut, curlHand, nc.liquescentLooped, LIQ_CURL_R);
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
            else if (hasSShape || nc.oriscus) {
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
                Stroke sh = CurvedStroke(start, tilt, nc.curve, len, tIn, tOut);
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

        // Start a new run on a gap, and isolate a punctum into its own run so its single dab is never
        // folded into a neighbouring continuous gesture.
        const bool prevWasDot = !runs.empty() && !runs.back().empty() && runs.back().back().isDot;
        if (runs.empty() || brk || dot || prevWasDot) runs.push_back({});
        runs.back().push_back({ std::move(pts), tilt, (int)i, dot });
    }

    // 2) Ink each run as one continuous gesture, cut into per-nc slices.
    geo.ncs.resize(ncs.size());
    for (std::vector<Seg> &run : runs) {
        InkRun(run, geo, slant);
    }

    // 3) Episemata: short accent strokes set just clear of the ink they mark (see BuildEpisemata).
    BuildEpisemata(runs, ncs, geo, slant);

    // 4) Scale from prototype pixels into verovio drawing units.
    for (NcGeometry &nc : geo.ncs) {
        for (PointF &p : nc.ribbon) p = { p.x * scale, p.y * scale };
        for (std::vector<PointF> &epi : nc.episemata)
            for (PointF &p : epi) p = { p.x * scale, p.y * scale };
    }

    return geo;
}

} // namespace vrv
