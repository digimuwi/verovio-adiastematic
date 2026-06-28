/////////////////////////////////////////////////////////////////////////////
// Name:        signiflet.h
// Author:      Niels Pfeffer
// Created:     2026
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#ifndef __VRV_SIGNIFLET_H__
#define __VRV_SIGNIFLET_H__

#include "atts_shared.h"
#include "atts_visual.h"
#include "layerelement.h"

namespace vrv {

//----------------------------------------------------------------------------
// SignifLet
//----------------------------------------------------------------------------

/**
 * This class models the MEI <signifLet> element, a significative letter (littera significativa)
 * attached to a neume. It is a child of <neume> and holds the letter glyph(s) as text content,
 * positioned relative to the neume through its @place attribute.
 */
class SignifLet : public LayerElement,
                  public TextListInterface,
                  public AttColor,
                  public AttSignifLetVis,
                  public AttTypography {
public:
    /**
     * @name Constructors, destructors, and other standard methods
     * Reset method resets all attribute classes
     */
    ///@{
    SignifLet();
    virtual ~SignifLet();
    Object *Clone() const override { return new SignifLet(*this); }
    void Reset() override;
    std::string GetClassName() const override { return "signifLet"; }
    ///@}

    /**
     * Add a text or rend child (the letter glyph) to a signifLet.
     * Only supported elements will be actually added to the child list.
     */
    bool IsSupportedChild(ClassId classId) override;

private:
    //
public:
    //
private:
};

} // namespace vrv

#endif // __VRV_SIGNIFLET_H__
