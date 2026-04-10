#pragma once
#include <JuceHeader.h>

namespace Theme {
    // Backgrounds
    inline const juce::Colour BG_PRIMARY    {0xff0d0d14};
    inline const juce::Colour BG_SECONDARY  {0xff14141e};
    inline const juce::Colour BG_TERTIARY   {0xff1a1a28};

    // Grid
    inline const juce::Colour GRID_BG       {0xff111118};
    inline const juce::Colour GRID_LINE     {0xff3a3a50};
    inline const juce::Colour GRID_LINE_BEAT{0xff4a4a65};
    inline const juce::Colour STEP_HIGHLIGHT{0xff4fc3f7};

    // Text
    inline const juce::Colour FG_COLOR      {0xffe0e0e8};
    inline const juce::Colour FG_DIM        {0xff6a6a7a};
    inline const juce::Colour ACCENT        {0xff4fc3f7};
    inline const juce::Colour GREEN_ACCENT  {0xff00c853};
    inline const juce::Colour RED_ACCENT    {0xffff4444};

    // Border
    inline const juce::Colour BORDER        {0xff2a2a3e};

    // Dimensions
    inline constexpr int HEADER_H    = 24;
    inline constexpr int LABEL_W     = 54;
    inline constexpr int BASE_CELL_W = 36;
    inline constexpr int BASE_CELL_H = 28;
    inline constexpr int BAR_H       = 48;
    inline constexpr float FONT_SZ   = 12.0f;
}

// ============================================================================
// Dark LookAndFeel
// ============================================================================
class DarkLookAndFeel : public juce::LookAndFeel_V4 {
public:
    DarkLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId,   Theme::BG_PRIMARY);
        setColour(juce::TextButton::buttonColourId,            Theme::BG_TERTIARY);
        setColour(juce::TextButton::buttonOnColourId,          Theme::ACCENT.darker(0.3f));
        setColour(juce::TextButton::textColourOffId,           Theme::FG_COLOR);
        setColour(juce::TextButton::textColourOnId,            Theme::FG_COLOR);
        setColour(juce::ComboBox::backgroundColourId,          Theme::BG_TERTIARY);
        setColour(juce::ComboBox::textColourId,                Theme::FG_COLOR);
        setColour(juce::ComboBox::outlineColourId,             Theme::BORDER);
        setColour(juce::PopupMenu::backgroundColourId,         Theme::BG_SECONDARY);
        setColour(juce::PopupMenu::textColourId,               Theme::FG_COLOR);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::ACCENT.darker(0.4f));
        setColour(juce::Label::textColourId,                   Theme::FG_COLOR);
        setColour(juce::Slider::backgroundColourId,            Theme::BG_TERTIARY);
        setColour(juce::Slider::trackColourId,                 Theme::ACCENT);
        setColour(juce::Slider::thumbColourId,                 Theme::FG_COLOR);
        setColour(juce::ToggleButton::textColourId,            Theme::FG_COLOR);
        setColour(juce::ToggleButton::tickColourId,            Theme::ACCENT);
        setColour(juce::ScrollBar::thumbColourId,              Theme::BORDER);
    }
};
