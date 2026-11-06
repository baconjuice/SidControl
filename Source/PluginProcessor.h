#pragma once

#include "device/SidDevice.h"
#include "model/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The processor makes no sound: the MIDIbox SID does. It exists so the editor
// can run standalone or inside a DAW, with its state saved in the project.
class SidControlProcessor : public juce::AudioProcessor
{
public:
    SidControlProcessor();
    ~SidControlProcessor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    SidDevice device;
    Session session { device };

    // Editor window size, remembered with the state.
    int editorWidth = 1440, editorHeight = 860;

private:
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidControlProcessor)
};
