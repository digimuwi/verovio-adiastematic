/////////////////////////////////////////////////////////////////////////////
// Name:        signiflet.cpp
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "signiflet.h"

//----------------------------------------------------------------------------

#include <cassert>

//----------------------------------------------------------------------------

#include "editorial.h"
#include "text.h"
#include "vrv.h"

namespace vrv {

//----------------------------------------------------------------------------
// SignifLet
//----------------------------------------------------------------------------

static const ClassRegistrar<SignifLet> s_factory("signifLet", SIGNIFLET);

SignifLet::SignifLet() : LayerElement(SIGNIFLET), TextListInterface(), AttColor(), AttSignifLetVis(), AttTypography()
{
    this->RegisterAttClass(ATT_COLOR);
    this->RegisterAttClass(ATT_SIGNIFLETVIS);
    this->RegisterAttClass(ATT_TYPOGRAPHY);

    this->Reset();
}

SignifLet::~SignifLet() {}

void SignifLet::Reset()
{
    LayerElement::Reset();
    this->ResetColor();
    this->ResetSignifLetVis();
    this->ResetTypography();
}

bool SignifLet::IsSupportedChild(ClassId classId)
{
    static const std::vector<ClassId> supported{ REND, TEXT };

    if (std::find(supported.begin(), supported.end(), classId) != supported.end()) {
        return true;
    }
    else if (Object::IsEditorialElement(classId)) {
        return true;
    }
    else {
        return false;
    }
}

} // namespace vrv
