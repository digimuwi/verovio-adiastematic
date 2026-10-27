/////////////////////////////////////////////////////////////////////////////
// Name:        calligraphicpoint.cpp
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "calligraphicneume.h"

//----------------------------------------------------------------------------

namespace vrv {

//----------------------------------------------------------------------------
// Point scripts: every <nc> its own mark at its own pitch
//----------------------------------------------------------------------------

CalligraphicNeume::NeumeGeometry CalligraphicNeume::BuildPoint(
    const std::vector<NcInfo> &ncs, Slant slant, const Pen &pen, const Gesture &gesture, const Hand &hand)
{
    (void)pen;
    (void)gesture;
    // Not drawn separately yet: a point script falls back to the stroke builder.
    return BuildStroke(ncs, slant, hand);
}

} // namespace vrv
