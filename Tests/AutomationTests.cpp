// DAW automation slots: host -> patch, patch -> host, engines, state.

#include "model/Session.h"
#include "plugin/Automation.h"

#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct DummyProcessor : juce::AudioProcessor
{
    const juce::String getName() const override { return "test"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

static void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    SidDevice device;
    Session session (device);
    DummyProcessor proc;
    Automation automation (proc, session);
    auto& params = proc.getParameters();
    CHECK (params.size() == Automation::kSlots);

    // Every default resolves to a real parameter.
    for (int e = 0; e < 4; ++e)
        for (auto& a : Automation::defaultsFor (e))
            CHECK (a.paramId.empty() || sid::paramsFor ((sid::Engine) e).find (a.paramId) != nullptr);

    const auto& L = sid::leadParams();
    CHECK (params[0]->getName (100) == "1: Filter CutOff");
    CHECK (params[3]->getName (100) == "4: OSC Pulsewidth");

    // Host -> patch (slot 1 = cutoff, linked L/R).
    params[0]->setValue (1.0f);
    pump();
    CHECK (session.getParam (L.get ("fL.cutoff")) == 4095);
    CHECK (session.getParam (L.get ("fR.cutoff")) == 4095);
    params[0]->setValue (0.5f);
    pump();
    CHECK (std::abs (session.getParam (L.get ("fL.cutoff")) - 2048) <= 1);

    // Patch -> host.
    session.setParam (L.get ("fL.res"), 255);
    CHECK (params[1]->getValue() == 1.0f);
    CHECK (params[1]->getText (params[1]->getValue(), 20) == "255");

    // Assign a control to a slot, then remove it.
    automation.assign (4, 0, "lfo3.rate", 0);
    CHECK (automation.slotFor (0, "lfo3.rate") == 4);
    CHECK (params[4]->getName (100).contains ("LFO3"));
    automation.unassign (0, "lfo3.rate");
    CHECK (automation.slotFor (0, "lfo3.rate") == -1);
    CHECK (params[4]->getName (100) == "5: (unused)");

    // Engine switch renames the slots.
    session.loadIntoEditor (sid::Patch::makeInit (sid::Engine::Drum));
    CHECK (params[3]->getName (100).contains ("Tune"));
    params[3]->setValue (0.0f);
    pump();
    CHECK (session.getParam (sid::drumParams().get ("d1.tune")) == 0);

    // State round trip.
    auto state = automation.toValueTree();
    automation.resetToDefaults (0);
    automation.fromValueTree (state);
    CHECK (automation.slotFor (0, "lfo3.rate") == -1);
    CHECK (automation.slotFor (0, "v1.pw") == 3);

    std::printf (failures == 0 ? "automation tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
