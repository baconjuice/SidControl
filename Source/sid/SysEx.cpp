#include "SysEx.h"

namespace sid::sysex
{
namespace
{
    Bytes header (uint8_t device, uint8_t command)
    {
        Bytes b (std::begin (kHeader), std::end (kHeader));
        b.push_back (device & 0x7f);
        b.push_back (command & 0x7f);
        return b;
    }

    void appendPatchNibbles (Bytes& b, const Patch& p)
    {
        const size_t start = b.size();
        for (auto v : p.data)
        {
            b.push_back (v & 0x0f);        // low nibble first
            b.push_back ((v >> 4) & 0x0f);
        }
        b.push_back (checksum (b.data() + start, b.size() - start));
    }
} // namespace

uint8_t checksum (const uint8_t* nibbles, size_t count)
{
    unsigned sum = 0;
    for (size_t i = 0; i < count; ++i)
        sum += nibbles[i];
    return (uint8_t) (-(int) sum & 0x7f);
}

std::vector<uint16_t> linkedAddresses (Engine engine, uint16_t addr, uint8_t writeOption)
{
    std::vector<uint16_t> out;
    const bool lr = (writeOption & wopt::LeftRight) != 0;
    const bool voices = (writeOption & wopt::Voices) != 0;
    if (writeOption == wopt::None)
        return out;

    // All engines, L/R only: Ext pairs 1/2, 3/4, 5/6, 7/8 and the two filters.
    if (lr && addr >= 0x40 && addr <= 0x4f)
    {
        const int within = (addr - 0x40) % 4;
        out.push_back ((uint16_t) (within < 2 ? addr + 2 : addr - 2));
        return out;
    }
    if (lr && addr >= 0x54 && addr <= 0x5f)
    {
        out.push_back ((uint16_t) (addr < 0x5a ? addr + 6 : addr - 6));
        return out;
    }

    // Lead (6 voices x 0x10) and Multi (6 instruments x 0x30): voices 1-3 are
    // the left SID, 4-6 the right SID.
    auto voiceLinks = [&] (int start, int size)
    {
        const int end = start + 6 * size;
        if (addr < start || addr >= end)
            return;
        const int v = (addr - start) / size;
        const int off = (addr - start) % size;
        for (int other = 0; other < 6; ++other)
        {
            if (other == v) continue;
            const bool sameSide = (other / 3) == (v / 3);
            const bool partner = other == (v + 3) % 6;
            if ((lr && voices) || (lr && partner) || (voices && sameSide))
                out.push_back ((uint16_t) (start + other * size + off));
        }
    };

    switch (engine)
    {
        case Engine::Lead:  voiceLinks (0x60, 0x10); break;
        case Engine::Multi: voiceLinks (0x60, 0x30); break;
        case Engine::Bassline:
            if (lr && addr >= 0x60 && addr <= 0xff)
                out.push_back ((uint16_t) (addr < 0xb0 ? addr + 0x50 : addr - 0x50));
            break;
        case Engine::Drum: break;
    }
    return out;
}

Bytes requestPatch (uint8_t device, uint8_t bank, uint8_t patch)
{
    auto b = header (device, cmd::Read);
    b.insert (b.end(), { type::Patch, (uint8_t) (bank & 0x7f), (uint8_t) (patch & 0x7f), 0xf7 });
    return b;
}

Bytes requestEditBuffer (uint8_t device)
{
    auto b = header (device, cmd::Read);
    b.insert (b.end(), { type::EditBuffer, 0x00, 0x00, 0xf7 });
    return b;
}

Bytes requestBank (uint8_t device, uint8_t bank)
{
    auto b = header (device, cmd::BankRead);
    b.insert (b.end(), { type::Patch, (uint8_t) (bank & 0x7f), 0xf7 });
    return b;
}

Bytes writePatch (uint8_t device, uint8_t bank, uint8_t patch, const Patch& p)
{
    auto b = header (device, cmd::Write);
    b.insert (b.end(), { type::Patch, (uint8_t) (bank & 0x7f), (uint8_t) (patch & 0x7f) });
    appendPatchNibbles (b, p);
    b.push_back (0xf7);
    return b;
}

Bytes writeEditBuffer (uint8_t device, const Patch& p)
{
    auto b = header (device, cmd::Write);
    b.insert (b.end(), { type::EditBuffer, 0x00, 0x00 });
    appendPatchNibbles (b, p);
    b.push_back (0xf7);
    return b;
}

Bytes parameterWrite (uint8_t device, uint16_t addr, uint8_t value, uint8_t writeOption)
{
    auto b = header (device, cmd::ParWrite);
    b.insert (b.end(), { (uint8_t) (writeOption & 0x7f),
                         (uint8_t) ((addr >> 7) & 0x03),
                         (uint8_t) (addr & 0x7f),
                         (uint8_t) (value & 0x0f),
                         (uint8_t) ((value >> 4) & 0x0f),
                         0xf7 });
    return b;
}

Bytes selectSids (uint8_t device, uint8_t mask)
{
    auto b = header (device, cmd::Special);
    b.insert (b.end(), { 0x00, (uint8_t) (mask & 0x0f), 0xf7 });
    return b;
}

Bytes allNotesOff (uint8_t device)
{
    auto b = header (device, cmd::Special);
    b.insert (b.end(), { 0x08, 0xf7 });
    return b;
}

Bytes playPatch (uint8_t device, int instrument)
{
    auto b = header (device, cmd::Special);
    b.push_back (0x09);
    if (instrument >= 0)
        b.push_back ((uint8_t) (instrument & 0x7f));
    b.push_back (0xf7);
    return b;
}

Bytes storeEditBuffer (uint8_t device, int sidIndex)
{
    auto b = header (device, cmd::Special);
    b.insert (b.end(), { 0x18, (uint8_t) (0x08 + (sidIndex & 0x03)), 0xf7 });
    return b;
}

Bytes ping (uint8_t device)
{
    auto b = header (device, cmd::Ack);
    b.push_back (0xf7);
    return b;
}

std::optional<Message> parse (const uint8_t* data, size_t size)
{
    // Strip F0 / F7 if present.
    if (size > 0 && data[0] == 0xf0) { ++data; --size; }
    if (size > 0 && data[size - 1] == 0xf7) --size;

    // 00 00 7E 4B <device> <command> ...
    if (size < 6 || data[0] != 0x00 || data[1] != 0x00 || data[2] != 0x7e || data[3] != 0x4b)
        return std::nullopt;

    Message m;
    m.device = data[4];
    m.command = data[5];
    const uint8_t* body = data + 6;
    const size_t bodySize = size - 6;

    const uint8_t baseCmd = m.command & 0x0f;
    const bool extended = (m.command & cmd::ExtendedFlag) != 0;

    if (baseCmd == cmd::Write && bodySize >= 3)
    {
        m.dumpType = body[0];
        m.bank = body[1];
        m.patchNumber = body[2];

        if (m.dumpType == type::Ensemble || m.dumpType == type::EnsembleEdit)
            return m; // ensembles are handled in a later phase (kind Other)

        const size_t copies = extended ? 4 : 1;
        const size_t nibbleCount = 2 * kPatchSize * copies;
        if (bodySize < 3 + nibbleCount + 1)
            return m; // truncated

        const uint8_t* nib = body + 3;
        m.checksumOk = checksum (nib, nibbleCount) == body[3 + nibbleCount];

        auto byteAt = [nib] (size_t i) { return (uint8_t) ((nib[2 * i] & 0x0f) | ((nib[2 * i + 1] & 0x0f) << 4)); };

        // Extended dumps carry one copy per engine; pick the one matching the
        // engine stored in the first copy. Bytes 0x00..0x5f are shared.
        const size_t engineCopy = extended ? (byteAt (kEngineAddr) & 0x03) : 0;
        for (size_t i = 0; i < (size_t) kPatchSize; ++i)
        {
            const size_t src = (i < 0x60) ? i : engineCopy * kPatchSize + i;
            m.patch.data[i] = byteAt (src);
        }
        m.kind = Message::Kind::PatchDump;
        return m;
    }

    if (baseCmd == cmd::Ack)
    {
        m.kind = Message::Kind::Ack;
        m.value = bodySize > 0 ? body[0] : 0;
        return m;
    }

    if (baseCmd == cmd::Error)
    {
        m.kind = Message::Kind::Error;
        m.value = bodySize > 0 ? body[0] : 0;
        return m;
    }

    return m;
}

const char* errorText (uint8_t code)
{
    switch (code)
    {
        case 0x01: return "received fewer bytes than expected";
        case 0x03: return "wrong checksum";
        case 0x0a: return "BankStick or patch not available";
        case 0x0b: return "parameter not available";
        case 0x0c: return "invalid command";
        case 0x10: return "RAM access not supported";
        case 0x11: return "BankStick too small (32k: only patches 1-64)";
        case 0x12: return "wrong type";
        case 0x13: return "selected SID not available";
        default:   return "unknown error";
    }
}
} // namespace sid::sysex
