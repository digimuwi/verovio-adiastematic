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

AdjustNeumeXFunctor::AdjustNeumeXFunctor(Doc *doc) : DocFunctor(doc) {}

FunctorCode AdjustNeumeXFunctor::VisitLayer(Layer *layer)
{
    m_minPos = VRV_UNSET;

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
    // It is 0 when we process the first neume of the syllable
    if (m_neumeMinPos != VRV_UNSET) {
        Alignment *alignment = neume->GetAlignment();

        int selfLeft = neume->GetContentLeft();
        if (selfLeft < m_neumeMinPos) {
            const int adjust = m_neumeMinPos - selfLeft;
            alignment->SetXRel(alignment->GetXRel() + adjust);
        }
    }

    // Gap to the next neume. Classic glyph neumes are spaced a full unit apart; the calligraphic
    // (broad-nib) rendering reads as flowing handwriting, so its neumes are set much closer together
    // to keep long melismas compact.
    const int neumeGap = m_doc->GetOptions()->m_neumeCalligraphic.GetValue()
        ? m_doc->GetDrawingUnit(100) / 3
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

    // Indicates that the neume will be the first of the syllable
    m_neumeMinPos = VRV_UNSET;

    int selfLeft = syl->GetContentLeft();
    if (selfLeft < m_minPos) {
        const int adjust = m_minPos - selfLeft;
        alignment->SetXRel(alignment->GetXRel() + adjust);
    }

    m_minPos = syl->GetContentRight() + m_doc->GetDrawingUnit(100);

    return FUNCTOR_CONTINUE;
}

} // namespace vrv
