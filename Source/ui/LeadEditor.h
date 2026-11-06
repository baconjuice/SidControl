#pragma once

#include "EditorCommon.h"
#include "model/Session.h"

namespace ui
{
// Full editor for the Lead engine, organised in tabs.
class LeadEditor : public EngineEditor
{
public:
    explicit LeadEditor (Session& s);
};
} // namespace ui
