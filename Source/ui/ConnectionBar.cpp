#include "ConnectionBar.h"
#include "LookAndFeel.h"

namespace ui
{
ConnectionBar::ConnectionBar (Session& s) : session (s)
{
    auto setupLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (font (12.5f));
        l.setColour (juce::Label::textColourId, colours::dimText);
        l.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    };
    setupLabel (inLabel, "MIDI In");
    setupLabel (outLabel, "MIDI Out");
    setupLabel (devLabel, "Device");

    addAndMakeVisible (inBox);
    addAndMakeVisible (outBox);
    addAndMakeVisible (devBox);
    for (int i = 0; i < 4; ++i)
        devBox.addItem (juce::String (i) + (i == 0 ? " (Master)" : ""), i + 1);
    devBox.setSelectedId (session.device.getDeviceNumber() + 1, juce::dontSendNotification);
    devBox.setTooltip ("SysEx device number of the core (0 = master)");
    devBox.onChange = [this] { session.device.setDeviceNumber (devBox.getSelectedId() - 1); };

    inBox.onChange = [this]
    {
        const int i = inBox.getSelectedId() - 2;
        if (i < 0) session.device.closeInput();
        else if (i < inputs.size()) session.device.openInput (inputs[i].identifier);
    };
    outBox.onChange = [this]
    {
        const int i = outBox.getSelectedId() - 2;
        if (i < 0) session.device.closeOutput();
        else if (i < outputs.size()) session.device.openOutput (outputs[i].identifier);
    };

    pingButton.setTooltip ("Ping the MIDIbox SID and show which SID cores answer");
    pingButton.onClick = [this] { session.ping(); };

    liveButton.setClickingTogglesState (true);
    liveButton.setToggleState (session.isLive(), juce::dontSendNotification);
    liveButton.setTooltip ("When on, every edit is sent to the MIDIbox SID immediately");
    liveButton.onClick = [this] { session.setLive (liveButton.getToggleState()); };

    getButton.setTooltip ("Read the patch currently playing on the MIDIbox SID");
    getButton.onClick = [this] { session.fetchEditBuffer(); };
    sendButton.setTooltip ("Send the whole patch in the editor to the MIDIbox SID (not stored)");
    sendButton.onClick = [this] { session.sendEditBuffer(); };
    playButton.setTooltip ("Play a test note (C-3) on the MIDIbox SID");
    playButton.onClick = [this] { session.playPatch(); };
    panicButton.setTooltip ("All notes and sequences off");
    panicButton.onClick = [this] { session.allNotesOff(); };

    for (auto* b : { &pingButton, &liveButton, &getButton, &sendButton, &playButton, &panicButton })
        addAndMakeVisible (b);

    session.device.addListener (this);
    deviceListConnection = juce::MidiDeviceListConnection::make ([this] { refreshPorts(); });
    refreshPorts();
    startTimerHz (20);
}

ConnectionBar::~ConnectionBar()
{
    session.device.removeListener (this);
}

void ConnectionBar::refreshPorts()
{
    inputs = juce::MidiInput::getAvailableDevices();
    outputs = juce::MidiOutput::getAvailableDevices();

    auto fill = [] (juce::ComboBox& box, const juce::Array<juce::MidiDeviceInfo>& list, const juce::String& current)
    {
        box.clear (juce::dontSendNotification);
        box.addItem ("<none>", 1);
        int selected = 1;
        for (int i = 0; i < list.size(); ++i)
        {
            box.addItem (list[i].name, i + 2);
            if (list[i].identifier == current) selected = i + 2;
        }
        box.setSelectedId (selected, juce::dontSendNotification);
    };
    fill (inBox, inputs, session.device.getInputId());
    fill (outBox, outputs, session.device.getOutputId());
    devBox.setSelectedId (session.device.getDeviceNumber() + 1, juce::dontSendNotification);
}

void ConnectionBar::sidPortsChanged()
{
    refreshPorts();
    liveButton.setToggleState (session.isLive(), juce::dontSendNotification);
}

void ConnectionBar::sidActivity (bool outgoing)
{
    (outgoing ? lastOut : lastIn) = juce::Time::getMillisecondCounter();
    repaint (outgoing ? outLed : inLed);
}

void ConnectionBar::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastIn < 300) repaint (inLed);
    if (now - lastOut < 300) repaint (outLed);
    if (liveButton.getToggleState() != session.isLive())
        liveButton.setToggleState (session.isLive(), juce::dontSendNotification);
}

void ConnectionBar::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::panelEdge);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    const auto now = juce::Time::getMillisecondCounter();
    auto led = [&] (juce::Rectangle<int> r, juce::uint32 last, juce::Colour on)
    {
        const bool lit = now - last < 150;
        g.setColour (lit ? on : colours::panelEdge);
        g.fillEllipse (r.toFloat());
    };
    led (inLed, lastIn, colours::good);
    led (outLed, lastOut, colours::accent2);
}

void ConnectionBar::resized()
{
    auto r = getLocalBounds().reduced (8, 7);
    inLabel.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (4);
    inBox.setBounds (r.removeFromLeft (170));
    inLed = r.removeFromLeft (16).withSizeKeepingCentre (8, 8);
    outLabel.setBounds (r.removeFromLeft (60));
    r.removeFromLeft (4);
    outBox.setBounds (r.removeFromLeft (170));
    outLed = r.removeFromLeft (16).withSizeKeepingCentre (8, 8);
    devLabel.setBounds (r.removeFromLeft (50));
    r.removeFromLeft (4);
    devBox.setBounds (r.removeFromLeft (100));
    r.removeFromLeft (8);
    pingButton.setBounds (r.removeFromLeft (50));

    panicButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (6);
    playButton.setBounds (r.removeFromRight (50));
    r.removeFromRight (12);
    sendButton.setBounds (r.removeFromRight (120));
    r.removeFromRight (6);
    getButton.setBounds (r.removeFromRight (115));
    r.removeFromRight (6);
    liveButton.setBounds (r.removeFromRight (50));
}
} // namespace ui
