#pragma once

// Patch randomizer, modelled on the MIDIbox SID V2 control surface's random
// generator (sid_rnd.inc): the same sections per engine and the same rules
// that keep results playable (transpose within an octave or two, cutoff in
// the lower quarter, no voice delay, arpeggiator off, ...).

#include "Patch.h"

#include <random>
#include <string>
#include <vector>

namespace sid
{
struct RandomSection
{
    std::string id;    // "all", "osc", "fil", ...
    std::string label; // shown on the button
    std::string help;  // tooltip
};

// Sections offered for an engine, "all" first.
const std::vector<RandomSection>& randomSections (Engine e);

// Randomize one section of the patch. amount 1 = completely new values,
// smaller values only move each parameter part of the way (0.25 = a gentle
// variation of the current sound).
void randomize (Patch& patch, const std::string& section, float amount, std::mt19937& rng);
} // namespace sid
