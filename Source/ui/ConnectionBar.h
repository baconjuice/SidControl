#pragma once

#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// Top bar: MIDI ports, device number, connection test and edit buffer actions.
class ConnectionBar : public juce::Component,
                      private SidDevice::Listener,
                      private juce::Timer
{
public:
    explicit ConnectionBar (Session& s);
    ~ConnectionBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void refreshPorts();
    void sidActivity (bool outgoing) override;
    void sidPortsChanged() override;
    void timerCallback() override;

    Session& session;
    juce::Label inLabel, outLabel, devLabel;
    juce::ComboBox inBox, outBox, devBox;
    juce::TextButton pingButton { "Test" }, liveButton { "Live" }, getButton { "Get Edit Buffer" },
        sendButton { "Send Edit Buffer" }, playButton { "Play" }, panicButton { "Panic" };

    juce::Array<juce::MidiDeviceInfo> inputs, outputs;
    juce::MidiDeviceListConnection deviceListConnection;
    juce::uint32 lastIn = 0, lastOut = 0;
    juce::Rectangle<int> inLed, outLed;
};
} // namespace ui
