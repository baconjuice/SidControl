#pragma once

// Connection to a MIDIbox SID V2 over a pair of MIDI ports.
//
// - Opens MIDI in/out directly (also when running as a plugin, because many
//   hosts drop or mangle SysEx).
// - Runs requests one at a time and waits for the box's reply (dump, ack or
//   error) with a timeout, so bank transfers are reliable.
// - Coalesces live parameter writes: while a knob is dragged only the latest
//   value per address is sent, at a rate the 31.25 kbaud link can carry.
//
// All callbacks and listener calls happen on the message thread.

#include "sid/SysEx.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <deque>
#include <functional>
#include <map>

class SidDevice : private juce::MidiInputCallback,
                  private juce::Timer
{
public:
    SidDevice();
    ~SidDevice() override;

    // ---- ports -----------------------------------------------------------
    bool openInput (const juce::String& identifier);
    bool openOutput (const juce::String& identifier);
    void closeInput();
    void closeOutput();
    juce::String getInputId() const { return inputId; }
    juce::String getOutputId() const { return outputId; }
    bool isInputOpen() const { return input != nullptr; }
    bool isOutputOpen() const { return output != nullptr; }

    void setDeviceNumber (int n) { deviceNumber = (uint8_t) juce::jlimit (0, 3, n); }
    uint8_t getDeviceNumber() const { return deviceNumber; }

    // ---- sending ---------------------------------------------------------
    void send (const sid::sysex::Bytes& message);
    void sendMidi (const juce::MidiMessage& m);

    // Live edit of one byte in the edit buffer (coalesced and rate limited).
    void queueParameter (uint16_t addr, uint8_t value, uint8_t writeOption = 0);
    void clearPendingParameters() { pendingParams.clear(); }

    enum class Expect { Nothing, Dump, Ack };

    struct Result
    {
        bool ok = false;
        juce::String error;
        const sid::sysex::Message* reply = nullptr; // valid only during the callback
    };
    using Done = std::function<void (const Result&)>;

    // Queue a message and wait for the expected reply before the next one.
    void enqueue (sid::sysex::Bytes message, Expect expect, int timeoutMs, Done done);
    void cancelAll();
    int pendingRequests() const { return (int) jobs.size(); }

    // ---- receiving -------------------------------------------------------
    struct Listener
    {
        virtual ~Listener() = default;
        // A dump that nobody asked for (e.g. sent from the box's control surface).
        virtual void sidUnsolicitedDump (const sid::sysex::Message&) {}
        virtual void sidActivity (bool outgoing) { juce::ignoreUnused (outgoing); }
        virtual void sidLog (const juce::String&) {}
        virtual void sidPortsChanged() {}
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    void log (const juce::String& text);

private:
    struct Job
    {
        sid::sysex::Bytes message;
        Expect expect = Expect::Nothing;
        int timeoutMs = 2000;
        Done done;
        juce::uint32 startedAt = 0;
        bool started = false;
    };

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;
    void handleSysEx (const std::vector<uint8_t>& bytes);
    void startNextJob();
    void finishJob (const Result& r);
    bool isOwnEcho (const sid::sysex::Message& m) const;

    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String inputId, outputId;
    uint8_t deviceNumber = 0;

    std::map<std::pair<uint16_t, uint8_t>, uint8_t> pendingParams; // (addr, wopt) -> value
    juce::uint32 lastParamSendAt = 0;
    std::deque<Job> jobs;
    std::deque<std::pair<sid::Patch, juce::uint32>> recentlySentDumps; // to ignore a MIDI merger echo
    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SidDevice)
    JUCE_DECLARE_NON_COPYABLE (SidDevice)
};
