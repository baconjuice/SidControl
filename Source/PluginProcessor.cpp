#include "PluginProcessor.h"
#include "PluginEditor.h"

SidControlProcessor::SidControlProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

SidControlProcessor::~SidControlProcessor()
{
    aliveFlag.reset();
}

bool SidControlProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SidControlProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Keyboard (standalone "Active MIDI Inputs") or DAW track -> MIDIbox SID.
    device.forwardMidi (midi);
    buffer.clear();
    midi.clear();
}

juce::AudioProcessorEditor* SidControlProcessor::createEditor()
{
    return new SidControlEditor (*this);
}

void SidControlProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto v = session.toValueTree();
    v.setProperty ("editorWidth", editorWidth, nullptr);
    v.setProperty ("editorHeight", editorHeight, nullptr);
    if (auto xml = v.createXml())
        copyXmlToBinary (*xml, destData);
}

void SidControlProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto v = juce::ValueTree::fromXml (*xml);
    editorWidth = v.getProperty ("editorWidth", editorWidth);
    editorHeight = v.getProperty ("editorHeight", editorHeight);

    // The session talks to UI and MIDI ports, so apply on the message thread.
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        session.fromValueTree (v);
        return;
    }
    juce::MessageManager::callAsync ([this, v, alive = std::weak_ptr<bool> (aliveFlag)]
    {
        if (! alive.expired())
            session.fromValueTree (v);
    });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SidControlProcessor();
}
