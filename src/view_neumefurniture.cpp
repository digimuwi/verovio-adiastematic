/////////////////////////////////////////////////////////////////////////////
// Name:        view_neumefurniture.cpp
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "view.h"

//----------------------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>

//----------------------------------------------------------------------------

#include "calligraphicneume.h"
#include "clef.h"
#include "custos.h"
#include "devicecontext.h"
#include "doc.h"
#include "layer.h"
#include "measure.h"
#include "options.h"
#include "pitchinterface.h"
#include "staff.h"
#include "staffdef.h"
#include "vrv.h"

namespace vrv {

//----------------------------------------------------------------------------
// Staff furniture of a calligraphic neume staff (neumes on lines)
//----------------------------------------------------------------------------

// Under --neume-calligraphic the staff a neume stands on is drawn in its script's hand as well
// (CalligraphicNeume::Hand, named by staffDef/@notationsubtype or --neume-hand). Everything here is opt-in:
// without --neume-calligraphic, or on a staff that is not a neume staff, each method returns at once and the
// stock drawing is untouched. The encoding, all in plain MEI:
//
// - A one-line staff whose line is a reference pitch with no clef (Aquitanian: the Silos red line is G):
//     <staffDef lines="1" notationtype="neume" notationsubtype="aquitanian" lines.color="#c0392b"/>
//     ... <layer><clef shape="G" line="1" visible="false"/> ...
//   The hidden clef keys the line (G on line 1) and draws nothing; pitched ncs are placed around the line as
//   on any staff, and ncs get no ledger lines. A dry-point line is a faint @lines.color, e.g. "#d8d2c4" or
//   "rgba(0,0,0,0.15)".
// - Coloured staff lines (the red F and yellow c lines of a Guidonian staff): staffDef/@lines.color (MEI
//   att.staffDef.vis), one colour per line listed from the lowest line up - the order of clef/@line - or a
//   single colour for every line. A colour is #rgb, #rrggbb, rgb(r,g,b), rgba(r,g,b,a) or a basic colour name;
//   "none" leaves that line out. A line with no or an unreadable colour is drawn as usual.
// - Pitch markers that follow the clef (the red f of the Hildegard books, which runs through a space when the
//   clef puts f there and moves with a clef change): not encoded, but drawn from the hand's own markers under
//   --neume-pitch-markers. An encoded @lines.color wins over them.
// - Letter clefs and the custos: the ordinary <clef shape line> and <custos pname oct>; the hand decides how
//   they look (a letter c / F, a custos in its own form).

namespace {

    // An ink colour as the device context takes it: an 0xRRGGBB value (COLOR_NONE: the page's own colour) and
    // an opacity (negative: opaque). @p draw is false for "none" / "transparent".
    struct InkColor {
        bool draw = true;
        int color = COLOR_NONE;
        float opacity = -1.0f;
    };

    // The colours of a structured @lines.color: whitespace-separated, with the commas and spaces inside an
    // rgb() / rgba() kept together.
    std::vector<std::string> SplitColors(const std::string &value)
    {
        std::vector<std::string> tokens;
        std::string current;
        int depth = 0;
        for (char ch : value) {
            if (ch == '(') ++depth;
            if (ch == ')') depth = std::max(0, depth - 1);
            if (std::isspace((unsigned char)ch) && depth == 0) {
                if (!current.empty()) tokens.push_back(current);
                current.clear();
                continue;
            }
            current += ch;
        }
        if (!current.empty()) tokens.push_back(current);
        return tokens;
    }

    // Read one colour; false when it cannot be read (the line is then drawn as usual).
    bool ParseColor(std::string value, InkColor &ink)
    {
        std::transform(
            value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (value == "none" || value == "transparent") {
            ink.draw = false;
            return true;
        }
        if (!value.empty() && value[0] == '#') {
            const std::string hex = value.substr(1);
            if (hex.find_first_not_of("0123456789abcdef") != std::string::npos) return false;
            if (hex.size() == 3) {
                const int v = (int)std::strtol(hex.c_str(), NULL, 16);
                const int r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
                ink.color = (r * 17) << 16 | (g * 17) << 8 | (b * 17);
                return true;
            }
            if (hex.size() == 6) {
                ink.color = (int)std::strtol(hex.c_str(), NULL, 16);
                return true;
            }
            return false;
        }
        if (value.rfind("rgb", 0) == 0) {
            const size_t open = value.find('('), close = value.find(')');
            if (open == std::string::npos || close == std::string::npos || close < open) return false;
            std::string inner = value.substr(open + 1, close - open - 1);
            std::replace(inner.begin(), inner.end(), ',', ' ');
            std::vector<std::string> parts = SplitColors(inner);
            if (parts.size() < 3) return false;
            int rgb[3];
            for (int i = 0; i < 3; ++i) rgb[i] = std::clamp(std::atoi(parts[i].c_str()), 0, 255);
            ink.color = rgb[0] << 16 | rgb[1] << 8 | rgb[2];
            if (parts.size() >= 4) ink.opacity = (float)std::clamp(std::atof(parts[3].c_str()), 0.0, 1.0);
            return true;
        }
        static const std::vector<std::pair<std::string, int>> names
            = { { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 },
                  { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "gray", 0x808080 }, { "grey", 0x808080 },
                  { "silver", 0xC0C0C0 }, { "brown", 0xA52A2A }, { "orange", 0xFFA500 }, { "gold", 0xFFD700 } };
        for (const auto &name : names) {
            if (name.first == value) {
                ink.color = name.second;
                return true;
            }
        }
        return false;
    }

    data_PITCHNAME PitchNameOf(char letter)
    {
        switch (std::tolower((unsigned char)letter)) {
            case 'c': return PITCHNAME_c;
            case 'd': return PITCHNAME_d;
            case 'e': return PITCHNAME_e;
            case 'f': return PITCHNAME_f;
            case 'g': return PITCHNAME_g;
            case 'a': return PITCHNAME_a;
            case 'b': return PITCHNAME_b;
            default: return PITCHNAME_NONE;
        }
    }

} // namespace

bool View::DrawNeumeStaffLinesColored(DeviceContext *dc, Staff *staff, StaffDef *staffDef, Measure *measure)
{
    assert(dc);
    assert(staff);
    assert(staffDef);
    assert(measure);

    if (!m_options->m_neumeCalligraphic.GetValue() || staff->m_drawingNotationType != NOTATIONTYPE_neume) return false;
    if (!staffDef->HasLinesColor()) return false;
    const std::vector<std::string> colors = SplitColors(staffDef->GetLinesColor());
    if (colors.empty()) return false;

    // The stock geometry of DrawStaffLines (straight or skewed), each line with its own pen.
    int x1 = measure->GetDrawingX();
    const int x2 = x1 + measure->GetWidth();
    if (staff->IsOssia()) x1 += staff->GetOssiaDrawingShift(measure, m_doc);
    int y1 = staff->GetDrawingY();
    int y2 = y1;
    if (staff->HasDrawingRotation()) {
        y2 = y1 - measure->GetWidth() * tan(staff->GetDrawingRotation() * M_PI / 180.0);
    }
    const int lineWidth = m_doc->GetDrawingStaffLineWidth(staff->m_drawingStaffSize);
    const int interline = m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize);

    for (int j = 0; j < staff->m_drawingLines; ++j) {
        // Lines are drawn top down; @lines.color lists them from the lowest up (line n is clef/@line n).
        const int lineN = staff->m_drawingLines - j;
        InkColor ink;
        if (colors.size() == 1) {
            ParseColor(colors.front(), ink);
        }
        else if (lineN <= (int)colors.size()) {
            ParseColor(colors.at(lineN - 1), ink);
        }
        if (ink.draw) {
            // A coloured line gets its own group carrying the colour: the page's CSS strokes every path in
            // currentColor, which an explicit stroke on the path itself does not override.
            if (ink.color != COLOR_NONE) {
                dc->StartCustomGraphic("staffLine");
                dc->SetCustomGraphicColor(StringFormat("#%06X", ink.color));
            }
            dc->SetPen(std::max(1, this->ToDeviceContextX(lineWidth)), PEN_SOLID, 0, 0, LINECAP_DEFAULT,
                LINEJOIN_DEFAULT, ink.opacity, ink.color);
            dc->DrawLine(this->ToDeviceContextX(x1), this->ToDeviceContextY(y1), this->ToDeviceContextX(x2),
                this->ToDeviceContextY(y2));
            dc->ResetPen();
            if (ink.color != COLOR_NONE) dc->EndCustomGraphic();
        }
        y1 -= interline;
        y2 -= interline;
    }
    return true;
}

void View::DrawNeumePitchMarkers(DeviceContext *dc, Staff *staff, StaffDef *staffDef, Measure *measure)
{
    assert(dc);
    assert(staff);
    assert(measure);

    if (!m_options->m_neumeCalligraphic.GetValue() || !m_options->m_neumePitchMarkers.GetValue()) return;
    if (staff->m_drawingNotationType != NOTATIONTYPE_neume || !staffDef) return;
    // Encoded line colours are the source's own: they win over the hand's markers.
    if (staffDef->HasLinesColor()) return;
    if (staff->HasDrawingRotation() || (staff->m_drawingLines < 1)) return;
    const CalligraphicNeume::Hand &hand = CalligraphicNeume::HandFor(this->GetNeumeScript(staff));
    if (hand.pitchMarkers.empty()) return;
    const Layer *layer = vrv_cast<const Layer *>(staff->FindDescendantByType(LAYER, 1));
    if (!layer) return;

    // A coloured marker names a pitch, not a line: the red f of the Hildegard books runs through the f space
    // when the clef puts f there and moves with the clef (hildegard-07, -11), and starts together with a new
    // clef letter mid-system (hildegard-04); on a Guidonian staff the same red F and yellow c fall on lines
    // (cistercian-03, hufnagel-06). So the staff is cut into segments at each clef, and each segment draws
    // every marker of the hand at the height its clef gives the marker's pitch: on the line that carries it,
    // or through the middle of its space. A pitch class the staff holds twice (an octave apart on five lines)
    // is marked once, on its line, as the Guidonian F and c are; one outside the staff is not marked.
    struct Segment {
        int x;
        const Clef *clef;
    };
    std::vector<Segment> segments;
    const int x1 = measure->GetDrawingX();
    const int x2 = x1 + measure->GetWidth();
    // The clef in effect where the lines start keys the staff up to the first clef of the layer: the clef drawn
    // at the start of the system, or the one carried over from an earlier measure of it (a page of neumes is
    // cast into several measures per system). When a clef of the layer opens the lines, before any neume, it
    // reaches back to the start of the lines itself (the first system, whose clef is in the layer).
    ListOfConstObjects clefs;
    for (const Object *object : layer->FindAllDescendantsByType(CLEF)) {
        if (!vrv_cast<const Clef *>(object)->m_crossStaff) clefs.push_back(object);
    }
    const Object *firstNeume = layer->FindDescendantByType(NEUME);
    const bool clefOpens = !clefs.empty()
        && (!firstNeume || (clefs.front()->GetDrawingX() <= vrv_cast<const LayerElement *>(firstNeume)->GetDrawingX()));
    const Clef *leading = layer->GetStaffDefClef() ? layer->GetStaffDefClef() : layer->GetCurrentClef();
    if (leading && !(clefOpens && !layer->GetStaffDefClef())) segments.push_back({ x1, leading });
    for (const Object *object : clefs) {
        segments.push_back({ segments.empty() ? x1 : object->GetDrawingX(), vrv_cast<const Clef *>(object) });
    }
    if (segments.empty()) return;

    const int top = (staff->m_drawingLines - 1) * 2; // the loc of the top line (the bottom line is 0)
    const int width = std::max(1, this->ToDeviceContextX(m_doc->GetDrawingStaffLineWidth(staff->m_drawingStaffSize)));
    for (size_t i = 0; i < segments.size(); ++i) {
        const int start = std::max(x1, segments[i].x);
        const int end = (i + 1 < segments.size()) ? std::min(x2, segments[i + 1].x) : x2;
        if (end <= start) continue;
        const int clefLocOffset = segments[i].clef->GetClefLocOffset();
        for (const auto &marker : hand.pitchMarkers) {
            const data_PITCHNAME pname = PitchNameOf(marker.first);
            if (pname == PITCHNAME_NONE) continue;
            int loc = VRV_UNSET;
            for (int oct = -1; oct <= 10; ++oct) {
                const int candidate = PitchInterface::CalcLoc(pname, oct, clefLocOffset);
                if ((candidate < 0) || (candidate > top)) continue;
                if ((loc == VRV_UNSET) || ((candidate % 2 == 0) && (loc % 2 != 0))) loc = candidate;
            }
            if (loc == VRV_UNSET) continue;
            InkColor ink;
            if (!ParseColor(marker.second, ink) || !ink.draw) continue;
            const int y = staff->GetDrawingY() + staff->CalcPitchPosYRel(m_doc, loc);

            // Its own group carrying the colour (see DrawNeumeStaffLinesColored) and the pitch it marks.
            dc->StartCustomGraphic("pitchMarker");
            if (ink.color != COLOR_NONE) dc->SetCustomGraphicColor(StringFormat("#%06X", ink.color));
            dc->SetCustomGraphicAttributes("pitch", std::string(1, (char)std::tolower((unsigned char)marker.first)));
            // Half again as broad as a staff line, so a marker on a line covers it in its colour.
            dc->SetPen(width * 3 / 2, PEN_SOLID, 0, 0, LINECAP_DEFAULT, LINEJOIN_DEFAULT, ink.opacity, ink.color);
            dc->DrawLine(this->ToDeviceContextX(start), this->ToDeviceContextY(y), this->ToDeviceContextX(end),
                this->ToDeviceContextY(y));
            dc->ResetPen();
            dc->EndCustomGraphic();
        }
    }
}

} // namespace vrv
