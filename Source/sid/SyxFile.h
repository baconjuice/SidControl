#pragma once

// Reading and writing .syx files containing MIDIbox SID V2 patch dumps.

#include "SysEx.h"

#include <vector>

namespace sid::syx
{
struct Entry
{
    uint8_t bank = 0;
    uint8_t patchNumber = 0;
    bool editBuffer = false;  // dump was addressed to the edit buffer
    bool checksumOk = false;
    Patch patch;
};

struct EnsembleEntry
{
    uint8_t number = 0;
    bool editBuffer = false;
    bool checksumOk = false;
    Ensemble ensemble;
};

struct ReadResult
{
    std::vector<Entry> patches;
    std::vector<EnsembleEntry> ensembles;
    int otherMessages = 0;   // SysEx messages that weren't SID V2 patch dumps
};

// Splits raw file bytes into SysEx messages and decodes the patch dumps.
ReadResult read (const uint8_t* data, size_t size);

// One write message per patch, addressed to <bank>/<number> as given.
sysex::Bytes write (const std::vector<Entry>& entries, uint8_t device = 0);
sysex::Bytes writeEnsembles (const std::vector<EnsembleEntry>& entries, uint8_t device = 0);
} // namespace sid::syx
