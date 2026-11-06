#pragma once

// MIDIbox SID V2 SysEx protocol: builds outgoing messages and parses replies.
// All messages built here are complete (F0 ... F7).

#include "Patch.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sid::sysex
{
using Bytes = std::vector<uint8_t>;

// Header: F0 00 00 7E 4B <device>
constexpr uint8_t kHeader[] = { 0xf0, 0x00, 0x00, 0x7e, 0x4b };

namespace cmd
{
    constexpr uint8_t Read = 0x01, Write = 0x02, BankRead = 0x03, ParWrite = 0x06,
                      Special = 0x0c, Error = 0x0e, Ack = 0x0f, ExtendedFlag = 0x10;
}

namespace type
{
    constexpr uint8_t Patch = 0x00, EditBuffer = 0x08, Ensemble = 0x70, EnsembleEdit = 0x78;
}

// Write options for parameter writes ("Direct Write Options" in the spec).
namespace wopt
{
    constexpr uint8_t None = 0, LeftRight = 1, Voices = 2, All = 3;
}

uint8_t checksum (const uint8_t* nibbles, size_t count); // 2s complement, 7 bit

// Addresses (other than addr) the firmware also writes when a parameter write
// uses <writeOption> on the given engine. Empty when the option doesn't apply.
std::vector<uint16_t> linkedAddresses (Engine engine, uint16_t addr, uint8_t writeOption);

Bytes requestPatch (uint8_t device, uint8_t bank, uint8_t patch);
Bytes requestEditBuffer (uint8_t device);
Bytes requestBank (uint8_t device, uint8_t bank);
Bytes writePatch (uint8_t device, uint8_t bank, uint8_t patch, const Patch& p);
Bytes writeEditBuffer (uint8_t device, const Patch& p);
Bytes parameterWrite (uint8_t device, uint16_t addr, uint8_t value, uint8_t writeOption = wopt::None);
Bytes selectSids (uint8_t device, uint8_t mask);
Bytes allNotesOff (uint8_t device);
Bytes playPatch (uint8_t device, int instrument = -1);
Bytes storeEditBuffer (uint8_t device, int sidIndex); // sidIndex 0..3
Bytes ping (uint8_t device);

struct Message
{
    enum class Kind { PatchDump, Ack, Error, Other };

    Kind kind = Kind::Other;
    uint8_t device = 0;
    uint8_t command = 0;
    uint8_t dumpType = 0;  // PatchDump: type::Patch or type::EditBuffer
    uint8_t bank = 0;
    uint8_t patchNumber = 0;
    bool checksumOk = false;
    Patch patch;           // PatchDump
    uint8_t value = 0;     // Ack: <sids> flags, Error: error code
};

// Accepts a SysEx message with or without the surrounding F0/F7.
// Returns nothing if it isn't a MIDIbox SID message.
std::optional<Message> parse (const uint8_t* data, size_t size);

const char* errorText (uint8_t code);
} // namespace sid::sysex
