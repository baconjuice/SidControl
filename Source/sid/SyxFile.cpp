#include "SyxFile.h"

namespace sid::syx
{
ReadResult read (const uint8_t* data, size_t size)
{
    ReadResult r;
    size_t i = 0;
    while (i < size)
    {
        if (data[i] != 0xf0) { ++i; continue; }

        size_t end = i + 1;
        while (end < size && data[end] != 0xf7 && data[end] != 0xf0)
            ++end;
        if (end >= size || data[end] != 0xf7)
        {
            ++r.otherMessages; // truncated message
            i = end;
            continue;
        }

        auto msg = sysex::parse (data + i, end - i + 1);
        if (msg && msg->kind == sysex::Message::Kind::PatchDump)
        {
            Entry e;
            e.bank = msg->bank;
            e.patchNumber = msg->patchNumber;
            e.editBuffer = (msg->dumpType & sysex::type::EditBuffer) != 0;
            e.checksumOk = msg->checksumOk;
            e.patch = msg->patch;
            r.patches.push_back (e);
        }
        else
        {
            ++r.otherMessages;
        }
        i = end + 1;
    }
    return r;
}

sysex::Bytes write (const std::vector<Entry>& entries, uint8_t device)
{
    sysex::Bytes out;
    for (const auto& e : entries)
    {
        auto m = e.editBuffer ? sysex::writeEditBuffer (device, e.patch)
                              : sysex::writePatch (device, e.bank, e.patchNumber, e.patch);
        out.insert (out.end(), m.begin(), m.end());
    }
    return out;
}
} // namespace sid::syx
