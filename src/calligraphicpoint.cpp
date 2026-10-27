/////////////////////////////////////////////////////////////////////////////
// Name:        calligraphicpoint.cpp
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "calligraphicneume.h"

//----------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <limits>

//----------------------------------------------------------------------------

#include "atttypes.h"

namespace vrv {

//----------------------------------------------------------------------------
// Point scripts: every <nc> its own mark at its own pitch
//----------------------------------------------------------------------------

// Aquitanian, square, Nonantolan and Central Italian scribes did not write a neume as one pen gesture spanning
// several pitches: every note is a mark of its own - a dot, a square, a dash - set at its pitch, and what joins
// two marks is at most a connector arriving at the second one. So the stroke builder's rule that a connected
// stroke runs |dy| / |dir.y| to reach its pitch does not apply here; each nc is placed by its own pitch and
// the shapes around it follow from the explicit visual attributes:
//
//   - a bare nc is the hand's mark (Hand::dot) centred on its pitch; an nc with a level @tilt (e / w) is a dash
//     centred on it (the Central Italian and Aquitanian dash);
//   - an nc with a rising or falling @tilt, or a @curve / @angled, also draws a stroke ARRIVING at its pitch:
//     from the previous mark when it is connected (no @con="g") - the hairline connector of a square pes or
//     clivis, the rising stroke of an Aquitanian pes - or, on a first or detached nc, an opening stroke of the
//     hand's Opening reach that ends at the pitch (a square virga's stem, a Nonantolan tall stroke, an
//     Aquitanian virga whose pitch is its head). The mark is the stroke's head whichever way the pen ran, so
//     Opening::at is measured up the stroke: @tilt="n" rises to the head, @tilt="s" hangs the stem below it.
//     A square hand runs these along the side of the square (Hand::edgeStems), the others through the mark's
//     centre, and it hangs every joined note on its hairline, stroke attribute or not;
//   - two consecutive ncs both carrying @ligated="true" are one oblique: a broad-nib diagonal whose two ends'
//     vertical centres are the two pitches (the square porrectus and oblique clivis, square-02), each nc owning
//     its half;
//   - a liquescent on a connected nc has no mark of its own (the plica, square-01): a hairline tail - two, on
//     either side of the square, in a square hand - runs from the previous mark through its pitch and on past
//     it by the hand's liqOvershootUp / Down;
//   - a quilisma, a stropha comma or an @s-shape keep their stroke-builder shapes, centred on the pitch (the
//     comma by its head).
//
// Marks step to the right along the neume as a scribe writes them: a note joined to the previous one stands
// straight above or below it (an Aquitanian clivis is two stacked dots; a square pes two stacked squares with
// the connector on their right), or beside it when the two would touch, while a detached note (@con="g")
// starts a Gesture::breakGap clear of the previous mark, or goes where its @place sends it (@place="s":
// straight below, the Aquitanian clivis written as two detached dots). A square hand sets a joined lower note
// beside the previous square, abutting it, as in the square clivis, and an oblique rising from a square too.
//
// The ink is the true broad-nib footprint: the nib, a line of the pen's width held at its angle (plus the
// hairline thickness across it), swept along each straight stroke - a convex hexagon whose ends lie along the
// nib. With the square hand's nib held upright (90 degrees) a horizontal sweep is a square, a vertical
// connector a hairline, and an oblique a parallelogram with upright ends, as written.

namespace {

    using PointF = CalligraphicNeume::PointF;
    using Hand = CalligraphicNeume::Hand;

    constexpr double STEP = CalligraphicNeume::s_unitPx; // one staff step, line to the next space
    constexpr double EPS = 1e-6;
    // A level @tilt's dash, in steps (scaled by @rellen), unless the hand sets Hand::markWidth, and an oblique's
    // length, in mark widths (square-02: 18-19 px against squares 10 px wide).
    constexpr double DASH_STEPS = 1.0;
    constexpr double OBLIQUE_WIDTHS = 1.9;
    constexpr double OBLIQUE_PER_STEP = 0.75; // ...and at least this much per pen px of its drop
    // Clearance of an episema accent from the mark it marks, and between stacked accents, in pen px.
    constexpr double EPISEMA_GAP = 3.0;

    // Andrew's monotone chain: the convex hull of a small point set, in order around it.
    std::vector<PointF> ConvexHull(std::vector<PointF> p)
    {
        std::sort(p.begin(), p.end(),
            [](const PointF &a, const PointF &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
        if (p.size() < 3) return p;
        auto cross = [](const PointF &o, const PointF &a, const PointF &b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<PointF> h(2 * p.size());
        size_t k = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
            h[k++] = p[i];
        }
        for (size_t i = p.size() - 1, t = k + 1; i > 0; --i) {
            while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0) --k;
            h[k++] = p[i - 1];
        }
        h.resize(k - 1);
        return h;
    }

    // A polygon as a closed outline in the flat Bézier format [P0, C0a, C0b, P1, ...] the view draws, every
    // edge a straight cubic (control points at its thirds).
    std::vector<PointF> StraightOutline(const std::vector<PointF> &poly)
    {
        std::vector<PointF> out;
        const size_t n = poly.size();
        if (n < 2) return out;
        out.reserve(1 + 3 * n);
        out.push_back(poly[0]);
        for (size_t i = 0; i < n; ++i) {
            const PointF a = poly[i], b = poly[(i + 1) % n];
            out.push_back(a + (b - a) * (1.0 / 3.0));
            out.push_back(a + (b - a) * (2.0 / 3.0));
            out.push_back(b);
        }
        return out;
    }

    void Translate(std::vector<PointF> &pts, PointF d)
    {
        for (PointF &p : pts) p = p + d;
    }

    // The extent of an outline (control points included, which bounds the curve) relative to @p c.
    struct Extent {
        double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
    };
    Extent ExtentAround(const std::vector<PointF> &outline, PointF c)
    {
        Extent e;
        bool any = false;
        for (const PointF &p : outline) {
            const PointF d = p - c;
            if (!any) {
                e = { d.x, d.x, d.y, d.y };
                any = true;
                continue;
            }
            e.left = std::min(e.left, d.x);
            e.right = std::max(e.right, d.x);
            e.top = std::min(e.top, d.y);
            e.bottom = std::max(e.bottom, d.y);
        }
        return e;
    }

} // namespace

CalligraphicNeume::NeumeGeometry CalligraphicNeume::BuildPoint(
    const std::vector<NcInfo> &ncs, Slant slant, const Pen &pen, const Gesture &gesture, const Hand &hand)
{
    NeumeGeometry geo;
    const size_t n = ncs.size();
    geo.ncs.resize(n);
    if (n == 0) return geo;

    const PointF nib = { std::cos(pen.angleDeg * M_PI / 180.0), std::sin(pen.angleDeg * M_PI / 180.0) };
    const PointF across = nib.Perp();
    const bool edge = hand.edgeStems;
    // No forward slant: each mark sits at its pitch and stacked marks stand plumb (an Aquitanian clivis's two
    // dots share their x, aquitanian-01, -02), so a point script's lean is only in the direction of its strokes
    // (@tilt), never a shear of the positions.
    (void)slant;
    const double rellenRatio = (gesture.rellenRatio > 0.0) ? gesture.rellenRatio : 1.5;
    auto rellen = [&](const NcInfo &nc) -> double {
        return nc.longStroke ? rellenRatio : (nc.shortStroke ? 1.0 / rellenRatio : 1.0);
    };
    const double gap = gesture.breakGap;

    // The nib's footprint swept along the straight stroke a -> b: the convex hull
    // of the nib - its width along the nib, its hairline thickness across - set down at both ends.
    auto sweep = [&](PointF a, PointF b) -> std::vector<PointF> {
        const PointF w = nib * (pen.width / 2.0), t = across * (pen.thin / 2.0);
        std::vector<PointF> corners;
        for (const PointF &c : { a, b }) {
            corners.push_back(c + w + t);
            corners.push_back(c + w - t);
            corners.push_back(c - w + t);
            corners.push_back(c - w - t);
        }
        return StraightOutline(ConvexHull(corners));
    };
    // How far the nib's footprint reaches either side of its centre horizontally, so a stroke can be set flush
    // with a mark's side.
    const double nibHalfX = (std::fabs(nib.x) * pen.width + std::fabs(across.x) * pen.thin) / 2.0;
    // A stem, connector or tail whose ink runs exactly from a to b: the nib's footprint reaches past the ends of
    // the path it is swept along (by half the nib's breadth where the stroke runs along the nib - the square
    // hand's hairlines), so the path is drawn in by that much at each end. Nothing when the stroke is shorter
    // than the footprint itself.
    auto stroke = [&](PointF a, PointF b) -> std::vector<PointF> {
        const double len = (b - a).Len();
        const PointF u = (b - a).Unit();
        const double reach = (std::fabs(nib.Dot(u)) * pen.width + std::fabs(across.Dot(u)) * pen.thin) / 2.0;
        if (len <= 2.0 * reach + EPS) return {};
        return sweep(a + u * reach, b - u * reach);
    };
    // How far along the unit direction @p u from the centre of a mark of extent @p e the ray leaves it.
    auto exitDist = [&](const PointF &u, double left, double right, double top, double bottom) -> double {
        double t = std::numeric_limits<double>::max();
        if (u.x > EPS) t = std::min(t, right / u.x);
        if (u.x < -EPS) t = std::min(t, left / u.x);
        if (u.y > EPS) t = std::min(t, bottom / u.y);
        if (u.y < -EPS) t = std::min(t, top / u.y);
        return (t == std::numeric_limits<double>::max()) ? 0.0 : std::max(0.0, t);
    };

    auto record = [&](size_t i, std::vector<PointF> pts, bool episema = false) {
        geo.strokes.push_back({ (int)i, episema, std::move(pts) });
    };

    // Each nc's mark, built around the origin: its outline, its ductus and its extent. A connected liquescent
    // has none (the plica's note is its tail); an oblique's two ncs share theirs, built below.
    struct Mark {
        std::vector<PointF> outline;
        std::vector<PointF> ductus;
        Extent ext;
    };
    auto special = [&](const NcInfo &nc) { return nc.quilisma || nc.strophicus || !nc.sShape.empty(); };
    auto levelTilt
        = [&](const NcInfo &nc) { return nc.tilt != COMPASSDIRECTION_NONE && std::fabs(TiltVec(nc.tilt).y) < EPS; };
    // A plain nc carrying a stroke that arrives at its pitch: a rising or falling @tilt, or a @curve / @angled.
    // Only an explicit @tilt counts: on a staff @intm is read off the pitches and says nothing about shape.
    auto hasStroke = [&](const NcInfo &nc) {
        if (special(nc)) return false;
        if (nc.tilt != COMPASSDIRECTION_NONE) return !levelTilt(nc);
        return nc.curve != curvatureDirection_CURVE_NONE || nc.angled;
    };
    auto dash = [&](double len) -> Mark {
        const PointF a = { -len / 2.0, 0.0 }, b = { len / 2.0, 0.0 };
        Mark m;
        m.outline = sweep(a, b);
        m.ductus = { a, b };
        m.ext = ExtentAround(m.outline, { 0.0, 0.0 });
        return m;
    };
    auto markFor = [&](const NcInfo &nc) -> Mark {
        Mark m;
        const PointF o = { 0.0, 0.0 };
        if (nc.quilisma) {
            const int tilt = (nc.tilt != COMPASSDIRECTION_NONE) ? nc.tilt : (int)COMPASSDIRECTION_e;
            const int curveHand = (nc.curve != curvatureDirection_CURVE_NONE) ? nc.curve : curvatureDirection_CURVE_c;
            const bool hasCurve = (nc.curve != curvatureDirection_CURVE_NONE) || nc.angled;
            const PointF d = TiltVec(tilt);
            m.ductus = Quilisma(o, tilt, nc.waves, true, curveHand, hasCurve, nc.angled, d, d).pts;
        }
        else if (nc.strophicus) {
            m.ductus = Comma(0.0, 0.0, nc.tilt).pts;
            Translate(m.ductus, PointF{ 0.0, 0.0 } - m.ductus.front()); // a stropha's pitch is its head
        }
        else if (!nc.sShape.empty()) {
            m.ductus = Wave(0.0, 0.0, nc.sShape, nc.tilt).pts;
        }
        else if (levelTilt(nc)) {
            return dash(DASH_STEPS * STEP * rellen(nc));
        }
        else {
            switch (hand.dot) {
                case Hand::Dot::Square: {
                    // A square: the upright nib drawn level, so the footprint is as tall as the nib is broad and as
                    // wide as Hand::markWidth (as wide as it is tall by default).
                    const double wide = (hand.markWidth > 0.0) ? hand.markWidth : pen.width;
                    return dash(std::max(0.0, wide - pen.thin));
                }
                case Hand::Dot::Dash:
                    return dash(((hand.markWidth > 0.0) ? hand.markWidth : DASH_STEPS * STEP) * rellen(nc));
                case Hand::Dot::Point: {
                    // A small round point, a touch longer along the nib than across it, Hand::markWidth across (the
                    // nib's breadth by default).
                    const double r = ((hand.markWidth > 0.0) ? hand.markWidth : pen.width) / 2.0;
                    std::vector<PointF> ring;
                    for (int k = 0; k < 8; ++k) {
                        const double a = k * M_PI / 4.0;
                        ring.push_back(nib * (1.08 * r * std::cos(a)) + across * (0.92 * r * std::sin(a)));
                    }
                    m.outline = SmoothClosed(ring);
                    m.ductus = { o };
                    m.ext = ExtentAround(m.outline, o);
                    return m;
                }
                case Hand::Dot::Lozenge: {
                    const double w = ((hand.markWidth > 0.0) ? hand.markWidth : pen.width) / 2.0, h = 1.5 * w;
                    m.outline = StraightOutline({ { 0.0, -h }, { w, 0.0 }, { 0.0, h }, { -w, 0.0 } });
                    m.ductus = { { 0.0, -h }, { 0.0, h } };
                    m.ext = ExtentAround(m.outline, o);
                    return m;
                }
                case Hand::Dot::Dab:
                default:
                    m.outline = Punctum(o);
                    m.ductus = { o };
                    m.ext = ExtentAround(m.outline, o);
                    return m;
            }
        }
        m.outline = NibRibbon(m.ductus);
        m.ext = ExtentAround(m.outline, o);
        return m;
    };

    // The last mark set down, in absolute pen space: where the next nc is placed from and where a
    // connector or plica tail leaves.
    struct Placed {
        double cx = 0.0, cy = 0.0, left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
        bool any = false;
    };
    Placed prev;
    std::vector<Placed> placed; // every mark of the neume so far, so a new one never lands on an earlier one
    auto setPrev = [&](double cx, double cy, const Extent &e) {
        prev = { cx, cy, cx + e.left, cx + e.right, cy + e.top, cy + e.bottom, true };
        placed.push_back(prev);
    };

    // The direction of the stroke arriving at nc @p i's pitch from the previous mark, @p dy away (+y down):
    // its @tilt turned to the interval's sense (PitchedTilt), vertical without one; plumb in an upright hand.
    auto arrivalDir = [&](const NcInfo &nc, double dy) -> PointF {
        PointF d = (nc.tilt != COMPASSDIRECTION_NONE) ? TiltVec(PitchedTilt(nc.tilt, dy))
                                                      : PointF{ 0.0, (dy < 0.0) ? -1.0 : 1.0 };
        if (hand.upright && std::fabs(dy) > EPS) d = { 0.0, (dy < 0.0) ? -1.0 : 1.0 };
        return d;
    };

    // Where nc @p i's mark (extent @p e) is centred horizontally, at pitch @p y. @p obliqueStart: the mark is
    // the left end of an oblique.
    auto placeX = [&](size_t i, double y, const Extent &e, bool obliqueStart) -> double {
        const NcInfo &nc = ncs[i];
        if (!prev.any) return 0.0;
        const double dy = y - prev.cy;
        double cx = prev.cx;
        if (i > 0 && nc.gapped) {
            // A detached note: a pen-lift to the right, or the way an explicit @place sends it - straight above or
            // below (an Aquitanian clivis's second dot, @place="s"), or back to the left.
            const PointF pd = TiltVec(nc.place);
            if (nc.place == COMPASSDIRECTION_NONE || pd.x > EPS) return prev.right + gap - e.left;
            if (pd.x < -EPS) return prev.left - gap - e.right;
        }
        else if (edge) {
            // Square: a higher note stands on the previous square, flush with its right side (the connector
            // runs there); a lower or equal one is set beside it, abutting, and so is an oblique rising from it
            // (square-02: the oblique's left end starts just right of the F square below it).
            return (dy < -EPS && !obliqueStart) ? prev.right - e.right : prev.right - e.left;
        }
        else if (hasStroke(nc) && std::fabs(dy) > EPS) {
            const PointF d = arrivalDir(nc, dy);
            if (std::fabs(d.y) > EPS) cx += d.x * std::fabs(dy) / std::fabs(d.y);
        }
        // Stacked straight above or below the previous mark unless it would touch that one or an earlier mark of
        // the neume (the third note of an F G F returning onto the first): then beside it.
        for (size_t guard = 0; guard <= placed.size(); ++guard) {
            const Placed *hit = NULL;
            for (const Placed &b : placed) {
                const bool overlapY = (y + e.top < b.bottom + 1.0) && (y + e.bottom > b.top - 1.0);
                const bool overlapX = (cx + e.left < b.right + 1.0) && (cx + e.right > b.left - 1.0);
                if (overlapY && overlapX) hit = &b;
            }
            if (!hit) break;
            cx = hit->right + gap - e.left;
        }
        return cx;
    };

    // The opening stroke of a first or detached nc with a stroke, ending (Opening::at) at its pitch @p p. Its
    // role follows the next connected note: a lead when that one is lower (a clivis's first note, whose stem
    // a square hand runs down its left side), a foot when it is higher, alone otherwise.
    auto opening = [&](size_t i, PointF c, const Extent &e) -> std::vector<PointF> {
        const NcInfo &nc = ncs[i];
        const Opening *op = &hand.solo;
        bool leftSide = false;
        if (i + 1 < n && !ncs[i + 1].gapped) {
            const double ny = ncs[i + 1].pitchY;
            if (ny > c.y + EPS) {
                op = &hand.lead;
                leftSide = true;
            }
            else if (ny < c.y - EPS) {
                op = &hand.foot;
            }
        }
        // A short stroke (@rellen="s") has its own reach and pitch point where the hand gives one.
        double scale = rellen(nc);
        if (nc.shortStroke && hand.shortOpening.reach > 0.0) {
            op = &hand.shortOpening;
            scale = 1.0;
        }
        PointF d = (nc.tilt != COMPASSDIRECTION_NONE) ? TiltVec(nc.tilt) : PointF{ 0.0, -1.0 };
        if (hand.upright) d = { 0.0, (d.y > 0.0) ? 1.0 : -1.0 };
        const double len = op->reach * STEP * scale / std::max(std::fabs(d.y), 0.2);
        // The mark is the stroke's head whichever way the pen ran - rising to it (Nonantolan, @tilt="n") or
        // setting it down and pulling the stem away (a square virga, @tilt="s") - so Opening::at is measured up
        // the stroke, from its lower end to its upper end: a falling stroke counts it from its far end.
        const double at = (d.y > 0.0) ? 1.0 - op->at : op->at;
        PointF p = c;
        if (edge) p.x = leftSide ? c.x + e.left + nibHalfX : c.x + e.right - nibHalfX;
        return { p - d * (len * at), p + d * (len * (1.0 - at)) };
    };

    // Episemata of nc @p k: short hairline accents set just clear of its mark (centred at cx, cy within the box
    // left..right, top..bottom) - above it by default, below, or upright to its left / right by @place - stacked
    // outward when there are several.
    auto drawEpisemata = [&](size_t k, double cx, double cy, double left, double right, double top, double bottom) {
        double stackAbove = 0.0, stackBelow = 0.0, stackLeft = 0.0, stackRight = 0.0;
        const double epiScale = pen.episema * 6.0 / std::max(pen.width, 6.0);
        for (const EpisemaInfo &ep : ncs[k].episemata) {
            const double half = std::max(4.0, (right - left) / 2.0 + 1.0);
            PointF a, b;
            if (ep.place == EVENTREL_left || ep.place == EVENTREL_right) {
                const bool l = (ep.place == EVENTREL_left);
                double &stack = l ? stackLeft : stackRight;
                const double x = l ? left - EPISEMA_GAP - stack : right + EPISEMA_GAP + stack;
                stack += EPISEMA_GAP;
                a = { x, cy - half };
                b = { x, cy + half };
            }
            else {
                const bool below = (ep.place == EVENTREL_below || ep.place == EVENTREL_below_left
                    || ep.place == EVENTREL_below_right);
                double &stack = below ? stackBelow : stackAbove;
                const double yy = below ? bottom + EPISEMA_GAP + stack : top - EPISEMA_GAP - stack;
                stack += EPISEMA_GAP;
                a = { cx - half, yy };
                b = { cx + half, yy };
            }
            std::vector<PointF> centre;
            for (int j = 0; j <= 4; ++j) centre.push_back(a + (b - a) * (j / 4.0));
            geo.ncs[k].episemata.push_back(NibRibbon(centre, epiScale));
            geo.strokes.push_back({ (int)k, true, centre });
        }
    };

    for (size_t i = 0; i < n; ++i) {
        const NcInfo &nc = ncs[i];
        NcGeometry &out = geo.ncs[i];
        const double y = nc.pitchY;
        const bool connected = (i > 0) && !nc.gapped && prev.any;

        // A plica / liquescent joined to the previous note: no mark of its own, only the hairline running from
        // that note through this pitch and on past it.
        if (nc.liquescent && connected && !special(nc)) {
            const double dy = y - prev.cy;
            const double sgn = (dy < -EPS) ? -1.0 : 1.0;
            const double over = std::max(0.0, (sgn < 0.0) ? hand.liqOvershootUp : hand.liqOvershootDown) * STEP;
            std::vector<std::vector<PointF>> tails;
            PointF tip;
            if (edge) {
                // The square plica: a tail down (or up) each side of the square, from its edge to the pitch and
                // beyond.
                const double endY = y + sgn * over;
                const double startY = (sgn < 0.0) ? prev.top : prev.bottom;
                const double xs[2] = { prev.right - nibHalfX, prev.left + nibHalfX };
                for (double x : xs) tails.push_back({ { x, startY }, { x, endY } });
                tip = { xs[0], y };
            }
            else {
                const PointF d = arrivalDir(nc, dy);
                const double toPitch = (std::fabs(dy) > EPS) ? std::fabs(dy) : STEP; // a unison falls a step
                const double len = (toPitch + over) / std::max(std::fabs(d.y), 0.2);
                const PointF a = { prev.cx, prev.cy };
                tails.push_back({ a, a + d * len });
                tip = a + d * (toPitch / std::max(std::fabs(d.y), 0.2));
            }
            for (size_t k = 0; k < tails.size(); ++k) {
                // The first tail, the one that carries the pitch, is the note's ink; a second is a piece of it. A
                // tail too short to draw in from both ends (a plica on a unison) is swept as it stands, so the
                // note never goes without ink.
                std::vector<PointF> ink = stroke(tails[k][0], tails[k][1]);
                if (ink.empty()) ink = sweep(tails[k][0], tails[k][1]);
                if (k == 0)
                    out.ribbon = std::move(ink);
                else
                    out.pieces.push_back(std::move(ink));
                record(i, tails[k]);
            }
            setPrev(tip.x, y, Extent());
            continue;
        }

        // An oblique: this nc and the next both @ligated, the next joined to it.
        const bool oblique = nc.ligated && (i + 1 < n) && ncs[i + 1].ligated && !ncs[i + 1].gapped && !special(nc)
            && !special(ncs[i + 1]) && !ncs[i + 1].liquescent;

        Mark m = markFor(nc);
        const double cx = placeX(i, y, m.ext, oblique);
        const PointF c = { cx, y };
        PointF head = c; // where the mark goes: its pitch, or the head of a stroke centred on the pitch

        // The stroke arriving at this pitch: from the previous mark when joined to it, else an opening stroke. A
        // square hand hangs every joined note on its hairline, stroke attribute or not: that is how its notes
        // join (a pes of two plain ncs is two squares and the connector); @con="g" sets a square apart.
        const bool joins = connected && (hasStroke(nc) || (edge && !special(nc)));
        if (joins || (!connected && hasStroke(nc))) {
            std::vector<PointF> path;
            if (joins) {
                const double dy = y - prev.cy;
                if (edge) {
                    // The square connector: a hairline along the shared side, sized by the interval, running in
                    // the gap between the two squares (none where they touch, as on a second).
                    const double x = prev.right - nibHalfX;
                    const double y0 = (dy < 0.0) ? prev.top : prev.bottom;
                    const double y1 = (dy < 0.0) ? y + m.ext.bottom : y + m.ext.top;
                    if ((dy < -EPS && y1 < y0) || (dy > EPS && y1 > y0)) path = { { x, y0 }, { x, y1 } };
                }
                else {
                    // From mark to mark, its ink between them.
                    const PointF a = { prev.cx, prev.cy };
                    const PointF u = (c - a).Unit();
                    const double t0 = exitDist(
                        u, prev.left - prev.cx, prev.right - prev.cx, prev.top - prev.cy, prev.bottom - prev.cy);
                    const double t1 = exitDist(u * -1.0, m.ext.left, m.ext.right, m.ext.top, m.ext.bottom);
                    if ((c - a).Len() > t0 + t1 + EPS) path = { a + u * t0, c - u * t1 };
                }
            }
            else {
                path = opening(i, c, m.ext);
                // The mark is the stroke's head: at the pitch unless the hand centres the stroke on it (a
                // Nonantolan short virga), then at the stroke's upper end.
                if (!edge && path.size() == 2) head = (path[0].y < path[1].y) ? path[0] : path[1];
                // A stem running through the space below (or above) the mark keeps later marks of the neume off
                // it: a lower note joined to a Nonantolan or Aquitanian virga steps beside the stem, not onto it.
                if (!edge && path.size() == 2) {
                    placed.push_back({ c.x, c.y, std::min(path[0].x, path[1].x) - nibHalfX,
                        std::max(path[0].x, path[1].x) + nibHalfX, std::min(path[0].y, path[1].y),
                        std::max(path[0].y, path[1].y), true });
                }
            }
            if (path.size() == 2) {
                std::vector<PointF> ink = stroke(path[0], path[1]);
                if (!ink.empty()) out.pieces.push_back(std::move(ink));
                record(i, path);
            }
        }

        if (oblique) {
            // One broad-nib diagonal from this pitch to the next, its ends' vertical centres on the two pitches;
            // each nc owns its half. It starts where this note's mark would.
            const NcInfo &nx = ncs[i + 1];
            const double left = cx + m.ext.left;
            const double width = m.ext.right - m.ext.left;
            // Longer for a wider interval, so a steep oblique keeps some body across its slope.
            const double reach = std::max(OBLIQUE_WIDTHS * width, OBLIQUE_PER_STEP * std::fabs(nx.pitchY - y));
            const PointF a = { left + nibHalfX, y }, b = { left + reach - nibHalfX, nx.pitchY };
            const PointF mid = (a + b) * 0.5;
            out.ribbon = sweep(a, mid);
            geo.ncs[i + 1].ribbon = sweep(mid, b);
            record(i, { a, b });
            // The next nc carries no mark or stroke of its own: the oblique is its connecting stroke.
            const double right = left + reach;
            Extent e = m.ext;
            placed.push_back({ (left + right) / 2.0, (y + nx.pitchY) / 2.0, left, right, std::min(y, nx.pitchY) + e.top,
                std::max(y, nx.pitchY) + e.bottom, true });
            prev = { right - width / 2.0, nx.pitchY, right - width, right, nx.pitchY + e.top, nx.pitchY + e.bottom,
                true };
            // Each nc's episemata sit over (or under) its own end of the oblique.
            drawEpisemata(i, left + width / 2.0, y, left, left + width, y + e.top, y + e.bottom);
            drawEpisemata(i + 1, prev.cx, nx.pitchY, prev.left, prev.right, prev.top, prev.bottom);
            ++i;
            continue;
        }

        Translate(m.outline, head);
        Translate(m.ductus, head);
        out.ribbon = std::move(m.outline);
        record(i, m.ductus);
        setPrev(cx, y, m.ext);

        // A liquescent first or detached note: its mark and a hairline running on a step past it, the way of
        // its @tilt (down without one).
        if (nc.liquescent && !special(nc)) {
            PointF d = (nc.tilt != COMPASSDIRECTION_NONE) ? TiltVec(nc.tilt) : PointF{ 0.0, 1.0 };
            if (hand.upright || std::fabs(d.y) < EPS) d = { 0.0, (d.y < 0.0) ? -1.0 : 1.0 };
            const double over = std::max(0.0, (d.y < 0.0) ? hand.liqOvershootUp : hand.liqOvershootDown) * STEP;
            PointF a = c;
            if (edge) a.x = prev.right - nibHalfX;
            const std::vector<PointF> path = { a, a + d * ((STEP + over) / std::fabs(d.y)) };
            std::vector<PointF> ink = stroke(path[0], path[1]);
            if (!ink.empty()) out.pieces.push_back(std::move(ink));
            record(i, path);
        }

        drawEpisemata(i, cx, y, prev.left, prev.right, prev.top, prev.bottom);
    }
    return geo;
}

} // namespace vrv
