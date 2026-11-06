#pragma once

#include "EditorCommon.h"
#include "model/Session.h"

namespace ui
{
// Editor for the Multi engine: 6 instruments with their own LFOs, envelope
// and wavetable sequencer.
class MultiEditor : public EngineEditor
{
public:
    explicit MultiEditor (Session& s);
};
} // namespace ui
