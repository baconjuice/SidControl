#pragma once

// Controls bound to one patch parameter. They update themselves when the
// edit buffer changes and write through the Session (which sends live edits).

#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace ui
{
using WriteOptionFn = std::function<uint8_t()>;
using FormatFn = std::function<juce::String (int displayValue)>;

class ParamControl : public juce::Component,
                     protected Session::Listener
{
public:
    ParamControl (Session& s, const sid::ParamDef& d);
    ~ParamControl() override;

    virtual juce::Point<int> preferredSize() const = 0;
    void setWriteOption (WriteOptionFn f) { writeOption = std::move (f); }
    const sid::ParamDef& getDef() const { return def; }

protected:
    void editReplaced() override { refresh(); }
    void editChanged (const std::vector<uint16_t>& addresses) override;
    virtual void refresh() = 0;

    void write (int raw);
    int read() const { return session.getParam (def); }

    Session& session;
    const sid::ParamDef& def;
    WriteOptionFn writeOption;
};

class ParamKnob : public ParamControl
{
public:
    ParamKnob (Session& s, const sid::ParamDef& d, juce::String label = {}, FormatFn format = {});
    juce::Point<int> preferredSize() const override { return compact ? juce::Point<int> { 48, 50 } : juce::Point<int> { 60, 78 }; }
    void resized() override;
    void refresh() override;
    juce::Slider& getSlider() { return slider; }
    void setCompact (bool c) { compact = c; title.setVisible (! c); resized(); }

private:
    juce::Label title;
    juce::Slider slider;
    FormatFn formatter;
    bool compact = false;
};

class ParamToggle : public ParamControl
{
public:
    ParamToggle (Session& s, const sid::ParamDef& d, juce::String label = {});
    juce::Point<int> preferredSize() const override;
    void resized() override { button.setBounds (getLocalBounds()); }
    void refresh() override { button.setToggleState (read() != 0, juce::dontSendNotification); }

private:
    juce::TextButton button;
};

class ParamCombo : public ParamControl
{
public:
    ParamCombo (Session& s, const sid::ParamDef& d, juce::String label = {}, int width = 120,
                std::vector<std::string> customChoices = {});
    juce::Point<int> preferredSize() const override { return { width, compact ? 24 : 42 }; }
    void resized() override;
    void refresh() override;
    void setCompact (bool c) { compact = c; title.setVisible (! c); resized(); }

private:
    juce::Label title;
    juce::ComboBox box;
    int width;
    bool compact = false;
};

// Choices "N: name" for an assignable parameter number (knobs, wavetables).
std::vector<std::string> parameterNumberChoices (sid::Engine e);

// A titled box that lays out its controls left to right, wrapping as needed.
class ParamGroup : public juce::Component
{
public:
    ParamGroup (Session& s, const sid::ParamTable& t, juce::String title, WriteOptionFn wopt = {});

    ParamKnob& knob (const std::string& id, juce::String label = {}, FormatFn format = {});
    ParamToggle& toggle (const std::string& id, juce::String label = {});
    ParamCombo& combo (const std::string& id, juce::String label = {}, int width = 120,
                       std::vector<std::string> customChoices = {});
    void toggles (std::initializer_list<std::pair<std::string, juce::String>> idsAndLabels); // vertical stack
    void lineBreak();
    void gap (int width);

    int preferredHeight (int forWidth) const;
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String title;

private:
    struct Item
    {
        juce::Component* comp = nullptr;
        juce::Point<int> size;
        bool lineBreak = false;
    };
    int layout (int width, bool apply);
    void addItem (std::unique_ptr<juce::Component> c, juce::Point<int> size);

    Session& session;
    const sid::ParamTable& table;
    WriteOptionFn writeOption;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Item> items;
};
} // namespace ui
