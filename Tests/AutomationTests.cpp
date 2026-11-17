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

    // Knob drags reach the host as gestures on the control's slot.
    {
        struct GestureCount : juce::AudioProcessorParameter::Listener
        {
            int begins = 0, ends = 0;
            void parameterValueChanged (int, float) override {}
            void parameterGestureChanged (int, bool starting) override { ++(starting ? begins : ends); }
        } count;
        params[3]->addListener (&count);
        automation.beginGesture (0, "v1.pw");
        automation.endGesture (0, "v1.pw");
        automation.beginGesture (0, "lfo3.rate"); // not on a slot: no gesture
        automation.endGesture (0, "lfo3.rate");
        params[3]->removeListener (&count);
        CHECK (count.begins == 1 && count.ends == 1);
    }

    // Linked edits copy the parameter value, not the changed byte.
    {
        // Different pulsewidths whose high nibbles differ, then edit OSC1
        // with OSC 1-3 linked so only its low byte changes.
        session.setParam (L.get ("v1.pw"), 0x100);
        session.setParam (L.get ("v2.pw"), 0x300);
        session.setParam (L.get ("v3.pw"), 0x500);
        session.setParam (L.get ("v4.pw"), 0x700);
        session.setParam (L.get ("v1.pw"), 0x1a0, sid::sysex::wopt::Voices);
        CHECK (session.getParam (L.get ("v2.pw")) == 0x1a0);
        CHECK (session.getParam (L.get ("v3.pw")) == 0x1a0);
        CHECK (session.getParam (L.get ("v4.pw")) == 0x700); // right SID untouched

        // Attack and Decay share a byte: a linked Attack edit keeps each
        // voice's own Decay.
        session.setParam (L.get ("v1.decay"), 3);
        session.setParam (L.get ("v2.decay"), 9);
        session.setParam (L.get ("v1.attack"), 12, sid::sysex::wopt::Voices);
        CHECK (session.getParam (L.get ("v2.attack")) == 12);
        CHECK (session.getParam (L.get ("v2.decay")) == 9);
        CHECK (session.getParam (L.get ("v1.decay")) == 3);

        // Edit one voice: nothing else moves.
        session.setParam (L.get ("v1.pw"), 0x800);
        CHECK (session.getParam (L.get ("v2.pw")) == 0x1a0);

        // L/R link pairs OSC1 with OSC4 only.
        session.setParam (L.get ("v1.pw"), 0x234, sid::sysex::wopt::LeftRight);
        CHECK (session.getParam (L.get ("v4.pw")) == 0x234);
        CHECK (session.getParam (L.get ("v2.pw")) == 0x1a0);
    }

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
