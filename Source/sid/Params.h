#pragma once

// Parameter definitions: where each editable value lives in the 512-byte patch
// and how to show it. The tables are built from the V2 SysEx spec.

#include "Patch.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace sid
{
struct ParamDef
{
    enum class Kind : uint8_t
    {
        Int,     // bit field inside one byte
        Bool,    // single bit
        Choice,  // bit field with named values
        Word12,  // 12 bit: low byte at addr, high nibble in bits [3:0] of addr+1
        Ext12,   // 12 bit: low nibble in bits [7:4] of addr, high byte at addr+1
    };

    std::string id;    // stable key, e.g. "v1.attack"
    std::string name;  // short label, e.g. "Attack"
    uint16_t addr = 0;
    uint8_t shift = 0; // bit position (Int/Bool/Choice)
    uint8_t bits = 8;  // field width (Int/Bool/Choice)
    Kind kind = Kind::Int;
    int offset = 0;    // displayed value = raw - offset
    std::vector<std::string> choices;

    int maxRaw() const
    {
        if (kind == Kind::Word12 || kind == Kind::Ext12) return 4095;
        return (1 << bits) - 1;
    }
    int minDisplay() const { return -offset; }
    int maxDisplay() const { return maxRaw() - offset; }

    // Bytes this parameter occupies (1 or 2).
    std::vector<uint16_t> addresses() const
    {
        if (kind == Kind::Word12 || kind == Kind::Ext12)
            return { addr, (uint16_t) (addr + 1) };
        return { addr };
    }
};

class ParamTable
{
public:
    void add (ParamDef d);
    const ParamDef* find (const std::string& id) const;
    const ParamDef& get (const std::string& id) const; // throws if missing
    const std::vector<ParamDef>& all() const { return defs; }

private:
    std::vector<ParamDef> defs;
    std::unordered_map<std::string, size_t> lookup;
};

// Parameters shared by every engine (0x000..0x05f) plus the full Lead engine.
const ParamTable& leadParams();

// Name of an assignable parameter number (0..255) for knobs, wavetables,
// velocity etc. From mbsidv2_parameter_chart.txt. May be empty.
const char* parameterNumberName (Engine e, int number);

// Choice lists shared by several editors.
const std::vector<std::string>& lfoWaveforms();
const std::vector<std::string>& modSources();
const std::vector<std::string>& modTargets();
const std::vector<std::string>& modOperators();
const std::vector<std::string>& envLoopPoints();
const std::vector<std::string>& arpDirections();
const std::vector<std::string>& lfoClockRates(); // CSn rate names, index = raw/8
} // namespace sid
