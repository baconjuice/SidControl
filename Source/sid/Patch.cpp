#include "Patch.h"
#include "InitPatches.h"
#include "Params.h"

#include <algorithm>
#include <cstring>

namespace sid
{
const char* engineName (Engine e)
{
    switch (e)
    {
        case Engine::Lead:     return "Lead";
        case Engine::Bassline: return "Bassline";
        case Engine::Drum:     return "Drum";
        case Engine::Multi:    return "Multi";
    }
    return "?";
}

char engineLetter (Engine e)
{
    static const char letters[] = { 'L', 'B', 'D', 'M' };
    return letters[(int) e & 3];
}

Patch Patch::makeInit (Engine engine)
{
    Patch p;
    const uint8_t* src = kInit_lead;
    switch (engine)
    {
        case Engine::Lead:     src = kInit_lead; break;
        case Engine::Bassline: src = kInit_bassline; break;
        case Engine::Drum:     src = kInit_drum; break;
        case Engine::Multi:    src = kInit_multi; break;
    }
    std::memcpy (p.data.data(), src, kPatchSize);
    return p;
}

std::string Patch::name() const
{
    std::string s;
    for (int i = 0; i < kNameLength; ++i)
    {
        auto c = (char) data[(size_t) i];
        s += (c >= 0x20 && c < 0x7f) ? c : ' ';
    }
    while (! s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

void Patch::setName (std::string_view newName)
{
    for (int i = 0; i < kNameLength; ++i)
    {
        char c = i < (int) newName.size() ? newName[(size_t) i] : ' ';
        if (c < 0x20 || c >= 0x7f) c = ' ';
        data[(size_t) i] = (uint8_t) c;
    }
}

int Patch::getRaw (const ParamDef& p) const
{
    switch (p.kind)
    {
        case ParamDef::Kind::Word12:
            return byte (p.addr) | ((byte (p.addr + 1) & 0x0f) << 8);
        case ParamDef::Kind::Ext12:
            return (byte (p.addr) >> 4) | (byte (p.addr + 1) << 4);
        default:
        {
            const int mask = (1 << p.bits) - 1;
            return (byte (p.addr) >> p.shift) & mask;
        }
    }
}

std::vector<uint16_t> Patch::setRaw (const ParamDef& p, int raw)
{
    raw = std::clamp (raw, 0, p.maxRaw());
    std::vector<uint16_t> changed;

    auto put = [&] (int addr, uint8_t v)
    {
        if (byte (addr) != v)
        {
            setByte (addr, v);
            changed.push_back ((uint16_t) addr);
        }
    };

    switch (p.kind)
    {
        case ParamDef::Kind::Word12:
            put (p.addr, (uint8_t) (raw & 0xff));
            put (p.addr + 1, (uint8_t) ((byte (p.addr + 1) & 0xf0) | ((raw >> 8) & 0x0f)));
            break;
        case ParamDef::Kind::Ext12:
            put (p.addr, (uint8_t) ((byte (p.addr) & 0x0f) | ((raw & 0x0f) << 4)));
            put (p.addr + 1, (uint8_t) ((raw >> 4) & 0xff));
            break;
        default:
        {
            const int mask = ((1 << p.bits) - 1) << p.shift;
            put (p.addr, (uint8_t) ((byte (p.addr) & ~mask) | ((raw << p.shift) & mask)));
            break;
        }
    }
    return changed;
}
} // namespace sid
