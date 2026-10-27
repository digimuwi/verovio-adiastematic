/////////////////////////////////////////////////////////////////////////////
// Name:        calligraphicfurniture.cpp
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "calligraphicneume.h"

//----------------------------------------------------------------------------

#include <algorithm>
#include <cmath>

//----------------------------------------------------------------------------

#include "atttypes.h"

namespace vrv {

//----------------------------------------------------------------------------
// Staff furniture in a hand's own forms: the custos and the letter clefs
//----------------------------------------------------------------------------

// Each family drew its custos in its own hand, with the same pen as its neumes, so the custos is swept with
// the hand's nib like a neume's ribbon. The shapes below are measured on the facsimile set of 2026-10-27
// (README section Q5); one staff step (line to the next space) is s_unitPx = 13 pen px, and pen space has
// +y down, so a mark rising to the right runs toward -y. Every shape has its pitch at the origin.

namespace {

    using PointF = CalligraphicNeume::PointF;

    constexpr double STEP = CalligraphicNeume::s_unitPx;
    constexpr double DEG = M_PI / 180.0;

    // A straight centreline from @p a to @p b, sampled densely enough that the outline keeps square ends.
    std::vector<PointF> Segment(PointF a, PointF b, int n = 12)
    {
        std::vector<PointF> pts;
        pts.reserve(n + 1);
        for (int i = 0; i <= n; ++i) pts.push_back(a + (b - a) * ((double)i / n));
        return pts;
    }

    // Append @p more to @p pts, dropping its first point when it repeats the last one (a joint).
    void Append(std::vector<PointF> &pts, const std::vector<PointF> &more)
    {
        for (size_t i = 0; i < more.size(); ++i) {
            if (i == 0 && !pts.empty() && (pts.back() - more[0]).Len() < 1e-6) continue;
            pts.push_back(more[i]);
        }
    }

    // The hand's pen reduced to its hairline: a stroke of the thin width whichever way it travels (the tail
    // of a square-notation custos, drawn with the corner of the nib).
    CalligraphicNeume::Pen Hairline(const CalligraphicNeume::Pen &pen)
    {
        CalligraphicNeume::Pen hair = pen;
        hair.width = pen.thin;
        return hair;
    }

} // namespace

std::vector<CalligraphicNeume::PointF> CalligraphicNeume::NibOutline(const std::vector<PointF> &centre, const Pen &pen)
{
    // The width law of NibEdges (w proportional to |T x n̂|), with the pen given rather than the one a Build
    // call has set: furniture is drawn outside Build, in the hand of the staff it stands on.
    const int n = (int)centre.size();
    if (n < 2) return {};
    const PointF nib = { std::cos(pen.angleDeg * DEG), std::sin(pen.angleDeg * DEG) };
    std::vector<PointF> left(n), right(n);
    for (int i = 0; i < n; ++i) {
        const PointF t = (centre[std::min(n - 1, i + 1)] - centre[std::max(0, i - 1)]).Unit();
        const double sinA = std::fabs(t.x * nib.y - t.y * nib.x);
        const double w = (pen.thin + (pen.width - pen.thin) * sinA) / 2.0;
        left[i] = { centre[i].x - t.y * w, centre[i].y + t.x * w };
        right[i] = { centre[i].x + t.y * w, centre[i].y - t.x * w };
    }
    SmoothPolyline(left, 2);
    SmoothPolyline(right, 2);
    std::vector<PointF> loop = left;
    loop.insert(loop.end(), right.rbegin(), right.rend());
    return SmoothClosed(loop);
}

std::vector<std::vector<CalligraphicNeume::PointF>> CalligraphicNeume::CustosOutlines(
    Hand::CustosForm form, const Pen &pen)
{
    std::vector<std::vector<PointF>> outlines;
    switch (form) {
        case Hand::CustosForm::Square: {
            // The Humbert codex (square-03): a small square placed exactly like a note square on its line,
            // its head the pitch, and a hairline tail rising to the upper right from its right edge. The
            // square is one short horizontal stroke of a nib held upright, a little under a step each way;
            // the tail climbs about 1.7 steps.
            const double side = 0.85 * STEP;
            Pen square = pen;
            square.angleDeg = 90.0;
            square.width = side;
            square.thin = side;
            outlines.push_back(NibOutline(Segment({ 0.0, 0.0 }, { side, 0.0 }), square));
            const double tailX = side - pen.thin / 2.0;
            outlines.push_back(
                NibOutline(Segment({ tailX, -0.2 * side }, { tailX + 0.35 * STEP, -1.7 * STEP }), Hairline(pen)));
            break;
        }
        case Hand::CustosForm::HeadTail: {
            // Rein, OeNB Cod. 1799** (cistercian-03): a thick head on the line - a short stroke down across
            // the nib, so it comes out broad - whose pen then swings up to the right in a hairline about a
            // step high and ends in a dot. The pitch is the head.
            std::vector<PointF> centre = Segment({ -0.3 * STEP, -0.25 * STEP }, { 0.3 * STEP, 0.25 * STEP }, 6);
            Append(centre,
                Cubic({ 0.3 * STEP, 0.25 * STEP }, { 0.75 * STEP, 0.5 * STEP }, { 1.0 * STEP, -0.3 * STEP },
                    { 1.75 * STEP, -0.9 * STEP }, 16));
            outlines.push_back(NibOutline(centre, pen));
            // The dot where the tail ends: a round blob about a nib wide.
            const PointF dot = { 1.85 * STEP, -1.0 * STEP };
            const double r = pen.width * 0.55;
            std::vector<PointF> round;
            for (int i = 0; i < 8; ++i) {
                round.push_back({ dot.x + r * std::cos(i * M_PI / 4.0), dot.y + r * std::sin(i * M_PI / 4.0) });
            }
            outlines.push_back(SmoothClosed(round));
            break;
        }
        case Hand::CustosForm::Slant: {
            // Saint-Yrieix, Paris lat. 903 (aquitanian-02): a slanted stroke from about 1.2 steps below the
            // pitch to 1.4 above it, leaning right, its pitch at a small hook part way up rather than at
            // either end. The hook is a short spur thrown off to the right at the pitch, across the nib,
            // so it reads as a thickening of the stroke.
            const PointF lo = { 0.0, 1.2 * STEP };
            const PointF hi = { 0.6 * STEP, -1.4 * STEP };
            outlines.push_back(NibOutline(Segment(lo, hi, 16), pen));
            const double t = 1.2 / 2.6; // where the stroke passes the pitch
            const PointF at = lo + (hi - lo) * t;
            outlines.push_back(NibOutline(
                Cubic(at, { at.x + 0.3 * STEP, at.y - 0.2 * STEP }, { at.x + 0.55 * STEP, at.y - 0.05 * STEP },
                    { at.x + 0.5 * STEP, at.y + 0.25 * STEP }, 10),
                pen));
            break;
        }
        case Hand::CustosForm::Check: {
            // Barb. lat. 603 (beneventan-01, low confidence): a check mark, its pitch at the lower blob, from
            // which a hairline rises to the upper right (the Solesmes editors' "point with a fine oblique
            // stroke", PM XV pp. 107-111). The blob is a short stroke down across the nib.
            std::vector<PointF> centre = Segment({ -0.25 * STEP, -0.25 * STEP }, { 0.2 * STEP, 0.2 * STEP }, 6);
            Append(centre,
                Cubic({ 0.2 * STEP, 0.2 * STEP }, { 0.45 * STEP, 0.3 * STEP }, { 0.75 * STEP, -0.6 * STEP },
                    { 1.1 * STEP, -1.8 * STEP }, 16));
            outlines.push_back(NibOutline(centre, pen));
            break;
        }
        case Hand::CustosForm::Glyph:
        default: break;
    }
    return outlines;
}

char32_t CalligraphicNeume::ClefLetter(const Hand &hand, int clefShape)
{
    // The clef letters of the stroke-neume books on lines: c and F are common, g appears stacked with them in
    // Stams 40 (hufnagel-06). The Hildegard books (Dendermonde, Riesencodex) write a small f where the others
    // write a capital F (hildegard-04, -07, -11).
    switch (clefShape) {
        case CLEFSHAPE_C: return U'c';
        case CLEFSHAPE_F: return (hand.token == "hildegard") ? U'f' : U'F';
        case CLEFSHAPE_G: return U'g';
        default: return 0;
    }
}

} // namespace vrv
