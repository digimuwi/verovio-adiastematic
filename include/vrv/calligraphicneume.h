/////////////////////////////////////////////////////////////////////////////
// Name:        calligraphicneume.h
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#ifndef __VRV_CALLIGRAPHICNEUME_H__
#define __VRV_CALLIGRAPHICNEUME_H__

#include <cmath>
#include <string>
#include <vector>

namespace vrv {

//----------------------------------------------------------------------------
// CalligraphicNeume
//----------------------------------------------------------------------------

/**
 * Pure geometry library for the adiastematic (staffless) neume rendering. It turns the visual
 * attributes of a sequence of <nc> components into a single broad-nib pen gesture: one closed,
 * filled "nib ribbon" outline plus the hairline accents of any <episema>.
 *
 * This is a faithful port of the JavaScript prototype renderer. To keep the math identical and
 * auditable, everything here works in the prototype's "pen space": a right-handed-on-screen frame
 * where +x is right and +y is DOWN (SVG convention), centred on the gesture origin (0, 0), and
 * measured in prototype pixels scaled by the caller-supplied @p scale (verovio drawing units per
 * prototype pixel). The caller (View::DrawNeumeAdiastematic) is solely responsible for the y-flip
 * into verovio's bottom-up logical space and for anchoring the gesture at the neume position.
 *
 * The renderer is calligraphic: stroke width follows the broad-nib law w proportional to |T x n̂|
 * (the sine of the angle between the travel direction T and a fixed nib direction n̂), so strokes
 * drawn across the nib are broad and strokes along it taper to a hairline.
 */
class CalligraphicNeume {
public:
    /** Prototype pixels that correspond to one verovio drawing unit (half a staff space). */
    static constexpr double s_unitPx = 13.0;

    /** A point (or vector) in floating-point pen space, with the minimal arithmetic the geometry needs. */
    struct PointF {
        double x = 0.0;
        double y = 0.0;
        PointF operator+(PointF o) const { return { x + o.x, y + o.y }; }
        PointF operator-(PointF o) const { return { x - o.x, y - o.y }; }
        PointF operator*(double k) const { return { x * k, y * k }; }
        double Dot(PointF o) const { return x * o.x + y * o.y; }
        double Len() const { return std::hypot(x, y); }
        PointF Perp() const { return { -y, x }; } ///< quarter-turn anticlockwise (pen space, +y down)
        /** Unit vector along this one, or @p fallback when (near) zero length. */
        PointF Unit(PointF fallback) const
        {
            const double m = std::hypot(x, y);
            return (m < 1e-9) ? fallback : PointF{ x / m, y / m };
        }
        /** Unit vector along this one, or the zero vector when (near) zero length. */
        PointF Unit() const { return Unit(PointF{ 0.0, 0.0 }); }
    };

    /** The visual rendering attributes of one <episema> child, pre-extracted from the MEI model. */
    struct EpisemaInfo {
        int form = 0; ///< episemaVis_FORM (h / v)
        int place = 0; ///< data_EVENTREL (above / below / left / right / compounds)
    };

    /** The visual rendering attributes of one <nc>, pre-extracted from the MEI model. */
    struct NcInfo {
        int tilt = 0; ///< data_COMPASSDIRECTION
        int curve = 0; ///< curvatureDirection_CURVE (a / c)
        std::string sShape; ///< @s-shape (w / n / s)
        bool longStroke = false; ///< @rellen == l
        bool shortStroke = false; ///< @rellen == s
        char intm = 0; ///< first char of @intm (u / d / s), 0 if absent
        bool gapped = false; ///< @con == g: detached from the previous component (a fresh gesture)
        bool oriscus = false; ///< has an <oriscus> child
        bool strophicus = false; ///< has a <strophicus> child
        bool quilisma = false; ///< has a <quilisma> child: drawn as a wavy ascending stroke
        int waves = 0; ///< @waves: number of crests of the quilisma's wavy line (0 = use default)
        bool liquescent = false; ///< has a <liquescent> child: drawn as a curl / loop (cephalicus, epiphonus)
        int liquescentCurve = 0; ///< <liquescent> @curve as curvatureDirection_CURVE (a / c), 0 (NONE) if absent
        bool liquescentLooped = false; ///< <liquescent> @looped: the curl closes into a loop
        bool hasNonEpisemaChild = false; ///< any child that is not an <episema> (ornament marker)
        std::vector<EpisemaInfo> episemata;
    };


    /**
     * The geometry attributable to a single <nc>. A ligature is inked as one continuous pen
     * gesture, but the ribbon is cut along cross-sections into per-nc slices so each <nc> owns its
     * portion of the ink. Adjacent slices share their boundary cross-section, so they tile the full
     * gesture without gaps or seams.
     */
    struct NcGeometry {
        /**
         * This nc's slice of the nib-ribbon outline as a flat list of cubic Bézier control points
         * [P0, C0a, C0b, P1, C1a, C1b, P2, ...] forming one implicitly closed loop. Empty if the
         * component contributes no ink.
         */
        std::vector<PointF> ribbon;
        /**
         * This nc's episema accents, each drawn with its own broad nib: a flat list of cubic Bézier
         * control points forming one implicitly closed, fillable loop (same format as @ref ribbon).
         * Positioned relative to - and butting against - the stroke they mark, so each reads as part
         * of that component's calligraphic shape rather than a detached line.
         */
        std::vector<std::vector<PointF>> episemata;
    };

    /** The complete geometry of one neume, one entry per <nc> in document order. */
    struct NeumeGeometry {
        std::vector<NcGeometry> ncs;
    };

    /**
     * A scribe's forward slant (italicisation): the whole gesture is pulled toward the upper-right,
     * as a right hand naturally drags the pen. The pull is anisotropic - a stroke travelling WITH it
     * (up-right) leans fully, while one travelling against it (a firm descent) leans less - so the
     * lean reads as a living habit rather than a uniform mechanical oblique. The shear is applied to
     * the centrelines BEFORE the nib is swept, so the fixed nib meets the leaned strokes at new
     * angles and the thick/thin redistributes exactly as a constant-angle pen does on a slant.
     */
    struct Slant {
        double tan; ///< tan of the slant angle: the full lean of an up-right stroke (0 = upright)
        double bias; ///< how much a descent resists the pull: 0 = uniform oblique, 1 = stays upright
        bool IsNone() const { return tan == 0.0; }
    };

    /**
     * Build the geometry of one neume from its ordered <nc> components. The returned
     * NeumeGeometry::ncs is parallel to @p ncs (same size and order), so each entry can be inked
     * inside its own <nc> graphic.
     * @p scale is verovio drawing units per prototype pixel (typically unit / s_unitPx).
     * @p slant applies the scribe's forward lean (default: none).
     */
    static NeumeGeometry Build(const std::vector<NcInfo> &ncs, double scale, Slant slant = {});

private:
    //----------------------------------------------------------------------------
    // Internal geometry helpers (faithful ports of the prototype, in raw pen pixels)
    //----------------------------------------------------------------------------

    struct Stroke {
        std::vector<PointF> pts;
        PointF exit;
    };

    /** One component's centreline contribution to a pen gesture, tagged with its <nc> index. */
    struct Seg {
        std::vector<PointF> pts;
        int tilt = 0;
        int ncIndex = 0;
        bool isDot = false; ///< a bare punctum: a single pen dab, not a swept stroke
    };

    /** The unit travel vector of a @tilt compass direction (pen space, +y down). */
    static PointF TiltVec(int tilt);
    /**
     * Resolve the effective tilt of an nc. An explicit @tilt always wins; a quilisma component then
     * defaults to e (it runs level - the ascent belongs to the next nc); otherwise the @intm default
     * applies (u->ne, d->se).
     */
    static int EffectiveTilt(const NcInfo &nc);

    static std::vector<PointF> Cubic(PointF p0, PointF p1, PointF p2, PointF p3, int n = 18);
    /**
     * Resample a centreline through a Catmull-Rom spline. When @p ownersIn (one segment index per
     * input point) and @p ownersOut are supplied, @p ownersOut is filled parallel to the result so
     * each densified point keeps the index of the component it came from.
     */
    static std::vector<PointF> Densify(const std::vector<PointF> &pts, double step = 5.0,
        const std::vector<int> *ownersIn = NULL, std::vector<int> *ownersOut = NULL);
    static std::vector<PointF> RotateAround(const std::vector<PointF> &pts, double cx, double cy, double ang);

    static Stroke ObliqueShape(double x, double y, PointF dir, double len);
    static Stroke Wave(double x, double y, const std::string &orient, int tilt);
    /** An S-shaped wave that begins at @p s (for a connected oriscus / quassus within a ligature,
     * e.g. the second component of a virga strata) rather than being centred on a point. */
    static Stroke WaveFrom(PointF s, const std::string &orient, int tilt);
    /**
     * A quilisma: a wavy horizontal flourish, modelled on the liquescent. The wiggle ALWAYS runs level
     * (east) - the broad nib swept over it draws the characteristic toothed quilisma - landing on the
     * first crest and lifting at the last trough; @p waves (from @waves) sets the crest count. When
     * @p rise is false the WHOLE note is this wavy line (the parent nc carries no @tilt). When @p rise
     * is true the wiggle is only a PREAMBLE: the note then sweeps up out of the last trough toward
     * @p tilt over length @p len, the climb of a quilismapes (its shallow foot travels across the nib
     * and inks broad, tapering as it steepens). @p centred backs the preamble off by half its length
     * so a standalone wavy note sits on @p s; otherwise the flourish springs forward from @p s (a
     * connected or stepped component).
     */
    static Stroke Quilisma(PointF s, int tilt, int waves, double len, bool rise, bool centred);
    static Stroke Comma(double x, double y, int tilt);
    /**
     * A liquescent stroke (cephalicus / epiphonus). When @p stem > 0 the note's own melodic stroke of
     * that length - travelling along @p dir from @p s - tapers at its tip into an inward curl, the
     * diminishing 'liquescence'; the lead-in keeps it attached to its ligature rather than a detached
     * blob. When @p stem == 0 there is no lead-in and the whole component is the curl, springing
     * straight off @p s along @p dir - a note rendered entirely as a hook off the previous nc (used
     * when a connected liquescent carries no @tilt of its own). @p r0 is the curl's starting radius
     * (a small terminal flourish, or a note-sized hook). @p curve sets the curl hand (c = clockwise,
     * a = anticlockwise). @p looped chooses the curl's ending: false leaves an open scroll that winds
     * past a full turn and tapers inward to a near-point; true seals it into a closed loop whose tail
     * winds exactly back to touch the tip (the beginning of the liquescence for a whole-note hook).
     * The curl begins tangent to @p dir so stroke and curl flow as one gesture.
     */
    static Stroke Loop(PointF s, PointF dir, int curve, bool looped, double stem, double r0);
    /**
     * The terminal curl of a liquescent on its own: a spiral of starting radius @p r0 springing off
     * @p tip and leaving it tangent to the unit travel direction @p d (the lead-in stroke's exit
     * direction), so stroke and curl flow without a kink. @p curve sets the hand (a = anticlockwise /
     * left of travel, c = clockwise / right) and @p looped the ending (false an open scroll tapering
     * inward to a near-point; true a closed loop winding one full turn back to touch the tip). Returns
     * just the spiral points, to be appended after a lead-in that already ends at @p tip. Factored out
     * of Loop so a straight stem (Loop) and a bowed lead-in (CurvedLoop) share the same curl.
     */
    static std::vector<PointF> Curl(PointF tip, PointF d, int curve, bool looped, double r0);
    /**
     * A curved stroke whose @curve (a / c) bows the centreline gently to one side, but which is
     * primarily a *smooth transition* between the previous and next strokes. @p tIn is the travel
     * direction arriving at the start @p s (the previous stroke's exit tangent) and @p tOut is the
     * travel direction the next stroke leaves in; the cubic's end tangents are aligned to these so
     * the join flows without a kink. When the following nc is also curved the two strokes share the
     * joint tangent, so their bows read as one continuous calligraphic curve rather than two
     * independent scoops. Falls back to the chord direction when a neighbour tangent is absent.
     */
    static Stroke CurvedStroke(PointF s, int tilt, int hand, double len, PointF tIn, PointF tOut);
    /**
     * A liquescent whose melodic lead-in is itself a curved stroke: @p curveHand (the <nc>'s @curve)
     * bows the lead-in via CurvedStroke, then the terminal curl springs off its exit tangent via Curl.
     * So an <nc> carrying both @curve and a <liquescent> child reads as a curved stroke flowing into
     * the curl, instead of the @curve being dropped in favour of a straight stem. @p tilt / @p len /
     * @p tIn / @p tOut are the CurvedStroke lead-in parameters; @p curlHand / @p looped / @p r0 shape
     * the curl (the <liquescent>'s own @curve / @looped, the hand falling back to the <nc>'s @curve).
     */
    static Stroke CurvedLoop(PointF s, int tilt, int curveHand, double len, PointF tIn, PointF tOut,
        int curlHand, bool looped, double r0);

    /**
     * Sweep the fixed nib along a centreline, producing the two offset edges of the ribbon (one
     * point per centreline point). The stroke width law and the along-stroke taper are applied here,
     * so the edges must be computed over the whole gesture before being cut into per-nc slices.
     */
    static void NibEdges(const std::vector<PointF> &pts, double scale, std::vector<PointF> &left,
        std::vector<PointF> &right, bool taperStart = true, bool taperEnd = true);
    /** Sweep the fixed nib along a centreline and return one closed outline (the whole gesture).
     * @p taperStart / @p taperEnd hold the corresponding end at full nib width instead of lifting it
     * to a hairline - used where an episema joins the stroke end so the ink reads as one shape. */
    static std::vector<PointF> NibRibbon(
        const std::vector<PointF> &pts, double scale = 1.0, bool taperStart = true, bool taperEnd = true);
    /** A punctum: the rounded footprint of a single broad-nib dab centred at @p c (pen space). */
    static std::vector<PointF> Punctum(PointF c);
    /** Endpoint-preserving binomial low-pass of a polyline (removes finite-difference edge noise). */
    static void SmoothPolyline(std::vector<PointF> &p, int passes);
    /** Control points for the segment p1->p2 of a centripetal Catmull-Rom spline through p0..p3. */
    static void CentripetalControls(PointF p0, PointF p1, PointF p2, PointF p3, PointF &c1, PointF &c2);
    /** Centripetal Catmull-Rom -> cubic Bézier through a closed loop of points (a smooth ink edge). */
    static std::vector<PointF> SmoothClosed(const std::vector<PointF> &p);
    /** Centripetal Catmull-Rom -> cubic Bézier through an open polyline (endpoints kept, no wrap). */
    static std::vector<PointF> SmoothOpen(const std::vector<PointF> &p);
    /**
     * Ink one run (a maximal sequence of connected components - a single continuous pen gesture)
     * into @p geo, cutting the ribbon into per-nc slices. Runs are separated by @con="g" breaks, so
     * a neume with no breaks is a single run and a pes subbipunctis is several.
     */
    static void InkRun(const std::vector<Seg> &run, NeumeGeometry &geo, Slant slant);

    /**
     * Ink the episema accents of every marked nc across @p runs into @p geo (phase 3 of Build,
     * factored out so the accent-placement convention is one self-contained unit). Each episema is a
     * short broad-nib stroke set just clear of the ink of the stroke it marks; @place chooses the
     * side and @form the orientation - see the .cpp for the full convention.
     */
    static void BuildEpisemata(
        const std::vector<std::vector<Seg>> &runs, const std::vector<NcInfo> &ncs, NeumeGeometry &geo, Slant slant);

    /**
     * Build one per-nc closed ribbon slice covering centreline knots [a, b]. @p leftCurve and
     * @p rightCurve are the two offset edges already smoothed over the WHOLE gesture (SmoothOpen
     * output, a flat [P0, C0a, C0b, P1, ...] list), so a slice simply re-uses the relevant Bézier
     * segments. Because the boundary tangents come from the global smoothing, adjacent slices are
     * tangent-continuous at the shared cross-section (no kink); the butt cross-sections at a and b
     * are kept straight so the slices abut with no gap and no seam.
     */
    static std::vector<PointF> SliceRibbon(
        const std::vector<PointF> &leftCurve, const std::vector<PointF> &rightCurve, int a, int b);
};

} // namespace vrv

#endif // __VRV_CALLIGRAPHICNEUME_H__
