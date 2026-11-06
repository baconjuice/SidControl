#pragma once

#include "EditorCommon.h"
#include "model/Session.h"

namespace ui
{
// Editor for the Drum engine: 16 drum instruments and the pattern sequencer.
class DrumEditor : public EngineEditor
{
public:
    explicit DrumEditor (Session& s);
};
} // namespace ui
