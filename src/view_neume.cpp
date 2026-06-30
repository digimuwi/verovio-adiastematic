/////////////////////////////////////////////////////////////////////////////
// Name:        view_neume.cpp
// Author:      Andrew Tran, Juliette Regimbal
// Created:     2017
// Copyright (c) Author and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "view.h"

//----------------------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <map>
#include <math.h>

//----------------------------------------------------------------------------

#include "calligraphicneume.h"
#include "devicecontext.h"
#include "divline.h"
#include "doc.h"
#include "episema.h"
#include "layer.h"
#include "layerelement.h"
#include "nc.h"
#include "neume.h"
#include "note.h"
#include "quilisma.h"
#include "resources.h"
#include "signiflet.h"
#include "smufl.h"
#include "staff.h"
#include "syllable.h"
#include "zone.h"

namespace vrv {

void View::DrawSyllable(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    Syllable *syllable = dynamic_cast<Syllable *>(element);
    assert(syllable);

    /******************************************************************/
    // Start the Beam graphic and draw the children

    dc->StartGraphic(element, "", element->GetID());

    /******************************************************************/
    // Draw the children

    this->DrawLayerChildren(dc, syllable, layer, staff, measure);

    dc->EndGraphic(element, this);
}

void View::DrawLiquescent(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    dc->StartGraphic(element, "", element->GetID());

    dc->EndGraphic(element, this);
}

void View::DrawNc(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    Nc *nc = dynamic_cast<Nc *>(element);
    assert(nc);

    if (m_options->m_neumeAsNote.GetValue()) {
        this->DrawNcAsNotehead(dc, nc, layer, staff, measure);
        return;
    }

    dc->StartGraphic(element, "", element->GetID());

    this->DrawNcGlyphs(dc, nc, staff);

    /******************************************************************/

    // Draw the children
    this->DrawLayerChildren(dc, nc, layer, staff, measure);

    dc->EndGraphic(element, this);
}

void View::DrawNeume(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    Neume *neume = dynamic_cast<Neume *>(element);
    assert(neume);

    /******************************************************************/
    // Start the Neume graphic and draw the children

    dc->StartGraphic(element, "", element->GetID());

    // Adiastematic (staffless) rendering: the whole neume is one calligraphic pen gesture built
    // from the visual attributes, so the per-nc glyphs are not drawn.
    if (m_options->m_neumeCalligraphic.GetValue()) {
        this->DrawNeumeAdiastematic(dc, neume, staff);
        dc->EndGraphic(element, this);
        return;
    }

    this->DrawLayerChildren(dc, neume, layer, staff, measure);

    if (m_options->m_neumeAsNote.GetValue()) {

        Nc *first = vrv_cast<Nc *>(neume->GetFirst(NC));
        Nc *last = vrv_cast<Nc *>(neume->GetLast(NC));

        if (first != last) {

            const int unit = m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
            const int lineWidth = m_doc->GetOptions()->m_octaveLineThickness.GetValue() * unit;

            int x1 = first->GetDrawingX();
            int x2 = last->GetDrawingX();
            int y = staff->GetDrawingY();

            this->CalcOffset(dc, x1, y);
            this->CalcOffsetX(dc, x2);

            const int maxNcY = std::max(first->GetDrawingY(), last->GetDrawingY());
            y = std::max(y, maxNcY + unit);
            y += 2 * unit;

            x1 += lineWidth / 2;
            x2 += 2 * last->GetDrawingRadius(m_doc) - lineWidth / 2;

            dc->SetPen(lineWidth, PEN_SOLID, 0, 0, LINECAP_BUTT, LINEJOIN_MITER);

            dc->DrawLine(this->ToDeviceContextX(x1), this->ToDeviceContextY(y), this->ToDeviceContextX(x2),
                this->ToDeviceContextY(y));
            dc->DrawLine(this->ToDeviceContextX(x1), this->ToDeviceContextY(y + lineWidth / 2),
                this->ToDeviceContextX(x1), this->ToDeviceContextY(y - unit));
            dc->DrawLine(this->ToDeviceContextX(x2), this->ToDeviceContextY(y + lineWidth / 2),
                this->ToDeviceContextX(x2), this->ToDeviceContextY(y - unit));

            dc->ResetPen();
        }
    }

    dc->EndGraphic(element, this);
}

void View::DrawNcAsNotehead(DeviceContext *dc, Nc *nc, Layer *layer, Staff *staff, Measure *measure)
{
    /******************************************************************/
    // Start the Neume graphic and draw the children

    dc->StartGraphic(nc, "", nc->GetID());

    const int noteX = nc->GetDrawingX();
    const int noteY = nc->GetDrawingY();

    bool cueSize = false;
    if (nc->FindDescendantByType(LIQUESCENT)) {
        cueSize = true;
    }

    this->DrawSmuflCode(dc, noteX, noteY, SMUFL_E0A4_noteheadBlack, staff->m_drawingStaffSize, cueSize, true);

    dc->EndGraphic(nc, this);
}

void View::DrawDivLine(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(element);
    assert(layer);
    assert(staff);
    assert(measure);

    DivLine *divLine = dynamic_cast<DivLine *>(element);
    assert(divLine);

    dc->StartGraphic(element, "", element->GetID());

    int sym = 0;

    switch (divLine->GetForm()) {
        case divLineLog_FORM_minima: sym = SMUFL_E8F3_chantDivisioMinima; break;
        case divLineLog_FORM_maior: sym = SMUFL_E8F4_chantDivisioMaior; break;
        case divLineLog_FORM_maxima: sym = SMUFL_E8F5_chantDivisioMaxima; break;
        case divLineLog_FORM_finalis: sym = SMUFL_E8F6_chantDivisioFinalis; break;
        case divLineLog_FORM_caesura: sym = SMUFL_E8F8_chantCaesura; break;
        case divLineLog_FORM_virgula: sym = SMUFL_E8F7_chantVirgula; break;
        default: break;
    }

    int x, y;
    x = divLine->GetDrawingX();
    y = staff->GetDrawingY();

    this->CalcOffset(dc, x, y);

    y -= (m_doc->GetDrawingUnit(staff->m_drawingStaffSize)) * 3;

    if (staff->HasDrawingRotation()) {
        y -= staff->GetDrawingRotationOffsetFor(x);
    }

    this->DrawSmuflCode(dc, x, y, sym, staff->m_drawingStaffSize, false, true);

    dc->EndGraphic(element, this);
}

void View::DrawEpisema(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    Episema *episema = vrv_cast<Episema *>(element);
    assert(episema);

    dc->StartGraphic(element, "", element->GetID());

    Nc *nc = vrv_cast<Nc *>(episema->GetFirstAncestor(NC));
    if (nc) {
        int x = nc->GetDrawingX();
        int y = nc->GetDrawingY();

        if (!nc->m_drawingGlyphs.empty()) {
            x += m_doc->GetGlyphWidth(nc->m_drawingGlyphs.at(0).m_fontNo, staff->m_drawingStaffSize, false) / 2;
        }

        if (staff->HasDrawingRotation()) {
            y -= staff->GetDrawingRotationOffsetFor(x);
        }

        this->CalcOffset(dc, x, y);

        const int unit = m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
        const bool above = (episema->GetPlace() != EVENTREL_below);
        // The SMuFL glyphs place their mark ~1 unit from the anchor (e.g. chantEpisema bar
        // is at +125 font units = +1 drawing unit above the anchor). This naturally lands
        // on the adjacent space when the anchor is on a line. When the anchor is on a space
        // the inherent +1 unit would land on the intermediate line instead, so shift the
        // anchor by 1 unit toward the target direction first.
        if (!staff->IsOnStaffLine(y, m_doc)) {
            y += above ? unit : -unit;
        }

        int sym = 0;
        if (episema->GetForm() == episemaVis_FORM_h) {
            sym = SMUFL_E9D8_chantEpisema;
        }
        else {
            sym = above ? SMUFL_E9D0_chantIctusAbove : SMUFL_E9D1_chantIctusBelow;
        }

        this->DrawSmuflCode(dc, x, y, sym, staff->m_drawingStaffSize, false, true);
    }

    dc->EndGraphic(element, this);
}

void View::DrawOriscus(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    dc->StartGraphic(element, "", element->GetID());

    dc->EndGraphic(element, this);
}

void View::DrawQuilisma(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    dc->StartGraphic(element, "", element->GetID());

    dc->EndGraphic(element, this);
}

void View::DrawStrophicus(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    dc->StartGraphic(element, "", element->GetID());

    dc->EndGraphic(element, this);
}

void View::DrawNeumeAdiastematic(DeviceContext *dc, Neume *neume, Staff *staff)
{
    assert(dc);
    assert(neume);
    assert(staff);

    // Collect the visual attributes of each <nc> child, in document order, keeping the matching Nc
    // objects so each component's ink can be inked inside its own <nc> graphic.
    std::vector<CalligraphicNeume::NcInfo> ncInfos;
    std::vector<Nc *> ncObjects;
    for (Object *child : neume->GetChildren()) {
        if (!child->Is(NC)) continue;
        Nc *nc = vrv_cast<Nc *>(child);
        ncObjects.push_back(nc);

        CalligraphicNeume::NcInfo info;
        info.tilt = nc->GetTilt();
        info.curve = nc->GetCurve();
        info.sShape = nc->GetSShape();
        info.longStroke = (nc->GetRellen() == ncForm_RELLEN_l);
        info.shortStroke = (nc->GetRellen() == ncForm_RELLEN_s);
        info.gapped = (nc->GetCon() == ncForm_CON_g);
        const std::string intm = nc->GetIntm();
        info.intm = intm.empty() ? 0 : intm.front();

        for (Object *grandChild : nc->GetChildren()) {
            if (grandChild->Is(EPISEMA)) {
                Episema *episema = vrv_cast<Episema *>(grandChild);
                // A bare <episema> defaults to a vertical mark (across the stroke).
                const int form = episema->HasForm() ? episema->GetForm() : episemaVis_FORM_v;
                info.episemata.push_back({ form, episema->GetPlace() });
            }
            else if (grandChild->Is(ORISCUS)) {
                // <oriscus> is a purely semantic ("inhaltliche") marking: the oriscus's visual pen form
                // is carried by @s-shape, so the element itself has no effect on the rendered gesture -
                // it leaves the nc shaped by its attributes alone (a bare oriscus stays a punctum).
            }
            else {
                info.hasNonEpisemaChild = true;
                if (grandChild->Is(STROPHICUS)) info.strophicus = true;
                else if (grandChild->Is(QUILISMA)) {
                    info.quilisma = true;
                    Quilisma *quilisma = vrv_cast<Quilisma *>(grandChild);
                    info.waves = quilisma->HasWaves() ? quilisma->GetWaves() : 0;
                }
                else if (grandChild->Is(LIQUESCENT)) {
                    info.liquescent = true;
                    // @curve (a / c, the curl hand) and @looped are not in verovio's Liquescent model,
                    // so they arrive as unsupported attributes; read them straight off the element.
                    ArrayOfStrAttr attributes;
                    grandChild->GetAttributes(&attributes);
                    for (const auto &attr : attributes) {
                        // Store the curatureDirection_CURVE enum value, not the raw character, so it
                        // matches the enum comparison in CalligraphicNeume::Loop (a = anticlockwise).
                        if (attr.first == "curve" && attr.second == "a")
                            info.liquescentCurve = curvatureDirection_CURVE_a;
                        else if (attr.first == "curve" && attr.second == "c")
                            info.liquescentCurve = curvatureDirection_CURVE_c;
                        else if (attr.first == "looped")
                            info.liquescentLooped = (attr.second == "true");
                    }
                }
            }
        }
        ncInfos.push_back(info);
    }
    if (ncObjects.empty()) return;

    const int unit = m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
    const double scale = (double)unit / CalligraphicNeume::s_unitPx;
    // The scribe's forward slant: convert the option's degrees to the shear's tangent.
    const CalligraphicNeume::Slant slant{ std::tan(m_options->m_neumeCalligraphicSlant.GetValue() * M_PI / 180.0),
        m_options->m_neumeCalligraphicSlantBias.GetValue() };
    const CalligraphicNeume::NeumeGeometry geo = CalligraphicNeume::Build(ncInfos, scale, slant);
    if (geo.ncs.size() != ncObjects.size()) return;

    // Anchor the gesture at the first nc. The helper works in pen space (+y down); verovio logical
    // space is +y up, so the y component is flipped here, once, at the conversion boundary.
    const int anchorX = ncObjects.front()->GetDrawingX();
    int anchorY = ncObjects.front()->GetDrawingY();
    if (staff->HasDrawingRotation()) {
        anchorY -= staff->GetDrawingRotationOffsetFor(anchorX);
    }

    auto toDeviceContext = [&](const CalligraphicNeume::PointF &p) -> Point {
        const int x = anchorX + (int)std::lround(p.x);
        const int y = anchorY - (int)std::lround(p.y);
        return Point(this->ToDeviceContextX(x), this->ToDeviceContextY(y));
    };

    // The neume reads as one continuous gesture, but each <nc> owns its slice of the ink. Inking
    // each slice inside its own <nc> graphic gives every component an addressable element with a
    // real bounding box, exactly like the per-glyph rendering, so downstream editors can select and
    // colour individual components.
    // Track the logical bounding box of the whole inked gesture so the significative letters can be
    // positioned relative to it.
    int gestureX1 = INT_MAX, gestureY1 = INT_MAX, gestureX2 = INT_MIN, gestureY2 = INT_MIN;
    auto extendBBox = [&](const CalligraphicNeume::PointF &p) {
        const int lx = anchorX + (int)std::lround(p.x);
        const int ly = anchorY - (int)std::lround(p.y);
        gestureX1 = std::min(gestureX1, lx);
        gestureX2 = std::max(gestureX2, lx);
        gestureY1 = std::min(gestureY1, ly);
        gestureY2 = std::max(gestureY2, ly);
    };

    for (size_t i = 0; i < ncObjects.size(); ++i) {
        const CalligraphicNeume::NcGeometry &ncGeo = geo.ncs[i];
        if (ncGeo.ribbon.empty() && ncGeo.episemata.empty()) continue;

        for (const CalligraphicNeume::PointF &p : ncGeo.ribbon) extendBBox(p);
        for (const std::vector<CalligraphicNeume::PointF> &epi : ncGeo.episemata)
            for (const CalligraphicNeume::PointF &p : epi) extendBBox(p);

        dc->StartGraphic(ncObjects[i], "", ncObjects[i]->GetID());

        // Ink this component's slice as one filled closed path. With the default brush (no explicit
        // colour) the fill is inherited from the page styling, exactly like a notehead.
        if (!ncGeo.ribbon.empty()) {
            std::vector<Point> outline;
            outline.reserve(ncGeo.ribbon.size());
            for (const CalligraphicNeume::PointF &p : ncGeo.ribbon) outline.push_back(toDeviceContext(p));

            dc->SetPen(0, PEN_SOLID);
            dc->SetBrush(1.0);
            dc->DrawClosedBezierPath(outline);
            dc->ResetBrush();
            dc->ResetPen();
        }

        // Episemata are short broad-nib strokes in the same hand as the ribbon: filled closed paths,
        // each butting against the broad end of the stroke it marks.
        for (const std::vector<CalligraphicNeume::PointF> &epi : ncGeo.episemata) {
            if (epi.empty()) continue;
            std::vector<Point> outline;
            outline.reserve(epi.size());
            for (const CalligraphicNeume::PointF &p : epi) outline.push_back(toDeviceContext(p));

            dc->SetPen(0, PEN_SOLID);
            dc->SetBrush(1.0);
            dc->DrawClosedBezierPath(outline);
            dc->ResetBrush();
            dc->ResetPen();
        }

        dc->EndGraphic(ncObjects[i], this);
    }

    // The significative letters are placed relative to the gesture's bounding box. Fall back to the
    // anchor if nothing was inked (e.g. an all-empty neume).
    if (gestureX1 > gestureX2) {
        gestureX1 = gestureX2 = anchorX;
        gestureY1 = gestureY2 = anchorY;
    }
    this->DrawNeumeSignifLets(dc, neume, staff, gestureX1, gestureY1, gestureX2, gestureY2);
}

void View::DrawNeumeSignifLets(DeviceContext *dc, Neume *neume, Staff *staff, int x1, int y1, int x2, int y2)
{
    assert(dc);
    assert(neume);
    assert(staff);

    std::vector<SignifLet *> signifLets;
    for (Object *child : neume->GetChildren()) {
        if (child->Is(SIGNIFLET)) signifLets.push_back(vrv_cast<SignifLet *>(child));
    }
    if (signifLets.empty()) return;

    const int unit = m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
    const int margin = unit / 2;
    const int xCenter = (x1 + x2) / 2;

    // Upright (roman) text in the document text font. Litterae significativae are small annotations,
    // so they are drawn at a fraction of the lyric font size rather than full lyric size.
    FontInfo font;
    font.SetFaceName(m_doc->GetResources().GetTextFont());
    font.SetPointSize(m_doc->GetDrawingLyricFont(staff->m_drawingStaffSize)->GetPointSize() * 3 / 5);
    font.SetStyle(FONTSTYLE_normal);
    font.SetWeight(FONTWEIGHT_normal);

    const int letterHeight = m_doc->GetTextGlyphHeight('x', &font, false);
    const int gap = unit / 2; // Horizontal space between adjacent letters sharing a placement.

    // Group the letters by @place so that several letters at the same place are laid out as a
    // horizontal run (read left to right), rather than being stacked on top of one another.
    std::map<data_EVENTREL, std::vector<SignifLet *>> groups;
    std::vector<data_EVENTREL> order;
    for (SignifLet *signifLet : signifLets) {
        if (signifLet->GetText().empty()) continue;
        // Default placement is above the neume when @place is absent.
        const data_EVENTREL place = signifLet->HasPlace() ? signifLet->GetPlace() : EVENTREL_above;
        if (!groups.count(place)) order.push_back(place);
        groups[place].push_back(signifLet);
    }

    dc->SetFont(&font);

    for (const data_EVENTREL &place : order) {
        const std::vector<SignifLet *> &group = groups[place];

        // Measure each letter and the width of the whole run (letters plus the gaps between them).
        std::vector<int> widths;
        int runWidth = 0;
        for (SignifLet *signifLet : group) {
            TextExtend extend;
            dc->GetTextExtent(signifLet->GetText(), &extend, false);
            widths.push_back(extend.m_width);
            runWidth += extend.m_width;
        }
        runWidth += gap * (static_cast<int>(group.size()) - 1);

        // Vertical baseline for the run. Above sits clear of the top; below clear of the bottom;
        // pure left/right sit centred on the gesture.
        int y;
        if (place == EVENTREL_below || place == EVENTREL_below_left || place == EVENTREL_below_right) {
            y = y1 - margin - letterHeight;
        }
        else if (place == EVENTREL_left || place == EVENTREL_right) {
            y = (y1 + y2) / 2 - letterHeight / 2;
        }
        else {
            y = y2 + margin;
        }

        // Left edge of the run: left of the gesture (the run ends at x1 - margin), right of it (the
        // run starts at x2 + margin), or centred over it.
        int runX;
        if (place == EVENTREL_left || place == EVENTREL_above_left || place == EVENTREL_below_left) {
            runX = x1 - margin - runWidth;
        }
        else if (place == EVENTREL_right || place == EVENTREL_above_right || place == EVENTREL_below_right) {
            runX = x2 + margin;
        }
        else {
            runX = xCenter - runWidth / 2;
        }

        // Draw the letters left to right.
        int x = runX;
        for (size_t i = 0; i < group.size(); ++i) {
            SignifLet *signifLet = group[i];
            const std::u32string str = signifLet->GetText();

            dc->StartGraphic(signifLet, "", signifLet->GetID());
            dc->StartText(this->ToDeviceContextX(x), this->ToDeviceContextY(y), HORIZONTALALIGNMENT_left);
            dc->DrawText(UTF32to8(str), str);
            dc->EndText();
            dc->EndGraphic(signifLet, this);

            x += widths[i] + gap;
        }
    }

    dc->ResetFont();
}

void View::DrawNcGlyphs(DeviceContext *dc, Nc *nc, Staff *staff)
{
    assert(dc);
    assert(nc);
    assert(staff);

    int ncX = nc->GetDrawingX();
    int ncY = nc->GetDrawingY();

    if (staff->HasDrawingRotation()) {
        ncY -= staff->GetDrawingRotationOffsetFor(ncX);
    }

    for (auto &glyph : nc->m_drawingGlyphs) {
        this->DrawSmuflCode(
            dc, ncX + glyph.m_xOffset, ncY + glyph.m_yOffset, glyph.m_fontNo, staff->m_drawingStaffSize, false, true);
    }
}

} // namespace vrv
