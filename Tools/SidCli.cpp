// sidcli: command-line diagnostics for a MIDIbox SID, using the same device
// code as the SidControl app.
//
//   sidcli list
//   sidcli ping   <in-port> <out-port> [device]
//   sidcli get    <in-port> <out-port> [device]     (read the edit buffer)
//   sidcli scan   [port-filter]                     (ping every in/out pairing)
//   sidcli monitor <in-port> [seconds]              (print everything received)
//   sidcli play   <out-port> [device]               (box plays a test note; listen!)
//
// Port arguments match any part of the port name, case-insensitively.

#include "device/SidDevice.h"
#include "sid/Params.h"

#include <juce_events/juce_events.h>

#include <iostream>

namespace
{
juce::MidiDeviceInfo findPort (const juce::Array<juce::MidiDeviceInfo>& ports, const juce::String& part)
{
    for (auto& p : ports)
        if (p.name.containsIgnoreCase (part))
            return p;
    return {};
}

void waitFor (const bool& done, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! done && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

struct Printer : SidDevice::Listener
{
    void sidLog (const juce::String& s) override { std::cout << "  log: " << s << "\n"; }
};

// Returns true if the box answered.
bool request (SidDevice& dev, const sid::sysex::Bytes& msg, SidDevice::Expect expect, bool quiet = false)
{
    bool done = false, ok = false;
    dev.enqueue (msg, expect, 1500, [&] (const SidDevice::Result& r)
    {
        done = true;
        ok = r.ok;
        if (quiet) return;
        if (! r.ok)
            std::cout << "  no: " << r.error << "\n";
        else if (r.reply != nullptr && r.reply->kind == sid::sysex::Message::Kind::Ack)
            std::cout << "  ack, SID cores answering: mask 0x" << juce::String::toHexString ((int) r.reply->value) << "\n";
        else if (r.reply != nullptr && r.reply->kind == sid::sysex::Message::Kind::PatchDump)
            std::cout << "  patch \"" << r.reply->patch.name() << "\" (" << sid::engineName (r.reply->patch.engine())
                      << " engine)\n";
    });
    waitFor (done, 3000);
    return ok;
}

int usage()
{
    std::cout << "usage: sidcli list | ping <in> <out> [dev] | get <in> <out> [dev] | scan [filter]\n"
                 "              | monitor <in> [sec] | play <out> [dev]\n";
    return 2;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args.isEmpty()) return usage();

    const auto ins = juce::MidiInput::getAvailableDevices();
    const auto outs = juce::MidiOutput::getAvailableDevices();
    const auto cmd = args[0];

    if (cmd == "list")
    {
        std::cout << "MIDI inputs:\n";
        for (auto& p : ins) std::cout << "  " << p.name << "\n";
        std::cout << "MIDI outputs:\n";
        for (auto& p : outs) std::cout << "  " << p.name << "\n";
        return 0;
    }

    if (cmd == "monitor" && args.size() >= 2)
    {
        auto in = findPort (ins, args[1]);
        if (in.identifier.isEmpty()) { std::cout << "no input matching " << args[1] << "\n"; return 1; }
        struct Mon : juce::MidiInputCallback
        {
            void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
            {
                std::cout << "  " << juce::String::toHexString (m.getRawData(), m.getRawDataSize()) << "\n" << std::flush;
            }
        } mon;
        auto port = juce::MidiInput::openDevice (in.identifier, &mon);
        port->start();
        const int secs = args.size() >= 3 ? args[2].getIntValue() : 15;
        std::cout << "Listening on " << in.name << " for " << secs << " s...\n" << std::flush;
        bool never = false;
        waitFor (never, secs * 1000);
        return 0;
    }

    if (cmd == "play" && args.size() >= 2)
    {
        auto out = findPort (outs, args[1]);
        if (out.identifier.isEmpty()) { std::cout << "no output matching " << args[1] << "\n"; return 1; }
        SidDevice dev;
        dev.openOutput (out.identifier);
        const auto d = (uint8_t) (args.size() >= 3 ? args[2].getIntValue() : 0);
        std::cout << "Sending 'play patch' to " << out.name << " (device " << (int) d << "). Listen to the SID...\n";
        dev.send (sid::sysex::playPatch (d));
        bool never = false;
        waitFor (never, 1500);
        dev.send (sid::sysex::allNotesOff (d));
        return 0;
    }

    // Test of keyboard play-through: note on/off on ch 1, remapped to <channel>.
    if (cmd == "thru" && args.size() >= 3)
    {
        auto out = findPort (outs, args[1]);
        if (out.identifier.isEmpty()) { std::cout << "no output matching " << args[1] << "\n"; return 1; }
        SidDevice dev;
        dev.openOutput (out.identifier);
        dev.setKeyboardChannel (args[2].getIntValue());
        juce::MidiBuffer on, off;
        on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        dev.forwardMidi (on);
        bool never = false;
        waitFor (never, 800);
        dev.forwardMidi (off);
        waitFor (never, 200);
        std::cout << "sent note 60 (ch 1 -> " << (args[2].getIntValue() == 0 ? juce::String ("as is") : args[2]) << ")\n";
        return 0;
    }

    if (cmd == "scan")
    {
        const auto filter = args.size() >= 2 ? args[1] : juce::String();
        bool any = false;
        for (auto& o : outs)
        {
            if (filter.isNotEmpty() && ! o.name.containsIgnoreCase (filter)) continue;
            for (auto& i : ins)
            {
                if (filter.isNotEmpty() && ! i.name.containsIgnoreCase (filter)) continue;
                SidDevice dev;
                if (! dev.openOutput (o.identifier) || ! dev.openInput (i.identifier)) continue;
                for (int d = 0; d < 4; ++d)
                {
                    dev.setDeviceNumber (d);
                    if (request (dev, sid::sysex::ping ((uint8_t) d), SidDevice::Expect::Ack, true))
                    {
                        std::cout << "FOUND: out \"" << o.name << "\" -> in \"" << i.name << "\", device " << d << "\n";
                        any = true;
                    }
                }
            }
        }
        if (! any) std::cout << "No MIDIbox SID answered on any port pairing.\n";
        return any ? 0 : 1;
    }

    if (cmd == "getens" && args.size() >= 3)
    {
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        if (in.identifier.isEmpty() || out.identifier.isEmpty()) { std::cout << "port not found\n"; return 1; }
        SidDevice dev;
        dev.openInput (in.identifier);
        dev.openOutput (out.identifier);
        bool done = false, ok = false;
        dev.enqueue (sid::sysex::requestEnsembleEditBuffer (0, 0), SidDevice::Expect::Dump, 2000, [&] (const SidDevice::Result& r)
        {
            done = true;
            if (! r.ok || r.reply == nullptr || r.reply->kind != sid::sysex::Message::Kind::EnsembleDump)
            {
                std::cout << "  no ensemble: " << r.error << "\n";
                return;
            }
            ok = true;
            const auto& e = r.reply->ensemble;
            const auto& t = sid::ensembleParams();
            std::cout << "  ensemble ok (checksum " << (r.reply->checksumOk ? "ok" : "BAD") << ")\n";
            for (int s = 1; s <= 4; ++s)
            {
                const std::string p = "s" + std::to_string (s);
                std::cout << "  SID" << s << ": patch " << (char) ('A' + e.getRaw (t.get (p + ".bank")))
                          << (e.getRaw (t.get (p + ".patch")) + 1) << ", BPM " << e.getRaw (t.get (p + ".bpm"))
                          << ", instr1 ch " << (e.getRaw (t.get (p + ".i1.chn")) + 1)
                          << (e.getRaw (t.get (p + ".i1.off")) ? " (off)" : "") << "\n";
            }
        });
        waitFor (done, 3000);
        return ok ? 0 : 1;
    }

    if ((cmd == "ping" || cmd == "get") && args.size() >= 3)
    {
        auto in = findPort (ins, args[1]);
        auto out = findPort (outs, args[2]);
        if (in.identifier.isEmpty()) { std::cout << "no input matching " << args[1] << "\n"; return 1; }
        if (out.identifier.isEmpty()) { std::cout << "no output matching " << args[2] << "\n"; return 1; }

        SidDevice dev;
        Printer printer;
        dev.addListener (&printer);
        dev.openInput (in.identifier);
        dev.openOutput (out.identifier);
        dev.setDeviceNumber (args.size() >= 4 ? args[3].getIntValue() : 0);

        const bool ok = cmd == "ping"
                            ? request (dev, sid::sysex::ping (dev.getDeviceNumber()), SidDevice::Expect::Ack)
                            : request (dev, sid::sysex::requestEditBuffer (dev.getDeviceNumber()), SidDevice::Expect::Dump);
        dev.removeListener (&printer);
        return ok ? 0 : 1;
    }

    return usage();
}
