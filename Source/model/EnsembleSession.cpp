#include "EnsembleSession.h"
#include "Session.h"

#include "sid/SyxFile.h"

#include <cstring>

using namespace sid;

namespace
{
constexpr int kTimeoutMs = 2500;
constexpr int kWriteTimeoutMs = 4000;
juce::String ensName (int n) { return "E" + juce::String (n + 1).paddedLeft ('0', 3); }
} // namespace

EnsembleSession::EnsembleSession (Session& s) : session (s)
{
}

void EnsembleSession::setParam (const ParamDef& p, int raw, uint8_t)
{
    auto changed = ens.setRaw (p, raw);
    if (changed.empty())
        return;
    if (session.isLive())
        for (auto addr : changed)
            session.device.queueEnsembleParameter (addr, ens.byte (addr));
    notifyChanged (changed);
}

void EnsembleSession::replace (const Ensemble& e, bool fromBox)
{
    ens = e;
    loaded = true;
    notifyReplaced();
    if (! fromBox && session.isLive())
        sendEditBuffer();
}

void EnsembleSession::fetchEditBuffer()
{
    juce::WeakReference<EnsembleSession> self (this);
    session.setStatus ("Requesting current ensemble...");
    session.device.enqueue (sysex::requestEnsembleEditBuffer (session.device.getDeviceNumber(), (uint8_t) number),
                            SidDevice::Expect::Dump, kTimeoutMs,
                            [self] (const SidDevice::Result& r)
                            {
                                auto* e = self.get();
                                if (e == nullptr) return;
                                if (r.ok && r.reply != nullptr && r.reply->kind == sysex::Message::Kind::EnsembleDump)
                                {
                                    e->replace (r.reply->ensemble, true);
                                    e->session.setStatus ("Received current ensemble");
                                }
                                else
                                {
                                    e->session.setStatus ("Ensemble fetch failed: " + (r.error.isNotEmpty() ? r.error : "unexpected reply"));
                                }
                            });
}

void EnsembleSession::sendEditBuffer()
{
    if (! session.device.isOutputOpen())
    {
        session.setStatus ("Select a MIDI output first");
        return;
    }
    juce::WeakReference<EnsembleSession> self (this);
    session.device.enqueue (sysex::writeEnsembleEditBuffer (session.device.getDeviceNumber(), ens), SidDevice::Expect::Ack,
                            kTimeoutMs,
                            [self] (const SidDevice::Result& r)
                            {
                                if (auto* e = self.get())
                                    e->session.setStatus (r.ok ? "Sent ensemble" : "Ensemble send failed: " + r.error);
                            });
}

void EnsembleSession::fetchStored()
{
    juce::WeakReference<EnsembleSession> self (this);
    const int n = number;
    session.setStatus ("Requesting ensemble " + ensName (n) + "...");
    session.device.enqueue (sysex::requestEnsemble (session.device.getDeviceNumber(), (uint8_t) n), SidDevice::Expect::Dump,
                            kTimeoutMs,
                            [self, n] (const SidDevice::Result& r)
                            {
                                auto* e = self.get();
                                if (e == nullptr) return;
                                if (r.ok && r.reply != nullptr && r.reply->kind == sysex::Message::Kind::EnsembleDump)
                                {
                                    e->replace (r.reply->ensemble, true);
                                    e->session.setStatus ("Received ensemble " + ensName (n));
                                }
                                else
                                {
                                    e->session.setStatus ("Ensemble fetch failed: " + (r.error.isNotEmpty() ? r.error : "unexpected reply"));
                                }
                            });
}

void EnsembleSession::storeToBox()
{
    juce::WeakReference<EnsembleSession> self (this);
    const int n = number;
    session.setStatus ("Storing ensemble " + ensName (n) + "...");
    session.device.enqueue (sysex::writeEnsemble (session.device.getDeviceNumber(), (uint8_t) n, ens), SidDevice::Expect::Ack,
                            kWriteTimeoutMs,
                            [self, n] (const SidDevice::Result& r)
                            {
                                if (auto* e = self.get())
                                    e->session.setStatus (r.ok ? "Stored ensemble " + ensName (n) : "Ensemble store failed: " + r.error);
                            });
}

bool EnsembleSession::loadSyx (const juce::File& f, juce::String& message)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb))
    {
        message = "Could not read " + f.getFileName();
        return false;
    }
    auto r = syx::read (static_cast<const uint8_t*> (mb.getData()), mb.getSize());
    if (r.ensembles.empty())
    {
        message = f.getFileName() + " contains no ensemble";
        return false;
    }
    const auto& first = r.ensembles.front();
    if (! first.editBuffer)
        number = first.number;
    replace (first.ensemble, false);
    message = "Loaded ensemble from " + f.getFileName();
    if (! first.checksumOk)
        message << " (bad checksum)";
    return true;
}

bool EnsembleSession::saveSyx (const juce::File& f, juce::String& message) const
{
    syx::EnsembleEntry e;
    e.number = (uint8_t) number;
    e.ensemble = ens;
    auto bytes = syx::writeEnsembles ({ e });
    if (! f.replaceWithData (bytes.data(), bytes.size()))
    {
        message = "Could not write " + f.getFileName();
        return false;
    }
    message = "Saved ensemble to " + f.getFileName();
    return true;
}

juce::ValueTree EnsembleSession::toValueTree() const
{
    juce::ValueTree v ("Ensemble");
    v.setProperty ("number", number, nullptr);
    if (loaded)
        v.setProperty ("data", juce::Base64::toBase64 (ens.data.data(), ens.data.size()), nullptr);
    return v;
}

void EnsembleSession::fromValueTree (const juce::ValueTree& v)
{
    if (! v.isValid())
        return;
    number = juce::jlimit (0, 127, (int) v.getProperty ("number", 0));
    juce::MemoryOutputStream mo;
    if (juce::Base64::convertFromBase64 (mo, v.getProperty ("data").toString()) && mo.getDataSize() == (size_t) kEnsembleSize)
    {
        std::memcpy (ens.data.data(), mo.getData(), (size_t) kEnsembleSize);
        loaded = true;
    }
    notifyReplaced();
}
