// Core tests: SysEx encode/decode, patch bit fields, .syx files.
// Usage: sid_tests [file.syx ...]   (extra files are parsed and summarised)

#include "sid/Params.h"
#include "sid/SyxFile.h"
#include "sid/SysEx.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace sid;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static void testInitPatches()
{
    CHECK (Patch::makeInit (Engine::Lead).engine() == Engine::Lead);
    CHECK (Patch::makeInit (Engine::Bassline).engine() == Engine::Bassline);
    CHECK (Patch::makeInit (Engine::Drum).engine() == Engine::Drum);
    CHECK (Patch::makeInit (Engine::Multi).engine() == Engine::Multi);
    CHECK (Patch::makeInit (Engine::Lead).name() == "Lead Patch");
}

static void testNames()
{
    Patch p;
    p.setName ("Hello World, this is too long");
    CHECK (p.name() == "Hello World, thi");
    p.setName ("Bass");
    CHECK (p.name() == "Bass");
    CHECK (p.byte (4) == ' ');
}

static void testRoundTrip()
{
    Patch p = Patch::makeInit (Engine::Lead);
    for (int i = 0; i < kPatchSize; ++i)
        p.setByte (i, (uint8_t) (i * 37 + 11));
    p.setName ("RoundTrip");

    auto msg = sysex::writePatch (0, 2, 99, p);
    CHECK (msg.size() == 5 + 1 + 1 + 3 + 1024 + 1 + 1);
    CHECK (msg[6] == sysex::cmd::Write);

    auto parsed = sysex::parse (msg.data(), msg.size());
    CHECK (parsed.has_value());
    CHECK (parsed->kind == sysex::Message::Kind::PatchDump);
    CHECK (parsed->checksumOk);
    CHECK (parsed->bank == 2 && parsed->patchNumber == 99);
    CHECK (parsed->patch == p);

    // Corrupt one nibble: checksum must fail.
    msg[100] ^= 0x01;
    auto bad = sysex::parse (msg.data(), msg.size());
    CHECK (bad && ! bad->checksumOk);

    // Edit buffer
    auto eb = sysex::writeEditBuffer (1, p);
    auto pe = sysex::parse (eb.data(), eb.size());
    CHECK (pe && pe->dumpType == sysex::type::EditBuffer && pe->device == 1 && pe->patch == p);
}

static void testParameterWrite()
{
    auto m = sysex::parameterWrite (0, 0x1a5, 0xc3, sysex::wopt::LeftRight);
    const sysex::Bytes expect { 0xf0, 0x00, 0x00, 0x7e, 0x4b, 0x00, 0x06, 0x01, 0x03, 0x25, 0x03, 0x0c, 0xf7 };
    CHECK (m == expect);

    auto req = sysex::requestPatch (0, 1, 5);
    const sysex::Bytes expectReq { 0xf0, 0x00, 0x00, 0x7e, 0x4b, 0x00, 0x01, 0x00, 0x01, 0x05, 0xf7 };
    CHECK (req == expectReq);
}

static void testAckAndError()
{
    const uint8_t ack[] = { 0xf0, 0x00, 0x00, 0x7e, 0x4b, 0x00, 0x0f, 0x03, 0xf7 };
    auto a = sysex::parse (ack, sizeof ack);
    CHECK (a && a->kind == sysex::Message::Kind::Ack && a->value == 0x03);

    const uint8_t err[] = { 0xf0, 0x00, 0x00, 0x7e, 0x4b, 0x00, 0x0e, 0x03, 0xf7 };
    auto e = sysex::parse (err, sizeof err);
    CHECK (e && e->kind == sysex::Message::Kind::Error && e->value == 0x03);

    const uint8_t other[] = { 0xf0, 0x43, 0x10, 0x00, 0xf7 };
    CHECK (! sysex::parse (other, sizeof other));
}

static void testBitFields()
{
    const auto& t = leadParams();
    Patch p = Patch::makeInit (Engine::Lead);

    // Voice 1 attack/decay share byte 0x62.
    p.setByte (0x62, 0x00);
    p.setRaw (t.get ("v1.attack"), 0xa);
    p.setRaw (t.get ("v1.decay"), 0x5);
    CHECK (p.byte (0x62) == 0xa5);
    CHECK (p.getRaw (t.get ("v1.attack")) == 0xa);

    // 12 bit cutoff keeps the FIP flag in bit 7 of the high byte.
    p.setByte (0x56, 0x80);
    auto changed = p.setRaw (t.get ("fL.cutoff"), 0x123);
    CHECK (p.byte (0x55) == 0x23 && p.byte (0x56) == 0x81);
    CHECK (p.getRaw (t.get ("fL.cutoff")) == 0x123);
    CHECK (changed.size() == 2);

    // Ext12: low nibble in [7:4] of addr, high byte at addr+1.
    p.setByte (0x40, 0x0f);
    p.setRaw (t.get ("ext1"), 0xabc);
    CHECK (p.byte (0x40) == 0xcf && p.byte (0x41) == 0xab);
    CHECK (p.getRaw (t.get ("ext1")) == 0xabc);

    // Clamping and offset (signed) values.
    p.setRaw (t.get ("v1.transpose"), 500);
    CHECK (p.getRaw (t.get ("v1.transpose")) == 127);
    CHECK (t.get ("lfo1.depth").minDisplay() == -128 && t.get ("lfo1.depth").maxDisplay() == 127);

    // Init patch values from the firmware: volume 127, voice 1 pulse wave.
    CHECK (Patch::makeInit (Engine::Lead).getRaw (t.get ("lead.volume")) == 127);
    CHECK (Patch::makeInit (Engine::Lead).getRaw (t.get ("v1.pulse")) == 1);
    CHECK (Patch::makeInit (Engine::Lead).getRaw (t.get ("v2.off")) == 1);

    // Every parameter must lie inside the patch and setRaw/getRaw must round trip.
    for (const auto& d : t.all())
    {
        CHECK (d.addr + (d.addresses().size() - 1) < (size_t) kPatchSize);
        Patch q;
        q.setRaw (d, d.maxRaw());
        if (q.getRaw (d) != d.maxRaw())
        {
            ++failures;
            std::printf ("FAIL round trip %s\n", d.id.c_str());
        }
    }
}

static void testLinks()
{
    using sysex::linkedAddresses;
    auto has = [] (const std::vector<uint16_t>& v, std::initializer_list<int> want)
    {
        if (v.size() != want.size()) return false;
        for (int w : want)
            if (std::find (v.begin(), v.end(), (uint16_t) w) == v.end()) return false;
        return true;
    };
    CHECK (linkedAddresses (Engine::Lead, 0x62, sysex::wopt::None).empty());
    CHECK (has (linkedAddresses (Engine::Lead, 0x62, sysex::wopt::LeftRight), { 0x92 }));
    CHECK (has (linkedAddresses (Engine::Lead, 0xa2, sysex::wopt::LeftRight), { 0x72 }));
    CHECK (has (linkedAddresses (Engine::Lead, 0x62, sysex::wopt::Voices), { 0x72, 0x82 }));
    CHECK (has (linkedAddresses (Engine::Lead, 0x62, sysex::wopt::All), { 0x72, 0x82, 0x92, 0xa2, 0xb2 }));
    CHECK (has (linkedAddresses (Engine::Lead, 0x55, sysex::wopt::LeftRight), { 0x5b }));
    CHECK (has (linkedAddresses (Engine::Lead, 0x43, sysex::wopt::LeftRight), { 0x41 }));
    CHECK (has (linkedAddresses (Engine::Multi, 0x60, sysex::wopt::LeftRight), { 0xf0 }));
    CHECK (has (linkedAddresses (Engine::Multi, 0x60, sysex::wopt::Voices), { 0x90, 0xc0 }));
    CHECK (has (linkedAddresses (Engine::Bassline, 0x61, sysex::wopt::LeftRight), { 0xb1 }));
    CHECK (linkedAddresses (Engine::Drum, 0x61, sysex::wopt::LeftRight).empty());
}

static void testSyxFile()
{
    std::vector<syx::Entry> in;
    for (int i = 0; i < 3; ++i)
    {
        syx::Entry e;
        e.bank = 0;
        e.patchNumber = (uint8_t) i;
        e.patch = Patch::makeInit ((Engine) i);
        in.push_back (e);
    }
    auto bytes = syx::write (in);
    auto r = syx::read (bytes.data(), bytes.size());
    CHECK (r.patches.size() == 3 && r.otherMessages == 0);
    for (size_t i = 0; i < r.patches.size(); ++i)
        CHECK (r.patches[i].checksumOk && r.patches[i].patch == in[i].patch && r.patches[i].patchNumber == i);
}

static void summariseFile (const char* path)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<uint8_t> data ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    auto r = syx::read (data.data(), data.size());
    int bad = 0, counts[4] = {};
    for (auto& e : r.patches)
    {
        if (! e.checksumOk) ++bad;
        ++counts[(int) e.patch.engine()];
    }
    std::printf ("%s: %zu patches (%d bad checksum, %d other msgs) L=%d B=%d D=%d M=%d\n", path,
                 r.patches.size(), bad, r.otherMessages, counts[0], counts[1], counts[2], counts[3]);
    for (size_t i = 0; i < r.patches.size() && i < 4; ++i)
        std::printf ("   %c%03d [%c] %s\n", 'A' + r.patches[i].bank, r.patches[i].patchNumber + 1,
                     engineLetter (r.patches[i].patch.engine()), r.patches[i].patch.name().c_str());
    CHECK (bad == 0);

    // Re-encoding must give back the identical file bytes.
    auto again = syx::write (r.patches);
    if (again != data)
        std::printf ("   note: re-encoded file differs from original (%zu vs %zu bytes)\n", again.size(), data.size());
}

int main (int argc, char** argv)
{
    testInitPatches();
    testNames();
    testRoundTrip();
    testParameterWrite();
    testAckAndError();
    testBitFields();
    testSyxFile();
    testLinks();
    for (int i = 1; i < argc; ++i)
        summariseFile (argv[i]);

    std::printf (failures == 0 ? "All tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
