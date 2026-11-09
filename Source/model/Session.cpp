#include "Session.h"
#include "EnsembleSession.h"
#include "MidiLearn.h"

#include <cstring>

using namespace sid;

namespace
{
juce::String slotName (int bank, int slot)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + juce::String (slot + 1).paddedLeft ('0', 3);
}

constexpr int kDumpTimeoutMs = 2500;
constexpr int kWriteTimeoutMs = 4000; // BankStick (EEPROM) writes are slow
} // namespace

Session::Session (SidDevice& d) : device (d)
{
    device.addListener (this);
    ensemble = std::make_unique<EnsembleSession> (*this);
    midiLearn = std::make_unique<MidiLearn> (*this);
}

void Session::fireReplaced()
{
    listeners.call ([] (Listener& l) { l.editReplaced(); });
    notifyReplaced();
}

void Session::fireChanged (const std::vector<uint16_t>& addresses)
{
    listeners.call ([&] (Listener& l) { l.editChanged (addresses); });
    notifyChanged (addresses);
}

int Session::defaultRaw (const ParamDef& p) const
{
    return Patch::makeInit (engine()).getRaw (p);
}

Session::~Session()
{
    device.removeListener (this);
}

// ---------------------------------------------------------------------------
// Edit buffer
// ---------------------------------------------------------------------------
void Session::setParam (const ParamDef& p, int raw, uint8_t writeOption)
{
    auto changed = editPatch.setRaw (p, raw);
    if (changed.empty())
        return;

    std::vector<uint16_t> all = changed;
    for (auto addr : changed)
    {
        const auto value = editPatch.byte (addr);
        auto links = sysex::linkedAddresses (editPatch.engine(), addr, writeOption);
        for (auto other : links)
        {
            editPatch.setByte (other, value);
            all.push_back (other);
        }
        if (live)
            device.queueParameter (addr, value, links.empty() ? sysex::wopt::None : writeOption);
    }
    fireChanged (all);
}

void Session::setName (const juce::String& name)
{
    const auto before = editPatch;
    editPatch.setName (name.toStdString());
    std::vector<uint16_t> changed;
    for (int i = 0; i < kNameLength; ++i)
    {
        if (before.byte (i) != editPatch.byte (i))
        {
            changed.push_back ((uint16_t) i);
            if (live)
                device.queueParameter ((uint16_t) i, editPatch.byte (i));
        }
    }
    if (! changed.empty())
        fireChanged (changed);
}

void Session::loadIntoEditor (const Patch& p, int fromSlot)
{
    editPatch = p;
    sourceSlot = fromSlot;
    fireReplaced();
    if (live)
        sendEditBuffer();
}

void Session::sendEditBuffer()
{
    if (! device.isOutputOpen())
    {
        setStatus ("Select a MIDI output first");
        return;
    }
    device.clearPendingParameters();
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::writeEditBuffer (device.getDeviceNumber(), editPatch), SidDevice::Expect::Ack, kDumpTimeoutMs,
                    [self] (const SidDevice::Result& r)
                    {
                        if (auto* s = self.get())
                            s->setStatus (r.ok ? "Sent patch to edit buffer" : "Send failed: " + r.error);
                    });
}

void Session::fetchEditBuffer()
{
    juce::WeakReference<Session> self (this);
    setStatus ("Requesting edit buffer...");
    device.enqueue (sysex::requestEditBuffer (device.getDeviceNumber()), SidDevice::Expect::Dump, kDumpTimeoutMs,
                    [self] (const SidDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok && r.reply != nullptr)
                        {
                            s->editPatch = r.reply->patch;
                            s->sourceSlot = -1;
                            s->fireReplaced();
                            s->setStatus ("Received edit buffer: " + juce::String (s->editPatch.name()));
                        }
                        else
                        {
                            s->setStatus ("Fetch failed: " + r.error);
                        }
                    });
}

void Session::playPatch()   { device.send (sysex::playPatch (device.getDeviceNumber())); }
void Session::allNotesOff() { device.send (sysex::allNotesOff (device.getDeviceNumber())); }

void Session::ping()
{
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::ping (device.getDeviceNumber()), SidDevice::Expect::Ack, 1000,
                    [self] (const SidDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok)
                        {
                            if (! s->device.isOutputOpen() || ! s->device.isInputOpen())
                                s->setStatus ("Select both MIDI In and MIDI Out first");
                            else
                                s->setStatus ("No answer from MIDIbox SID. Check: MIDI Out goes to the SID's MIDI IN "
                                              "and the SID's MIDI OUT comes back to MIDI In, the ports and device number match, "
                                              "and the SID is running (LCD shows a patch).");
                            return;
                        }
                        juce::String sids;
                        const auto mask = r.reply != nullptr ? r.reply->value : 0;
                        for (int i = 0; i < 4; ++i)
                            if (mask & (1 << i))
                                sids << (sids.isEmpty() ? "" : ", ") << "SID" << (i + 1);
                        s->setStatus ("MIDIbox SID answered" + (sids.isEmpty() ? juce::String() : " (" + sids + ")"));
                    });
}

// ---------------------------------------------------------------------------
// Bank
// ---------------------------------------------------------------------------
void Session::setSlot (int i, std::optional<Patch> p)
{
    if (i < 0 || i >= kSlots) return;
    slots[(size_t) i] = std::move (p);
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

void Session::setBankNumber (int b)
{
    bankNumber = juce::jlimit (0, 7, b);
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

void Session::fetchSlot (int i, bool thenEdit)
{
    juce::WeakReference<Session> self (this);
    const int bank = bankNumber;
    setStatus ("Requesting " + slotName (bank, i) + "...");
    device.enqueue (sysex::requestPatch (device.getDeviceNumber(), (uint8_t) bank, (uint8_t) i), SidDevice::Expect::Dump,
                    kDumpTimeoutMs,
                    [self, i, bank, thenEdit] (const SidDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok || r.reply == nullptr)
                        {
                            s->setStatus ("Fetch " + slotName (bank, i) + " failed: " + r.error);
                            return;
                        }
                        s->setSlot (i, r.reply->patch);
                        s->setStatus ("Received " + slotName (bank, i) + ": " + juce::String (r.reply->patch.name()));
                        // Reading a stored patch doesn't change what the box plays, so
                        // opening it in the editor also sends it to the edit buffer.
                        if (thenEdit)
                            s->loadIntoEditor (r.reply->patch, i);
                    });
}

void Session::fetchBank()
{
    if (busy) return;
    if (! device.isOutputOpen() || ! device.isInputOpen())
    {
        setStatus ("Select MIDI input and output first");
        return;
    }
    busy = true;
    cancelRequested = false;
    fetchNext (0, kSlots, 0, 0);
}

void Session::fetchNext (int i, int endExclusive, int got, int failed)
{
    if (cancelRequested || i >= endExclusive)
    {
        busy = false;
        setStatus (juce::String (cancelRequested ? "Cancelled: " : "Bank fetched: ") + juce::String (got) + " patches"
                   + (failed > 0 ? ", " + juce::String (failed) + " not available" : juce::String()));
        return;
    }

    setStatus ("Fetching " + slotName (bankNumber, i) + "...", (float) i / (float) endExclusive);
    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::requestPatch (device.getDeviceNumber(), (uint8_t) bankNumber, (uint8_t) i), SidDevice::Expect::Dump,
                    kDumpTimeoutMs,
                    [self, i, endExclusive, got, failed] (const SidDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok && r.reply != nullptr)
                        {
                            s->slots[(size_t) i] = r.reply->patch;
                            s->listeners.call ([] (Listener& l) { l.bankChanged(); });
                            s->fetchNext (i + 1, endExclusive, got + 1, failed);
                        }
                        else if (r.reply != nullptr)
                        {
                            // The box answered with an error (e.g. 32k BankStick has only 64 patches): skip.
                            s->fetchNext (i + 1, endExclusive, got, failed + 1);
                        }
                        else
                        {
                            s->busy = false;
                            s->setStatus ("Bank fetch stopped at " + slotName (s->bankNumber, i) + ": " + r.error);
                        }
                    });
}

void Session::storeEditToSlot (int i)
{
    if (i < 0 || i >= kSlots) return;
    juce::WeakReference<Session> self (this);
    const int bank = bankNumber;
    const Patch p = editPatch;
    setStatus ("Storing to " + slotName (bank, i) + "...");
    device.enqueue (sysex::writePatch (device.getDeviceNumber(), (uint8_t) bank, (uint8_t) i, p), SidDevice::Expect::Ack,
                    kWriteTimeoutMs,
                    [self, i, bank, p] (const SidDevice::Result& r)
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (r.ok)
                        {
                            s->sourceSlot = i;
                            s->setSlot (i, p);
                            s->setStatus ("Stored " + slotName (bank, i) + ": " + juce::String (p.name()));
                        }
                        else
                        {
                            s->setStatus ("Store failed: " + r.error);
                        }
                    });
}

void Session::writeBankToDevice()
{
    if (busy) return;
    std::vector<int> todo;
    for (int i = kSlots - 1; i >= 0; --i)
        if (slots[(size_t) i]) todo.push_back (i);
    if (todo.empty())
    {
        setStatus ("Bank is empty");
        return;
    }
    busy = true;
    cancelRequested = false;
    const int total = (int) todo.size();
    writeNext (std::move (todo), 0, 0, total);
}

void Session::writeNext (std::vector<int> remaining, int done, int failed, int total)
{
    if (cancelRequested || remaining.empty())
    {
        busy = false;
        setStatus (juce::String (cancelRequested ? "Cancelled: " : "Bank written: ") + juce::String (done) + " patches"
                   + (failed > 0 ? ", " + juce::String (failed) + " failed" : juce::String()));
        return;
    }
    const int i = remaining.back();
    remaining.pop_back();
    setStatus ("Writing " + slotName (bankNumber, i) + "...", (float) done / (float) total);

    juce::WeakReference<Session> self (this);
    device.enqueue (sysex::writePatch (device.getDeviceNumber(), (uint8_t) bankNumber, (uint8_t) i, *slots[(size_t) i]),
                    SidDevice::Expect::Ack, kWriteTimeoutMs,
                    [self, remaining, done, failed, total] (const SidDevice::Result& r) mutable
                    {
                        auto* s = self.get();
                        if (s == nullptr) return;
                        if (! r.ok && r.reply == nullptr)
                        {
                            s->busy = false;
                            s->setStatus ("Bank write stopped: " + r.error);
                            return;
                        }
                        s->writeNext (std::move (remaining), done + (r.ok ? 1 : 0), failed + (r.ok ? 0 : 1), total);
                    });
}

void Session::cancelTransfers()
{
    cancelRequested = true;
    device.cancelAll();
    busy = false;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
bool Session::loadSyx (const juce::File& f, juce::String& message)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb))
    {
        message = "Could not read " + f.getFileName();
        return false;
    }
    auto r = syx::read (static_cast<const uint8_t*> (mb.getData()), mb.getSize());
    if (r.patches.empty())
    {
        message = f.getFileName() + " contains no MIDIbox SID V2 patches";
        return false;
    }

    int bad = 0;
    for (auto& e : r.patches)
        if (! e.checksumOk) ++bad;

    if (r.patches.size() == 1)
    {
        // Single patch: open it in the editor.
        loadIntoEditor (r.patches.front().patch);
        message = "Loaded patch " + juce::String (r.patches.front().patch.name());
    }
    else
    {
        for (auto& e : r.patches)
            slots[(size_t) (e.patchNumber & 0x7f)] = e.patch;
        listeners.call ([] (Listener& l) { l.bankChanged(); });
        message = "Loaded " + juce::String ((int) r.patches.size()) + " patches from " + f.getFileName();
    }
    if (bad > 0)
        message << " (" << bad << " with bad checksum)";
    return true;
}

bool Session::saveBankSyx (const juce::File& f, juce::String& message) const
{
    std::vector<syx::Entry> entries;
    for (int i = 0; i < kSlots; ++i)
    {
        if (! slots[(size_t) i]) continue;
        syx::Entry e;
        e.bank = (uint8_t) bankNumber;
        e.patchNumber = (uint8_t) i;
        e.patch = *slots[(size_t) i];
        entries.push_back (e);
    }
    if (entries.empty())
    {
        message = "Bank is empty";
        return false;
    }
    auto bytes = syx::write (entries);
    if (! f.replaceWithData (bytes.data(), bytes.size()))
    {
        message = "Could not write " + f.getFileName();
        return false;
    }
    message = "Saved " + juce::String ((int) entries.size()) + " patches to " + f.getFileName();
    return true;
}

bool Session::savePatchSyx (const juce::File& f, juce::String& message) const
{
    // Edit buffer format: can be sent to any box without overwriting a slot.
    syx::Entry e;
    e.editBuffer = true;
    e.patch = editPatch;
    auto bytes = syx::write ({ e });
    if (! f.replaceWithData (bytes.data(), bytes.size()))
    {
        message = "Could not write " + f.getFileName();
        return false;
    }
    message = "Saved " + juce::String (editPatch.name()) + " to " + f.getFileName();
    return true;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
juce::ValueTree Session::toValueTree() const
{
    juce::ValueTree v ("SidControl");
    v.setProperty ("version", 1, nullptr);
    v.setProperty ("midiIn", device.getInputId(), nullptr);
    v.setProperty ("midiOut", device.getOutputId(), nullptr);
    v.setProperty ("deviceNumber", (int) device.getDeviceNumber(), nullptr);
    v.setProperty ("live", live, nullptr);
    v.setProperty ("keyboardChannel", device.getKeyboardChannel(), nullptr);
    v.setProperty ("bank", bankNumber, nullptr);
    v.setProperty ("sourceSlot", sourceSlot, nullptr);
    v.setProperty ("edit", juce::Base64::toBase64 (editPatch.data.data(), editPatch.data.size()), nullptr);

    juce::ValueTree b ("Bank");
    for (int i = 0; i < kSlots; ++i)
    {
        if (! slots[(size_t) i]) continue;
        juce::ValueTree s ("Slot");
        s.setProperty ("index", i, nullptr);
        s.setProperty ("data", juce::Base64::toBase64 (slots[(size_t) i]->data.data(), kPatchSize), nullptr);
        b.appendChild (s, nullptr);
    }
    v.appendChild (b, nullptr);
    v.appendChild (ensemble->toValueTree(), nullptr);
    v.appendChild (midiLearn->toValueTree(), nullptr);
    return v;
}

void Session::fromValueTree (const juce::ValueTree& v)
{
    if (! v.hasType ("SidControl"))
        return;

    auto decode = [] (const juce::String& s, Patch& out)
    {
        juce::MemoryOutputStream mo;
        if (! juce::Base64::convertFromBase64 (mo, s) || mo.getDataSize() != (size_t) kPatchSize)
            return false;
        std::memcpy (out.data.data(), mo.getData(), (size_t) kPatchSize);
        return true;
    };

    device.setDeviceNumber (v.getProperty ("deviceNumber", 0));
    live = v.getProperty ("live", true);
    device.setKeyboardChannel (v.getProperty ("keyboardChannel", 0));
    bankNumber = juce::jlimit (0, 7, (int) v.getProperty ("bank", 0));
    sourceSlot = v.getProperty ("sourceSlot", -1);

    Patch p;
    if (decode (v.getProperty ("edit").toString(), p))
        editPatch = p;

    slots = {};
    for (auto s : v.getChildWithName ("Bank"))
    {
        const int i = s.getProperty ("index", -1);
        if (i >= 0 && i < kSlots && decode (s.getProperty ("data").toString(), p))
            slots[(size_t) i] = p;
    }

    ensemble->fromValueTree (v.getChildWithName ("Ensemble"));
    midiLearn->fromValueTree (v.getChildWithName ("MidiLearn"));

    if (auto in = v.getProperty ("midiIn").toString(); in.isNotEmpty())
        device.openInput (in);
    if (auto out = v.getProperty ("midiOut").toString(); out.isNotEmpty())
        device.openOutput (out);

    fireReplaced();
    listeners.call ([] (Listener& l) { l.bankChanged(); });
}

// ---------------------------------------------------------------------------
// Device notifications
// ---------------------------------------------------------------------------
void Session::sidUnsolicitedDump (const sysex::Message& m)
{
    if (m.kind == sysex::Message::Kind::EnsembleDump)
    {
        ensemble->replace (m.ensemble, true);
        setStatus ("Received ensemble from MIDIbox SID");
        return;
    }

    // E.g. the user pressed "send" on the box: show it in the editor.
    if ((m.dumpType & sysex::type::EditBuffer) != 0 || m.bank != bankNumber)
    {
        editPatch = m.patch;
        sourceSlot = -1;
        fireReplaced();
        setStatus ("Received patch from MIDIbox SID: " + juce::String (m.patch.name()));
    }
    else
    {
        setSlot (m.patchNumber, m.patch);
        setStatus ("Received " + slotName (m.bank, m.patchNumber) + ": " + juce::String (m.patch.name()));
    }
}

void Session::sidLog (const juce::String& text)
{
    logLines.add (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + text);
    while (logLines.size() > 200)
        logLines.remove (0);
    listeners.call ([] (Listener& l) { l.statusChanged(); });
}

void Session::setStatus (const juce::String& s, float newProgress)
{
    status = s;
    progress = newProgress;
    sidLog (s);
}
