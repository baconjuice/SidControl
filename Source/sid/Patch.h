#pragma once

// MIDIbox SID V2 patch model: a 512-byte image of one patch exactly as the
// firmware stores it (see mbsidv2_sysex_implementation.txt, "Patch Structure").
// Plain C++ with no JUCE dependency, so it can be unit tested.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sid
{
enum class Engine : uint8_t { Lead = 0, Bassline = 1, Drum = 2, Multi = 3 };

constexpr int kPatchSize = 512;
constexpr int kEnsembleSize = 256;
constexpr int kNameLength = 16;
constexpr int kEngineAddr = 0x010;

const char* engineName (Engine e);
char engineLetter (Engine e); // L, B, D, M

struct ParamDef;

// Bit-field access shared by patches and ensembles.
int getRawValue (const uint8_t* data, size_t size, const ParamDef& p);
std::vector<uint16_t> setRawValue (uint8_t* data, size_t size, const ParamDef& p, int raw);

class Patch
{
public:
    Patch() = default;
    static Patch makeInit (Engine engine);

    Engine engine() const { return static_cast<Engine> (data[kEngineAddr] & 0x03); }

    // Name is 16 ASCII chars, space padded. Returned right-trimmed.
    std::string name() const;
    void setName (std::string_view newName);

    uint8_t byte (int addr) const { return data[(size_t) (addr & 0x1ff)]; }
    void setByte (int addr, uint8_t v) { data[(size_t) (addr & 0x1ff)] = v; }

    // Raw (stored) value of a parameter, before display offset is applied.
    int getRaw (const ParamDef& p) const { return getRawValue (data.data(), data.size(), p); }

    // Writes a raw value, clamped to the parameter's range, preserving the
    // other bits of the touched bytes. Returns the byte addresses changed.
    std::vector<uint16_t> setRaw (const ParamDef& p, int raw) { return setRawValue (data.data(), data.size(), p, raw); }

    bool operator== (const Patch& o) const { return data == o.data; }
    bool operator!= (const Patch& o) const { return data != o.data; }

    std::array<uint8_t, kPatchSize> data {};
};

// Ensemble: the setup of up to 4 SID cores (patch per core, clock, MIDI
// channels and keyboard splits of each instrument, filter calibration).
class Ensemble
{
public:
    uint8_t byte (int addr) const { return data[(size_t) (addr & 0xff)]; }
    void setByte (int addr, uint8_t v) { data[(size_t) (addr & 0xff)] = v; }
    int getRaw (const ParamDef& p) const { return getRawValue (data.data(), data.size(), p); }
    std::vector<uint16_t> setRaw (const ParamDef& p, int raw) { return setRawValue (data.data(), data.size(), p, raw); }
    bool operator== (const Ensemble& o) const { return data == o.data; }
    bool operator!= (const Ensemble& o) const { return data != o.data; }

    std::array<uint8_t, kEnsembleSize> data {};
};
} // namespace sid
