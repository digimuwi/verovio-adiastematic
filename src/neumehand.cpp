/////////////////////////////////////////////////////////////////////////////
// Name:        neumehand.cpp
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "calligraphicneume.h"

//----------------------------------------------------------------------------

#include <algorithm>
#include <cctype>

namespace vrv {

//----------------------------------------------------------------------------
// The hands: one entry per script token (staffDef/@notationsubtype)
//----------------------------------------------------------------------------

// Each family put its own neume shapes on the lines, and hands differ measurably even within a family, so
// where a pitch sits on a pen stroke is the hand's, not the renderer's. The numbers below come from the
// facsimile set measured on 2026-10-27 (58 examples, 18 manuscripts; the example ids in the comments are
// its rows) and the literature reviewed with it. A reading in Dendermonde is good to about 0.1 step, in
// the smaller scans to 0.2-0.3, so a difference below that between two hands is noise. Lengths are in staff
// steps (line to the next space). A hand not listed here, or a field left at its default, behaves as the
// St. Gall hand the renderer was built on.

namespace {

    using Hand = CalligraphicNeume::Hand;

    Hand StGall()
    {
        // The St. Gall catalogue on lines (doc/hartker_neumes_on_lines.mei): every default - one interline
        // of reach and the pitch where the pen leaves an opening stroke, the liquescent curl at its
        // natural size.
        return Hand();
    }

    Hand Hildegard()
    {
        // Dendermonde cod. 9 and the Riesencodex (Wiesbaden Hs. 2): German neumes of the Hufnagel type on
        // four lines. The virga's pitch is its head and the thick stroke hangs about one step below it; the
        // same passage in both books (hildegard-01, -08) puts a G virga after a punctum E with its head on
        // G, where the foot would read F. A clivis lead-in starts about a step under the arch (-01, -03).
        Hand h;
        h.token = "hildegard";
        h.solo = { 1.0, 1.0 };
        h.lead = { 1.0, 1.0 };
        h.foot = { 1.0, 1.0 };
        // A liquescent with its own stroke: the plain cephalicus has its pitch on the lower part of the
        // thick tail and its hairline runs 0.3 to 0.85 step on (hildegard-10, -16, -17); an ascending
        // liquescent pes ends its thick stroke at the pitch with the hairline about half a step beyond (our
        // scan check of O splendidissima gemma). The curled three-note form ends on its pitch, which is the
        // universal curl-only rule in Build, not a hand setting.
        h.liqOvershootUp = 0.5;
        h.liqOvershootDown = 0.6;
        // Letter clefs c and f that move between and within systems; a red f stroke marks the pitch f,
        // through a space where the ruling puts it there (hildegard-07, -11).
        h.clef = Hand::ClefForm::Letter;
        h.pitchMarkers = { { 'f', "#b0302a" } };
        return h;
    }

    Hand German()
    {
        // Koeln, Dombibliothek, Cod. 1161: the virga's pitch is where the hairline flag meets the thick
        // stroke, which hangs 1.6 to 1.9 steps below it (german-01, -06). The clivis lead-in rises from the
        // F line to its arch on a (german-04). In a pes of a second the lower note sits in the middle of a
        // short rising hairline foot, about 0.6 step long (german-02).
        Hand h;
        h.token = "german";
        h.solo = { 1.75, 1.0 };
        h.lead = { 2.0, 1.0 };
        h.foot = { 0.6, 0.5 };
        // The epiphonus ends where its small curl turns (german-05, 0); the cephalicus ends on its pitch
        // once (german-04) and half a step past it once (german-06).
        h.liqOvershootUp = 0.0;
        h.liqOvershootDown = 0.25;
        h.clef = Hand::ClefForm::Letter;
        h.pitchMarkers = { { 'f', "#b0302a" }, { 'c', "#c9a227" } };
        return h;
    }

    Hand Hufnagel()
    {
        // Karlsruhe Aug. perg. 60 and Stams 40: the nail head carries the pitch and its tail runs about 2.3
        // steps below (hufnagel-03); the n-shaped clivis has its first pitch at the top-left corner, the left
        // stem's tail reaching 1 to 1.3 steps below it (hufnagel-02). A lozenge is read at its centre.
        Hand h;
        h.token = "hufnagel";
        h.solo = { 2.3, 1.0 };
        h.lead = { 1.15, 1.0 };
        h.dot = Hand::Dot::Lozenge;
        h.clef = Hand::ClefForm::Letter;
        h.pitchMarkers = { { 'f', "#b0302a" }, { 'c', "#c9a227" } };
        return h;
    }

    Hand Messine()
    {
        // Bellelay (Porrentruy Ms. 18): a single stroke is centred on its pitch and reaches about 0.6 step
        // either side (messine-03, -04). The 7-shaped clivis has its first pitch at the knob where the pen
        // sets down; the corner, 0.4 step higher, carries none (messine-02).
        Hand h;
        h.token = "messine";
        h.solo = { 1.2, 0.5 };
        h.lead = { 0.4, 0.0 };
        h.clef = Hand::ClefForm::Letter;
        return h;
    }

    Hand MessineGerman()
    {
        // Berlin Mus. ms. 40078 (one scribe): the single note's pitch is the centre of its head, the flag
        // tip 0.3 to 0.4 step above it and the stem end 0.85 to 1 below (messine-german-15) - a 1.25-step
        // stroke with its pitch 0.72 of the way up. The 7-clivis: knob, then a corner 0.4 to 0.5 step above
        // the first pitch (messine-german-16).
        Hand h;
        h.token = "messine-german";
        h.solo = { 1.25, 0.72 };
        h.lead = { 0.45, 0.0 };
        // The epiphonus overshoots by 0.4 (messine-german-11); the cephalicus forms end on the pitch (-12,
        // -13).
        h.liqOvershootUp = 0.4;
        h.liqOvershootDown = 0.0;
        h.clef = Hand::ClefForm::Letter;
        h.pitchMarkers = { { 'f', "#b0302a" }, { 'c', "#c9a227" } };
        return h;
    }

    Hand Cistercian()
    {
        // Rein (Wien, OeNB Cod. 1799**): stroke neumes on a Guidonian staff with red F and yellow c lines.
        // The epiphonus's pitch is on its hairline, 0.4 step past the thick stroke (cistercian-01); the
        // S-shaped cephalicus ends on its pitch (-02). The custos is a thick head on the line with a tail
        // rising to a dot (-03).
        Hand h;
        h.token = "cistercian";
        h.liqOvershootUp = 0.4;
        h.liqOvershootDown = 0.0;
        h.clef = Hand::ClefForm::Letter;
        h.custos = Hand::CustosForm::HeadTail;
        h.pitchMarkers = { { 'f', "#b0302a" }, { 'c', "#c9a227" } };
        return h;
    }

    Hand Beneventan()
    {
        // Benevento 34, Barb. lat. 603: "the end of the tail indicates the pitch of the second note" (Hiley
        // 1993, p. 429), within 0.15 step (beneventan-03). The custos is a check mark with its pitch at the
        // lower blob (beneventan-01, low confidence).
        Hand h;
        h.token = "beneventan";
        h.liqOvershootUp = 0.1;
        h.liqOvershootDown = 0.1;
        // Not a point script: its pes and clivis are joined pen strokes with the pitch at their ends (the pes
        // foot at the lower pitch, its upper end at the upper, beneventan-01; the clivis tail ending on its
        // pitch, -03), so it is drawn by the stroke builder like the St. Gall hand.
        h.custos = Hand::CustosForm::Check;
        return h;
    }

    Hand Aquitanian()
    {
        // Saint-Yrieix (Paris lat. 903), Silos: separate points stepping evenly around one line keyed to the
        // mode, no clef; the custos a slanted stroke with its pitch at a small hook part way up
        // (aquitanian-02).
        Hand h;
        h.token = "aquitanian";
        h.pointScript = true;
        // Each dot one pitch, stepping evenly, a clivis two dots stacked straight above each other at the same
        // x (aquitanian-01, -02: 305,32 over 305,57; 10,65 over 10,85). The dots are round blobs 10 to 11 px
        // across at 13 px a step (aquitanian-03), while the virga strokes are fine, about 3 px (a narrow nib);
        // detached dots stand close (aquitanian-01 'te': dashes 16 px apart at 24 px a step).
        h.dot = Hand::Dot::Point;
        h.pen.width = 4.0;
        h.pen.thin = 1.2;
        h.markWidth = 10.5;
        h.gesture.breakGap = 4.0;
        h.custos = Hand::CustosForm::Slant;
        return h;
    }

    Hand CentralItalian()
    {
        // Bodmer 74: long horizontal dashes centred on their pitch (central-italian-01, -02); red F and
        // yellow c lines.
        Hand h;
        h.token = "central-italian";
        h.pointScript = true;
        // A bare note is a short level dash, a step long (an nc with a level @tilt is one in any point script).
        h.dot = Hand::Dot::Dash;
        h.gesture.breakGap = 6.0;
        h.clef = Hand::ClefForm::Letter;
        h.pitchMarkers = { { 'f', "#b0302a" }, { 'c', "#c9a227" } };
        return h;
    }

    Hand Nonantolan()
    {
        // Nonantola (PM II): separate virgae and puncta, tall strokes rising to their pitch at the top
        // (nonantolan-01 to -03).
        Hand h;
        h.token = "nonantolan";
        h.pointScript = true;
        h.solo = { 3.0, 1.0 };
        h.gesture.breakGap = 6.0;
        h.clef = Hand::ClefForm::Letter;
        return h;
    }

    Hand Square()
    {
        // The Humbert codex: each note a square, an oblique carrying a pitch at the vertical centre of each
        // end (square-02); the plica's tail runs 0.6 to 1.3 step past the note (square-01); the custos a
        // small square with a hairline tail (square-03).
        Hand h;
        h.token = "square";
        h.pointScript = true;
        h.dot = Hand::Dot::Square;
        // The nib held upright (90 degrees): a level sweep comes out a square as tall as the nib is wide, a
        // vertical connector or stem a hairline, an oblique a parallelogram with upright ends. A square is about
        // 14 px tall and 10 wide at 13 px a step (square-02, the crop's own scale), a touch more than a step;
        // the oblique beside it spans 18-19 px. Stems and connectors run plumb along the square's side (the
        // right of a virga and a pes, the left of a clivis), and a joined lower note abuts the square before it.
        h.pen.angleDeg = 90.0;
        h.pen.width = 14.0;
        h.pen.thin = 1.4;
        h.markWidth = 10.0;
        h.upright = true;
        h.edgeStems = true;
        h.gesture.breakGap = 5.0;
        h.liqOvershootDown = 0.9;
        h.custos = Hand::CustosForm::Square;
        return h;
    }

    const std::vector<Hand> &Hands()
    {
        static const std::vector<Hand> hands = { StGall(), Hildegard(), German(), Hufnagel(), Messine(),
            MessineGerman(), Cistercian(), Beneventan(), Aquitanian(), CentralItalian(), Nonantolan(), Square() };
        return hands;
    }

} // namespace

const CalligraphicNeume::Hand &CalligraphicNeume::HandFor(const std::string &token)
{
    std::string key = token;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    const std::vector<Hand> &hands = Hands();
    for (const Hand &h : hands) {
        if (h.token == key) return h;
    }
    return hands.front();
}

std::vector<std::string> CalligraphicNeume::HandTokens()
{
    std::vector<std::string> tokens;
    for (const Hand &h : Hands()) tokens.push_back(h.token);
    return tokens;
}

} // namespace vrv
