/////////////////////////////////////////////////////////////////////////////
// Name:        adjustneumexfunctor.cpp
// Author:      Laurent Pugin
// Created:     2024
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "adjustneumexfunctor.h"

//----------------------------------------------------------------------------

#include "doc.h"
#include "layer.h"
#include "neume.h"
#include "score.h"
#include "staff.h"
#include "syl.h"
#include "syllable.h"

//----------------------------------------------------------------------------

namespace vrv {

//----------------------------------------------------------------------------
// AdjustNeumeXFunctor
//----------------------------------------------------------------------------

AdjustNeumeXFunctor::AdjustNeumeXFunctor(Doc *doc) : DocFunctor(doc)
{
    m_minPos = VRV_UNSET;
    m_neumeMinPos = VRV_UNSET;
    m_firstNeumeInSyllable = false;
}

FunctorCode AdjustNeumeXFunctor::VisitLayer(Layer *layer)
{
    m_minPos = VRV_UNSET;
    m_neumeMinPos = VRV_UNSET;
    m_firstNeumeInSyllable = false;

    return FUNCTOR_CONTINUE;
}

FunctorCode AdjustNeumeXFunctor::VisitLayerEnd(Layer *layer)
{
    // Alignment *alignment = m_rightBarline->GetAlignment();
    Measure *measure = vrv_cast<Measure *>(layer->GetFirstAncestor(MEASURE));
    assert(measure);
    Alignment *alignment = measure->m_measureAligner.GetRightAlignment();
    assert(alignment);

    int selfLeft = alignment->GetXRel();
    if (selfLeft < m_minPos) {
        const int adjust = m_minPos - selfLeft;
        alignment->SetXRel(alignment->GetXRel() + adjust);
    }

    m_minPos = VRV_UNSET;

    return FUNCTOR_CONTINUE;
}

FunctorCode AdjustNeumeXFunctor::VisitNeume(Neume *neume)
{
    // It is VRV_UNSET when we process the first neume of the layer
    if (m_neumeMinPos != VRV_UNSET) {
        Alignment *alignment = neume->GetAlignment();

        int selfLeft = neume->GetContentLeft();
        if (selfLeft < m_neumeMinPos) {
            const int adjust = m_neumeMinPos - selfLeft;
            alignment->SetXRel(alignment->GetXRel() + adjust);
            // The first neume of a syllable shares its alignment with the syl text, so pushing the
            // neume has also moved the text - keep the min position for the next syl in sync
            if (m_firstNeumeInSyllable && (m_minPos != VRV_UNSET)) m_minPos += adjust;
        }
    }
    else if (m_firstNeumeInSyllable && (neume->GetContentLeft() < 0)) {
        // This measure (= syllable, in the calligraphic cast-off) is placed immediately after the
        // previous one ends, with no additional gap - the previous measure's own sizing (above) is
        // what clears its trailing ink. A neume normally starts at or after its own measure's local
        // origin (0), but a @place="left" / "above-left" / "below-left" signifLet on the very first
        // component can draw ink reaching out further left than that origin. Since there is no
        // previous neume in this measure to compare against (m_neumeMinPos is unset here), that
        // overhang would otherwise go unchecked and bleed back into the previous syllable's ink. Pull
        // the shared syl/neume alignment right so this measure's own content never starts before 0.
        Alignment *alignment = neume->GetAlignment();
        alignment->SetXRel(alignment->GetXRel() - neume->GetContentLeft());
    }
    m_firstNeumeInSyllable = false;

    // Gap to the next neume. Classic glyph neumes are spaced a full unit apart; the calligraphic
    // (broad-nib) rendering reads as flowing handwriting, so its neumes are set much closer together
    // to keep long melismas compact.
    const int neumeGap = m_doc->GetOptions()->m_neumeCalligraphic.GetValue() ? m_doc->GetDrawingUnit(100) / 3
                                                                             : m_doc->GetDrawingUnit(100);
    m_neumeMinPos = neume->GetContentRight() + neumeGap;

    // Check if the neume takes more space the the syllable text
    if (m_neumeMinPos > m_minPos) m_minPos = m_neumeMinPos;

    return FUNCTOR_CONTINUE;
}

FunctorCode AdjustNeumeXFunctor::VisitStaff(Staff *staff)
{
    if (!staff->IsNeume()) return FUNCTOR_SIBLINGS;

    return FUNCTOR_CONTINUE;
}

FunctorCode AdjustNeumeXFunctor::VisitSyl(Syl *syl)
{
    Alignment *alignment = syl->GetAlignment();

    // The next neume starts a new syllable; m_neumeMinPos is kept from the previous neume so that
    // the first neume of this syllable cannot overlap it either (its ink can reach left of the
    // shared alignment point in the calligraphic rendering)
    m_firstNeumeInSyllable = true;

    int selfLeft = syl->GetContentLeft();
    if (selfLeft < m_minPos) {
        const int adjust = m_minPos - selfLeft;
        alignment->SetXRel(alignment->GetXRel() + adjust);
    }

    m_minPos = syl->GetContentRight() + m_doc->GetDrawingUnit(100);

    return FUNCTOR_CONTINUE;
}

} // namespace vrv
