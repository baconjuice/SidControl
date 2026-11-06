#include "SidDevice.h"

using namespace sid;

namespace
{
constexpr int kTimerMs = 10;
constexpr int kParamsPerTick = 2; // ~2.6 kB/s, below the 3.1 kB/s MIDI limit
} // namespace

SidDevice::SidDevice()
{
    startTimer (kTimerMs);
}

SidDevice::~SidDevice()
{
    stopTimer();
    closeInput(); // stop callbacks before members go away
    closeOutput();
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------
bool SidDevice::openInput (const juce::String& identifier)
{
    closeInput();
    if (identifier.isEmpty())
        return false;

    input = juce::MidiInput::openDevice (identifier, this);
    if (input == nullptr)
    {
        log ("Could not open MIDI input");
        listeners.call ([] (Listener& l) { l.sidPortsChanged(); });
        return false;
    }
    input->start();
    inputId = identifier;
    log ("MIDI in: " + input->getName());
    listeners.call ([] (Listener& l) { l.sidPortsChanged(); });
    return true;
}

bool SidDevice::openOutput (const juce::String& identifier)
{
    closeOutput();
    if (identifier.isEmpty())
        return false;

    auto opened = juce::MidiOutput::openDevice (identifier);
    {
        const juce::SpinLock::ScopedLockType lock (outputLock);
        output = std::move (opened);
    }
    if (output == nullptr)
    {
        log ("Could not open MIDI output");
        listeners.call ([] (Listener& l) { l.sidPortsChanged(); });
        return false;
    }
    outputId = identifier;
    log ("MIDI out: " + output->getName());
    listeners.call ([] (Listener& l) { l.sidPortsChanged(); });
    return true;
}

void SidDevice::closeInput()
{
    if (input != nullptr)
        input->stop();
    input.reset();
    inputId = {};
}

void SidDevice::closeOutput()
{
    std::unique_ptr<juce::MidiOutput> old;
    {
        const juce::SpinLock::ScopedLockType lock (outputLock);
        old = std::move (output);
    }
    old.reset();
    outputId = {};
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------
void SidDevice::send (const sysex::Bytes& message)
{
    if (output == nullptr || message.size() < 2)
        return;

    // Remember outgoing dumps so an echo from the box's MIDI merger is ignored.
    if (auto m = sysex::parse (message.data(), message.size()); m && m->kind == sysex::Message::Kind::PatchDump)
    {
        recentlySentDumps.push_back ({ m->patch, juce::Time::getMillisecondCounter() });
        while (recentlySentDumps.size() > 4)
            recentlySentDumps.pop_front();
    }

    {
        const juce::SpinLock::ScopedLockType lock (outputLock);
        if (output != nullptr)
            output->sendMessageNow (juce::MidiMessage (message.data(), (int) message.size()));
    }
    listeners.call ([] (Listener& l) { l.sidActivity (true); });
}

void SidDevice::sendMidi (const juce::MidiMessage& m)
{
    const juce::SpinLock::ScopedLockType lock (outputLock);
    if (output != nullptr)
        output->sendMessageNow (m);
}

void SidDevice::forwardMidi (const juce::MidiBuffer& buffer)
{
    if (buffer.isEmpty())
        return;

    const int channel = keyboardChannel.load();
    bool sent = false;
    {
        const juce::SpinLock::ScopedLockType lock (outputLock);
        if (output == nullptr)
            return;
        for (const auto meta : buffer)
        {
            auto m = meta.getMessage();
            if (m.isSysEx() || m.isActiveSense() || m.isMetaEvent())
                continue;
            if (channel > 0 && m.getChannel() > 0)
                m.setChannel (channel);
            output->sendMessageNow (m);
            sent = true;
        }
    }
    if (sent)
        lastOutputActivity = juce::Time::getMillisecondCounter();
}

void SidDevice::queueParameter (uint16_t addr, uint8_t value, uint8_t writeOption)
{
    pendingParams[{ addr, writeOption }] = value;
}

void SidDevice::enqueue (sysex::Bytes message, Expect expect, int timeoutMs, Done done)
{
    Job j;
    j.message = std::move (message);
    j.expect = expect;
    j.timeoutMs = timeoutMs;
    j.done = std::move (done);
    jobs.push_back (std::move (j));
    if (jobs.size() == 1)
        startNextJob();
}

void SidDevice::cancelAll()
{
    auto old = std::move (jobs);
    jobs.clear();
    Result r;
    r.error = "Cancelled";
    for (auto& j : old)
        if (j.done) j.done (r);
}

void SidDevice::startNextJob()
{
    while (! jobs.empty())
    {
        auto& j = jobs.front();
        if (j.started)
            return; // already in flight (e.g. queued from a completion callback)

        // Let acks of recent parameter writes arrive first, so they can't be
        // mistaken for the reply to this request. The timer retries.
        if (j.expect != Expect::Nothing
            && (! pendingParams.empty() || juce::Time::getMillisecondCounter() - lastParamSendAt < 60))
            return;

        if (output == nullptr)
        {
            Result r;
            r.error = "No MIDI output selected";
            finishJob (r);
            continue;
        }

        j.started = true;
        j.startedAt = juce::Time::getMillisecondCounter();
        send (j.message);

        if (j.expect == Expect::Nothing)
        {
            Result r;
            r.ok = true;
            finishJob (r);
            continue;
        }
        return; // wait for reply or timeout
    }
}

void SidDevice::finishJob (const Result& r)
{
    if (jobs.empty())
        return;
    auto done = std::move (jobs.front().done);
    jobs.pop_front();
    if (done)
        done (r);
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------
void SidDevice::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! m.isActiveSense())
        lastInputActivity = juce::Time::getMillisecondCounter();

    if (! m.isSysEx())
        return;

    // Copy the full message (F0..F7) and continue on the message thread.
    std::vector<uint8_t> raw (m.getRawData(), m.getRawData() + m.getRawDataSize());
    juce::WeakReference<SidDevice> self (this);
    juce::MessageManager::callAsync ([self, bytes = std::move (raw)]
    {
        if (auto* d = self.get())
            d->handleSysEx (bytes);
    });
}

bool SidDevice::isOwnEcho (const sysex::Message& m) const
{
    // A merger echo arrives right after we sent it; the same patch coming
    // back later is a genuine reply.
    if (m.kind != sid::sysex::Message::Kind::PatchDump)
        return false;
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& [patch, sentAt] : recentlySentDumps)
        if (patch == m.patch && now - sentAt < 1000)
            return true;
    return false;
}

void SidDevice::handleSysEx (const std::vector<uint8_t>& bytes)
{
    auto msg = sysex::parse (bytes.data(), bytes.size());
    if (! msg)
        return;

    listeners.call ([] (Listener& l) { l.sidActivity (false); });

    using Kind = sysex::Message::Kind;
    const bool waiting = ! jobs.empty() && jobs.front().started && jobs.front().expect != Expect::Nothing;

    // A request in flight claims replies from our device number.
    if (waiting && msg->device == deviceNumber)
    {
        const auto expect = jobs.front().expect;
        Result r;
        r.reply = &*msg;

        if (msg->kind == Kind::Error)
        {
            r.error = juce::String ("MIDIbox SID: ") + sysex::errorText (msg->value);
            finishJob (r);
            startNextJob();
            return;
        }
        const bool isDump = msg->kind == Kind::PatchDump || msg->kind == Kind::EnsembleDump;
        if (expect == Expect::Dump && isDump && ! isOwnEcho (*msg))
        {
            r.ok = msg->checksumOk;
            if (! r.ok) r.error = "Received dump with bad checksum";
            finishJob (r);
            startNextJob();
            return;
        }
        if (expect == Expect::Ack && msg->kind == Kind::Ack)
        {
            r.ok = true;
            finishJob (r);
            startNextJob();
            return;
        }
    }

    if ((msg->kind == Kind::PatchDump || msg->kind == Kind::EnsembleDump) && ! isOwnEcho (*msg))
    {
        if (msg->checksumOk)
            listeners.call ([&] (Listener& l) { l.sidUnsolicitedDump (*msg); });
        else
            log ("Ignored a dump with bad checksum");
    }
    else if (msg->kind == Kind::Error)
    {
        log (juce::String ("Error from MIDIbox SID: ") + sysex::errorText (msg->value));
    }
}

void SidDevice::timerCallback()
{
    const bool requestInFlight = ! jobs.empty() && jobs.front().started && jobs.front().expect != Expect::Nothing;

    // Rate limited live parameter writes (held back while a request waits for its reply).
    for (int i = 0; i < kParamsPerTick && ! requestInFlight && ! pendingParams.empty(); ++i)
    {
        auto it = pendingParams.begin();
        const auto [addr, wopt] = it->first;
        const auto value = it->second;
        pendingParams.erase (it);
        send (wopt == kEnsembleKey ? sysex::ensembleParameterWrite (deviceNumber, addr, value)
                                   : sysex::parameterWrite (deviceNumber, addr, value, wopt));
        lastParamSendAt = juce::Time::getMillisecondCounter();
    }

    if (! jobs.empty() && ! jobs.front().started)
        startNextJob();

    // Request timeouts.
    if (! jobs.empty() && jobs.front().started && jobs.front().expect != Expect::Nothing)
    {
        const auto elapsed = juce::Time::getMillisecondCounter() - jobs.front().startedAt;
        if ((int) elapsed > jobs.front().timeoutMs)
        {
            Result r;
            r.error = "No reply from MIDIbox SID (timeout)";
            finishJob (r);
            startNextJob();
        }
    }
}

void SidDevice::log (const juce::String& text)
{
    listeners.call ([&] (Listener& l) { l.sidLog (text); });
}
