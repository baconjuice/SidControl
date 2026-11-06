#pragma once

#include "EditorCommon.h"
#include "model/Session.h"

namespace ui
{
// Editor for the Bassline engine: two basslines (one per SID) and the
// 8 shared step sequences.
class BasslineEditor : public EngineEditor
{
public:
    explicit BasslineEditor (Session& s);
};
} // namespace ui
