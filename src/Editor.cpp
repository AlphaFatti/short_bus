// SPDX-License-Identifier: AGPL-3.0-only
#include "Editor.h"
#include "BusColours.h"
#include "PresetBrowser.h"
#include "ShortBusFontData.h"

#include <initializer_list>
#include <string_view>

namespace shortbus
{
using namespace juce;
using dsp::Stage;

namespace
{
const auto background = Colour(0xff121416);
const auto body = Colour(0xffe8a317);
const auto ink = Colour(0xff171715);
const auto glass = Colour(0xff2c3338);
const auto panel = Colour(0xff262b2f);
const auto door = Colour(0xff1b1e21);
const auto graph = Colour(0xff101214);
const auto edge = Colour(0xff434b51);
const auto white = Colour(0xfffff5e3);
const auto muted = Colour(0xffb3c0cb);
const auto yellow = Colour(0xffffd475);
const auto red = Colour(0xfff6877f);
constexpr int frameInset = 26;
constexpr int controlInset = 40;
constexpr int windowTop = 129;
constexpr int windowHeight = 50;
constexpr int doorTop = 185;
constexpr int inspectorTop = 196;
constexpr int optionTop = 226;
constexpr int bumperDepth = 70;
constexpr int sliderRowsBottom = 81;
constexpr int sliderRowHeight = 36;
constexpr float transferMinDb = -72.0f;
constexpr float transferMaxDb = 6.0f;
constexpr float transferSpanDb = transferMaxDb - transferMinDb;
constexpr float spectrumMinDb = -85.0f;
constexpr float meterFloorDb = -60.0f;
constexpr float stageMeterWidth = 28.0f;
constexpr float transferReadoutWidth = 120.0f;
constexpr float spectrumSideWidth = 29.0f;
constexpr float bandRangeInset = 6.0f;
constexpr float tonalTolerance = 4.0f;
constexpr int presetDoorWidth = 210;
constexpr int geqControlsDepth = 57;
constexpr int historyHeight = 58;
constexpr int historyGap = 6;
constexpr int historySpace = historyHeight + historyGap;
constexpr int minimumBusHeight = 494 + historySpace;
constexpr float geqRtaMinDb = -72.0f;
const std::array<Colour, dsp::bandCount> bandColours {
    Colour(0xfff0bf58), Colour(0xff73d6b6), Colour(0xff79bbea),
    Colour(0xffb7a7f6), Colour(0xfff090ab)
};
constexpr std::array<const char*, dsp::stageCount + 1> stageNames {
    "Expand / Lift", "5-Band", "Compressor", "Limiter", "GEQ", "RTA"
};
constexpr std::array<const char*, dsp::stageCount> inspectorNames {
    "Low-level dynamics", "5-band compressor", "Bus compressor", "Lookahead limiter", "31-band graphic EQ"
};

struct Control { const char* id; const char* label; };
constexpr std::array dynamicsControls {
    Control { "threshold", "Threshold (dBFS)" }, Control { "ratio", "Ratio (x:1)" },
    Control { "attack", "Attack (ms)" }, Control { "release", "Release (ms)" },
    Control { "knee", "Knee (dB)" }, Control { "range", "Range (dB)" }
};

void style(TextButton& button, bool toggle = false)
{
    button.setClickingTogglesState(toggle);
    button.setColour(TextButton::buttonColourId, panel);
    button.setColour(TextButton::buttonOnColourId, yellow);
    button.setColour(TextButton::textColourOffId, white);
    button.setColour(TextButton::textColourOnId, graph);
}

String signedDb(float db)
{
    return (db > 0.05f ? "+" : "") + String(std::abs(db) < 0.05f ? 0.0f : db, 1) + " dB";
}

String eqFrequency(float hz)
{
    if (hz < 1000.0f)
        return String(hz, std::floor(hz) < hz ? 1 : 0);
    return String(hz / 1000.0f, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + "k";
}

String fontKey(const String& family)
{
    return family.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

String clearviewFamily(const StringArray& families, std::initializer_list<const char*> preferred)
{
    for (const auto* key : preferred)
        for (const auto& family : families)
            if (fontKey(family) == key)
                return family;
    for (const auto& family : families)
        if (fontKey(family).startsWith("clearview"))
            return family;
    return {};
}

Typeface::Ptr installedTypeface(const String& family, bool bold)
{
    if (family.isEmpty())
        return {};
    const auto styles = Font::findAllTypefaceStyles(family);
    const auto preferred = bold ? "Bold" : "Regular";
    const auto style = styles.contains(preferred, true) || styles.isEmpty() ? String(preferred) : styles[0];
    return Font(FontOptions(family, style, 12.0f)).getTypefacePtr();
}

void label(Graphics& g, const Font& font, String text, Rectangle<int> bounds, Colour colour,
           Justification alignment = Justification::centredLeft)
{
    g.setFont(font);
    g.setColour(colour);
    g.drawFittedText(text, bounds, alignment, 1, 0.85f);
}

void drawWarningLamp(Graphics& g, Point<float> center, float radius, Colour lens, float glow = 0.0f)
{
    const auto ring = [center](float r) { return Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(center); };
    if (glow > 0.0f)
    {
        const auto haloRadius = radius * 1.82f;
        ColourGradient halo(Colour(0xffff3821).withAlpha(glow * 0.80f), center.x, center.y,
                            Colours::transparentBlack, center.x + haloRadius, center.y, true);
        halo.addColour(0.50, Colour(0xffff3020).withAlpha(glow * 0.57f));
        g.setGradientFill(halo);
        g.fillEllipse(ring(haloRadius));
    }

    g.setColour(Colour(0xff111315));
    g.fillEllipse(ring(radius));
    g.setColour(Colour(0xff596065).interpolatedWith(Colour(0xffff6a4c), glow * 0.8f));
    g.drawEllipse(ring(radius - 2.2f), 1.0f);
    const auto face = ring(radius - 4.4f);
    const auto top = lens.brighter(0.38f).interpolatedWith(Colour(0xffff9d58), glow);
    const auto bottom = lens.darker(0.58f).interpolatedWith(Colour(0xfff92d1f), glow);
    g.setGradientFill(ColourGradient(top, center.x, face.getY(), bottom, center.x, face.getBottom(), false));
    g.fillEllipse(face);
    const auto reflection = lens.brighter(0.85f).interpolatedWith(Colour(0xffffdf9b), glow);
    g.setColour(reflection.withAlpha(0.35f + glow * 0.5f));
    g.fillEllipse(face.withSizeKeepingCentre(face.getWidth() * 0.53f, face.getHeight() * 0.22f)
                      .translated(0.0f, -radius * 0.31f));
    g.setColour(Colours::black.withAlpha(0.16f * (1.0f - glow)));
    g.fillRoundedRectangle(face.reduced(face.getWidth() * 0.12f, face.getHeight() * 0.32f), 2.0f);
}

void drawMarkerLamp(Graphics& g, float x, bool running, float brake)
{
    constexpr float y = 23.5f;
    if (running)
    {
        const auto radius = 10.5f + brake * 5.0f;
        const auto glow = Colour(0xffff6931).interpolatedWith(Colour(0xffff2418), brake);
        ColourGradient halo(glow.withAlpha(0.36f + brake * 0.54f), x, y,
                            Colours::transparentBlack, x + radius, y, true);
        halo.addColour(0.50, glow.withAlpha(0.16f + brake * 0.45f));
        g.setGradientFill(halo);
        g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
    }

    g.setColour(Colour(0xff693818));
    g.fillRoundedRectangle(x - 8.0f, 19.0f, 16.0f, 9.0f, 2.7f);
    const auto top = running ? Colour(0xffffad67).interpolatedWith(Colour(0xffff8d60), brake) : Colour(0xffa6372e);
    const auto bottom = running ? Colour(0xffde4c2a).interpolatedWith(Colour(0xffff2115), brake) : Colour(0xff8c2e25);
    g.setGradientFill(ColourGradient(top, x, 20.0f, bottom, x, 25.5f, false));
    g.fillRoundedRectangle(x - 6.0f, 20.0f, 12.0f, 5.5f, 1.6f);
    g.setColour(running ? Colour(0xffffd78f).interpolatedWith(Colour(0xffffede0), brake) : Colour(0xffc66347));
    g.fillRoundedRectangle(x - 4.0f, 20.4f, 8.0f, 1.0f, 0.5f);
}

Path stopSign(Rectangle<float> bounds)
{
    const auto left = bounds.getX();
    const auto right = bounds.getRight();
    const auto top = bounds.getY();
    const auto bottom = bounds.getBottom();
    const auto corner = bounds.getWidth() * 0.293f;
    Path outline;
    outline.startNewSubPath(left + corner, top);
    outline.lineTo(right - corner, top);
    outline.lineTo(right, top + corner);
    outline.lineTo(right, bottom - corner);
    outline.lineTo(right - corner, bottom);
    outline.lineTo(left + corner, bottom);
    outline.lineTo(left, bottom - corner);
    outline.lineTo(left, top + corner);
    outline.closeSubPath();
    return outline;
}

float levelToY(Rectangle<float> area, float db, float floor = transferMinDb, float ceiling = transferMaxDb)
{
    return area.getBottom() - area.getHeight() * jlimit(0.0f, 1.0f, (db - floor) / (ceiling - floor));
}

float inputToX(Rectangle<float> area, float db)
{
    return area.getX() + area.getWidth() * (db - transferMinDb) / transferSpanDb;
}

float logFrequencyX(Rectangle<float> area, float hz)
{
    return area.getX() + area.getWidth() * std::log(hz / dsp::minHz) / std::log(dsp::maxHz / dsp::minHz);
}

float signedRange(const dsp::BandSettings& band)
{
    return band.lift ? band.dynamics.range : -band.dynamics.range;
}

Rectangle<float> rangeGrip(Rectangle<float> bar)
{
    return bar.removeFromRight(std::min(17.0f, bar.getWidth() * 0.40f));
}

void drawNode(Graphics& g, Point<float> position, Colour colour, bool selected)
{
    const auto size = selected ? 12.0f : 9.0f;
    const auto ring = Rectangle<float>(size, size).withCentre(position);
    g.setColour(graph);
    g.fillEllipse(ring.expanded(1.0f));
    g.setColour(colour);
    g.drawEllipse(ring, 1.4f);
    if (selected)
        g.fillEllipse(ring.reduced(3.0f));
}

void setGraphValue(RangedAudioParameter& parameter, float value)
{
    const auto normalised = parameter.convertTo0to1(value);
    if (std::abs(normalised - parameter.getValue()) > 1.0e-7f)
        parameter.setValueNotifyingHost(normalised);
}

} // namespace

Editor::BusLookAndFeel::BusLookAndFeel()
{
    const auto families = Font::findAllTypefaceNames();
    regular = installedTypeface(clearviewFamily(families,
        { "clearviewhwy4b", "clearviewhwy4w", "clearviewtext", "clearviewhwy", "clearview" }), false);
    bold = installedTypeface(clearviewFamily(families,
        { "clearviewhwy5b", "clearviewhwy5w", "clearviewtext", "clearviewhwy", "clearview" }), true);
    if (regular == nullptr)
        regular = Typeface::createSystemTypefaceFor(ShortBusFontData::overpassregular_otf,
                                                    ShortBusFontData::overpassregular_otfSize);
    if (bold == nullptr)
        bold = Typeface::createSystemTypefaceFor(ShortBusFontData::overpassbold_otf,
                                                 ShortBusFontData::overpassbold_otfSize);
    setColour(PopupMenu::backgroundColourId, panel);
    setColour(PopupMenu::textColourId, white);
    setColour(PopupMenu::highlightedBackgroundColourId, yellow);
    setColour(PopupMenu::highlightedTextColourId, ink);
}

Font Editor::BusLookAndFeel::font(float size, bool strong) const
{
    return Font(FontOptions(strong ? bold : regular).withHeight(size));
}

Font Editor::BusLookAndFeel::getTextButtonFont(TextButton&, int height)
{
    return font(jlimit(10.5f, 14.0f, static_cast<float>(height) * 0.53f), true);
}

Font Editor::BusLookAndFeel::getComboBoxFont(ComboBox&)
{
    return font(13.0f);
}

Font Editor::BusLookAndFeel::getLabelFont(Label& label)
{
    return font(label.getFont().getHeight(), label.getFont().isBold());
}

Font Editor::BusLookAndFeel::getPopupMenuFont()
{
    return font(14.0f);
}

void Editor::BusLookAndFeel::drawLinearSlider(Graphics& g, int x, int y, int width, int height,
                                             float position, float minimum, float maximum,
                                             Slider::SliderStyle style, Slider& slider)
{
    if (style != Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, position, minimum, maximum, style, slider);
        return;
    }
    const auto centre = static_cast<float>(x) + 0.5f * static_cast<float>(width);
    const auto zero = static_cast<float>(y) + 0.5f * static_cast<float>(height);
    const auto opacity = slider.isEnabled() ? 1.0f : 0.28f;
    g.setColour(edge.withAlpha(opacity));
    g.fillRoundedRectangle(centre - 1.0f, static_cast<float>(y), 2.0f, static_cast<float>(height), 1.0f);
    g.setColour(body.withAlpha(opacity));
    g.fillRect(centre - 1.0f, std::min(position, zero), 2.0f, std::abs(position - zero));
    const auto capWidth = std::min(18.0f, static_cast<float>(width) - 2.0f);
    const Rectangle<float> cap { centre - capWidth * 0.5f, position - 3.5f, capWidth, 7.0f };
    g.setColour((slider.isMouseOverOrDragging() || slider.hasKeyboardFocus(false) ? yellow : white).withAlpha(opacity));
    g.fillRoundedRectangle(cap, 1.7f);
    g.setColour(ink.withAlpha(opacity));
    g.fillRect(cap.reduced(2.0f, 3.0f));
}

void Editor::BusLookAndFeel::drawRotarySlider(Graphics& g, int x, int y, int width, int height,
                                             float position, float startAngle, float endAngle, Slider& slider)
{
    const auto centre = Rectangle<int>(x, y, width, height).getCentre().toFloat();
    const auto radius = 0.5f * static_cast<float>(std::min(width, height)) - 2.0f;
    const auto angle = startAngle + position * (endAngle - startAngle);
    Path scale;
    scale.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(edge);
    g.strokePath(scale, PathStrokeType(2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    if (position > 0.0f)
    {
        Path value;
        value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour(body);
        g.strokePath(value, PathStrokeType(2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    const auto face = Rectangle<float>((radius - 3.0f) * 2.0f, (radius - 3.0f) * 2.0f).withCentre(centre);
    const auto top = slider.isMouseOverOrDragging() ? bumperTop.brighter(0.22f) : bumperTop;
    g.setGradientFill(ColourGradient(top, centre.x, face.getY(), bumperBottom, centre.x, face.getBottom(), false));
    g.fillEllipse(face);
    g.setColour(slider.hasKeyboardFocus(false) ? yellow : bumperEdge);
    g.drawEllipse(face, 0.8f);
    g.setColour(yellow);
    g.drawLine({ centre.getPointOnCircumference(radius * 0.26f, angle),
                 centre.getPointOnCircumference(radius - 4.5f, angle) }, 1.6f);
}

void Editor::StageButton::paintButton(Graphics& g, bool hovered, bool pressed)
{
    const auto& skin = static_cast<const BusLookAndFeel&>(getLookAndFeel());
    const auto chosen = getToggleState();
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(ink);
    g.fillRoundedRectangle(bounds, 4.0f);
    const auto pane = bounds.reduced(2.5f);
    const auto tint = (chosen ? Colour(0xff353c40) : glass).brighter(hovered || pressed || dragging ? 0.12f : 0.0f);
    g.setGradientFill(ColourGradient(tint.brighter(0.18f), pane.getX(), pane.getY(),
                                    tint.darker(0.20f), pane.getX(), pane.getBottom(), false));
    g.fillRoundedRectangle(pane, 2.5f);
    g.setColour(chosen || hasKeyboardFocus(false) ? yellow : Colour(0xff566069));
    g.drawRoundedRectangle(pane, 2.5f, 1.0f);

    const auto area = getLocalBounds().reduced(7, 5);
    if (showPosition)
        label(g, skin.font(10.5f, true), "0" + String(position + 1), area.withWidth(20).withHeight(18),
              chosen ? yellow : muted);
    label(g, skin.font(12.5f, true), getName(), area.withTrimmedLeft(showPosition ? 18 : 2).withHeight(18), white);
    g.setColour(enabled ? yellow : edge);
    g.fillEllipse(static_cast<float>(area.getX() + 3), static_cast<float>(area.getY() + 25), 5.0f, 5.0f);
    const auto readout = dragging ? String("MOVE") : isAnalyzer ? String("MONITOR")
        : !enabled ? String("OFF") : showsGain ? signedDb(gain) : String("ON");
    label(g, skin.font(11.0f), readout, area.withTrimmedLeft(18).withY(23).withHeight(20),
          enabled ? yellow : muted);
}

void Editor::StageButton::mouseDown(const MouseEvent& event)
{
    Button::mouseDown(event);
    if (onReorder)
        onReorder(event, 0);
}

void Editor::StageButton::mouseDrag(const MouseEvent& event)
{
    Button::mouseDrag(event);
    if (onReorder)
        onReorder(event, 1);
}

void Editor::StageButton::mouseUp(const MouseEvent& event)
{
    if (onReorder)
        onReorder(event, 2);
    Button::mouseUp(event);
}

void Editor::PresetButton::paintButton(Graphics& g, bool hovered, bool pressed)
{
    const auto& skin = static_cast<const BusLookAndFeel&>(getLookAndFeel());
    const auto plate = getLocalBounds().toFloat().reduced(1.0f);
    const auto face = getToggleState() ? yellow : white;
    g.setGradientFill(ColourGradient(face.brighter(hovered || pressed ? 0.15f : 0.0f), plate.getX(), plate.getY(),
                                    face.darker(0.15f), plate.getX(), plate.getBottom(), false));
    g.fillRoundedRectangle(plate, 3.0f);
    g.setColour(Colour(0xff485155));
    g.drawRoundedRectangle(plate.reduced(0.6f), 3.0f, 1.2f);
    g.drawRoundedRectangle(plate.reduced(2.5f), 1.5f, 0.7f);
    for (const auto x : { plate.getX() + 5.0f, plate.getRight() - 5.0f })
        for (const auto y : { plate.getY() + 4.5f, plate.getBottom() - 4.5f })
            g.fillEllipse(x - 1.0f, y - 1.0f, 2.0f, 2.0f);
    label(g, skin.font(7.5f, true), "SHORT BUS", getLocalBounds().withY(4).withHeight(9), ink, Justification::centred);
    label(g, skin.font(12.5f, true), "PRESETS", getLocalBounds().withY(12).withHeight(18), ink, Justification::centred);
}

Editor::BypassButton::BypassButton() : Button("Bypass")
{
    setTitle("Bypass all processing");
    setClickingTogglesState(true);
}

bool Editor::BypassButton::hitTest(int x, int y)
{
    return stopSign(getLocalBounds().toFloat().reduced(1.5f))
        .contains(static_cast<float>(x), static_cast<float>(y));
}

void Editor::BypassButton::paintButton(Graphics& g, bool hovered, bool pressed)
{
    const auto& skin = static_cast<const BusLookAndFeel&>(getLookAndFeel());
    const auto bounds = getLocalBounds().toFloat().reduced(1.5f);
    const auto outline = stopSign(bounds);
    const auto active = getToggleState();
    g.setColour((active ? Colour(0xffdb332e) : Colour(0xff9a2a27)).brighter(hovered || pressed ? 0.15f : 0.0f));
    g.fillPath(outline);
    g.setColour(hasKeyboardFocus(false) ? yellow : white);
    g.strokePath(outline, PathStrokeType(1.5f));
    auto textArea = getLocalBounds().withSizeKeepingCentre(getWidth() - 5, 16);
    if (active)
        textArea.translate(0, -4);
    label(g, skin.font(12.0f, true), "BYPASS", textArea, white, Justification::centred);
    if (active)
        label(g, skin.font(8.5f, true), "ON", textArea.translated(0, 12), white, Justification::centred);
}

Editor::Editor(Processor& owner)
    : AudioProcessorEditor(owner), processor(owner), settings(owner.currentSettings()), order(owner.tileOrder())
{
    setLookAndFeel(&theme);
    levels.fill(spectrumMinDb);
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        auto& button = stages[i];
        button.setName(stageNames[i]);
        button.isAnalyzer = i == tonalTile;
        button.showsGain = i != dsp::index(Stage::geq);
        button.enabled = button.isAnalyzer;
        button.setTriggeredOnMouseDown(true);
        button.setMouseCursor(MouseCursor::PointingHandCursor);
        button.onReorder = [this, i](const MouseEvent& event, int phase) { reorderTile(i, event, phase); };
        button.onClick = [this, i] { selectTile(i); };
        addAndMakeVisible(button);
    }
    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        auto& button = bands[i];
        button.setButtonText("Band " + String(static_cast<int>(i + 1)));
        style(button);
        button.onClick = [this, i]
        {
            selectedBand = static_cast<int>(i);
            bindControls();
        };
        addChildComponent(button);
    }
    addAndMakeVisible(bypass);
    addAndMakeVisible(presetsButton);
    presetsButton.setTitle("Open preset door");
    presetsButton.onClick = [this] { togglePresets(); };
    for (auto* button : { &stageOn, &bandOn, &earlier, &later })
    {
        style(*button, button != &earlier && button != &later);
        addAndMakeVisible(*button);
    }
    earlier.setTitle("Move selected tile earlier");
    later.setTitle("Move selected tile later");
    earlier.onClick = [this] { moveStage(-1); };
    later.onClick = [this] { moveStage(1); };
    mode.setTitle("Low-level mode");
    targetChoice.setTitle("Tonal balance target");
    tonalMode.setTitle("Tonal balance view");
    compressorModeBox.setTitle("Compressor mode");
    compressorModeBox.setTooltip("Clean uses the original peak compressor. Opto has RMS sensing and optical memory. FET has fast feedback compression.");
    for (auto* choice : { &mode, &targetChoice, &tonalMode, &compressorModeBox })
    {
        choice->setColour(ComboBox::backgroundColourId, panel);
        choice->setColour(ComboBox::textColourId, white);
        choice->setColour(ComboBox::outlineColourId, edge);
        addChildComponent(*choice);
    }
    mode.addItem("Downward expansion", 1);
    mode.addItem("Upward compression", 2);
    targetChoice.addItemList({ "Balanced", "Bass-forward", "Speech", "Captured" }, 1);
    tonalMode.addItemList({ "Fine", "Broad" }, 1);
    compressorModeBox.addItemList({ "Clean", "Opto", "FET" }, 1);
    style(captureReference);
    captureReference.setTitle("Capture the average at this tap");
    captureReference.setEnabled(false);
    captureReference.onClick = [this]
    {
        if (lastTonalFrame <= 0.0 || tonalKey != currentTonalKey())
            return;
        const auto count = dsp::tonalBandsAtRate(processor.displayRate());
        processor.setTonalReference(dsp::TonalReference { dsp::normalizedTone(tonalAverage, count), count });
        targetChoice.setSelectedId(4, sendNotificationSync);
        repaint();
    };
    addChildComponent(captureReference);
    modeLink = std::make_unique<ChoiceLink>(processor.state, param::mode, mode);
    targetLink = std::make_unique<ChoiceLink>(processor.state, param::rtaTarget, targetChoice);
    tonalModeLink = std::make_unique<ChoiceLink>(processor.state, param::rtaView, tonalMode);
    compressorModeLink = std::make_unique<ChoiceLink>(processor.state, param::compMode, compressorModeBox);
    bypassLink = std::make_unique<ButtonLink>(processor.state, param::bypass, bypass);
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        auto& slider = sliders[i];
        slider.setSliderStyle(Slider::LinearHorizontal);
        slider.setTextBoxStyle(Slider::TextBoxRight, false, 51, 24);
        slider.setColour(Slider::trackColourId, body);
        slider.setColour(Slider::thumbColourId, yellow);
        slider.setColour(Slider::backgroundColourId, edge);
        slider.setColour(Slider::textBoxTextColourId, white);
        slider.setColour(Slider::textBoxBackgroundColourId, graph);
        slider.setColour(Slider::textBoxOutlineColourId, edge);
        labels[i].setColour(Label::textColourId, muted);
        labels[i].setFont(theme.font(11.5f, true));
        addChildComponent(slider);
        addChildComponent(labels[i]);
    }
    for (auto* dial : { &rtaSensitivity, &saturationDial })
    {
        dial->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        dial->setTextBoxStyle(Slider::TextBoxRight, false, 54, 22);
        dial->setColour(Slider::rotarySliderFillColourId, body);
        dial->setColour(Slider::thumbColourId, yellow);
        dial->setColour(Slider::textBoxTextColourId, white);
        dial->setColour(Slider::textBoxBackgroundColourId, graph);
        dial->setColour(Slider::textBoxOutlineColourId, edge);
        addChildComponent(*dial);
    }
    rtaSensitivity.setTitle("RTA sensitivity");
    rtaSensitivity.setTextBoxStyle(Slider::TextBoxRight, false, 65, 22);
    rtaSensitivity.setTooltip("Minimum RMS level at this tap. Raise the dBFS floor to hide idle noise.");
    rtaSensitivity.setTextValueSuffix(" dBFS");
    rtaSensitivity.setDoubleClickReturnValue(true, dsp::defaultRtaFloorDb);
    sensitivityLink = std::make_unique<SliderLink>(processor.state, param::rtaFloor, rtaSensitivity);
    saturationDial.setTitle("Saturation");
    saturationDial.setTooltip("Soft clipping strength after the EQ filters.");
    saturationDial.setTextValueSuffix(" %");
    saturationDial.setDoubleClickReturnValue(true, 0.0);
    saturationLink = std::make_unique<SliderLink>(processor.state, param::saturation, saturationDial);
    for (std::size_t i = 0; i < geqFaders.size(); ++i)
    {
        auto& fader = geqFaders[i];
        fader.setTitle(eqFrequency(dsp::geqFrequencies[i]) + " EQ gain");
        fader.setSliderStyle(Slider::LinearVertical);
        fader.setTextBoxStyle(Slider::NoTextBox, true, 0, 0);
        fader.setTextValueSuffix(" dB");
        fader.setDoubleClickReturnValue(true, 0.0);
        fader.setPopupDisplayEnabled(true, true, this, 800);
        fader.setScrollWheelEnabled(false);
        addChildComponent(fader);
        geqLinks[i] = std::make_unique<SliderLink>(processor.state, param::geqGain(i), fader);
    }
    style(flatEq);
    flatEq.setTooltip("Return all 31 EQ bands to 0 dB.");
    flatEq.onClick = [this]
    {
        for (auto& fader : geqFaders)
            fader.setValue(0.0, sendNotificationSync);
    };
    addChildComponent(flatEq);

    setResizable(true, true);
    setResizeLimits(568, minimumBusHeight, 1040, 820);
    setSize(636, 548 + historySpace);
    selectTile(dsp::index(Stage::lowLevel));
    processor.setMeterHistoryActive(true);
    processor.consumeOutputPeakEvent();
    startTimerHz(24);
}

Editor::~Editor()
{
    stopTimer();
    processor.spectrumRequested.store(Processor::SpectrumView::none, std::memory_order_relaxed);
    processor.setMeterHistoryActive(false);
    finishGraphDrag();
    setLookAndFeel(nullptr);
}

void Editor::selectTile(std::size_t tile)
{
    selectedTile = tile;
    if (!tonalSelected())
        selected = static_cast<Stage>(tile);
    for (std::size_t i = 0; i < stages.size(); ++i)
        stages[i].setToggleState(i == selectedTile, dontSendNotification);
    const auto spectrum = tonalSelected() ? Processor::SpectrumView::tonal
        : selected == Stage::multiband ? Processor::SpectrumView::output
        : geqSelected() ? Processor::SpectrumView::geq : Processor::SpectrumView::none;
    processor.discardSpectrum();
    processor.spectrumRequested.store(spectrum, std::memory_order_relaxed);
    tonalKey = 0;
    lastTonalFrame = 0.0;
    geqSpectrumKey = 0;
    lastGeqFrame = 0.0;
    geqSpectrum.fill(geqRtaMinDb);
    captureReference.setEnabled(false);
    levels.fill(spectrumMinDb);
    applyOrder();
    bindControls();
    if (presetBrowser != nullptr)
        presetBrowser->setScope(presetScope());
}

PresetScope Editor::presetScope() const
{
    if (tonalSelected())
        return PresetScope::rta;
    if (geqSelected())
        return PresetScope::geq;
    return static_cast<PresetScope>(dsp::index(selected));
}

void Editor::bindControls()
{
    finishGraphDrag();
    setMouseCursor(MouseCursor::NormalCursor);
    refreshSettings();
    const auto isTonal = tonalSelected();
    boundCompressorMode = settings.compressorMode;
    const auto isMulti = !isTonal && selected == Stage::multiband;
    compressorModeBox.setVisible(!isTonal && selected == Stage::compressor);
    mode.setVisible(!isTonal && selected == Stage::lowLevel);
    for (auto* control : std::array<Component*, 4> { &targetChoice, &tonalMode, &captureReference, &rtaSensitivity })
        control->setVisible(isTonal);
    const auto isGeq = geqSelected();
    saturationDial.setVisible(isGeq);
    flatEq.setVisible(isGeq);
    for (auto& fader : geqFaders)
        fader.setVisible(isGeq);
    const auto stagePrefix = param::stage(selected);
    const auto bandPrefix = param::band(selectedBand);
    stageLink.reset();
    stageOn.setVisible(!isTonal);
    if (!isTonal)
        stageLink = std::make_unique<ButtonLink>(processor.state, param::id(stagePrefix, "enabled"), stageOn);
    bandLink.reset();
    bandOn.setVisible(isMulti);
    if (isMulti)
        bandLink = std::make_unique<ButtonLink>(processor.state, param::id(bandPrefix, "enabled"), bandOn);
    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        bands[i].setVisible(isMulti);
        bands[i].setToggleState(i == static_cast<std::size_t>(selectedBand), dontSendNotification);
    }
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        sliderLinks[i].reset();
        sliders[i].setVisible(false);
        labels[i].setVisible(false);
    }
    if (isTonal || isGeq)
    {
        resized();
        repaint();
        return;
    }

    std::size_t slot = 0;
    const auto bind = [&](const String& prefix, Control control)
    {
        const auto timing = std::string_view(control.id) == "attack" || std::string_view(control.id) == "release";
        const auto characterTiming = selected == Stage::compressor && timing && settings.compressorMode != dsp::CompressorMode::clean;
        const auto timingPrefix = settings.compressorMode == dsp::CompressorMode::opto ? "opto_" : "fet_";
        const auto id = prefix + "_" + (characterTiming ? String(timingPrefix) : String()) + control.id;
        const auto* title = characterTiming && settings.compressorMode == dsp::CompressorMode::opto
            && std::string_view(control.id) == "release" ? "Fast release (ms)" : control.label;
        sliders[slot].setTitle(title);
        labels[slot].setText(title, dontSendNotification);
        sliderLinks[slot] = std::make_unique<SliderLink>(processor.state, id, sliders[slot]);
        sliders[slot].setVisible(true);
        labels[slot++].setVisible(true);
    };
    if (selected == Stage::limiter)
    {
        bind(stagePrefix, { "ceiling", "Ceiling (dBFS)" });
        bind(stagePrefix, { "drive", "Drive (dB)" });
        bind(stagePrefix, { "release", "Release (ms)" });
    }
    else
    {
        if (isMulti)
        {
            bind(bandPrefix, { "low_hz", "Low edge (Hz)" });
            bind(bandPrefix, { "high_hz", "High edge (Hz)" });
            limitFrequencySlider(sliders[0], dsp::BandEdge::low);
            limitFrequencySlider(sliders[1], dsp::BandEdge::high);
        }
        for (const auto& control : dynamicsControls)
            bind(isMulti ? bandPrefix : stagePrefix, control);
        if (selected == Stage::compressor)
            bind(stagePrefix, { "makeup", "Makeup (dB)" });
    }
    if (isMulti)
        refreshBandControls();
    resized();
    repaint();
}

void Editor::refreshSettings()
{
    settings = processor.currentSettings();
    bandSpans = dsp::orderedBandSpans(settings.bands, processor.displayRate());
}

float Editor::limitFrequency(std::size_t band, dsp::BandEdge bandEdge, float requested) const
{
    const auto rate = processor.displayRate();
    const auto spans = dsp::orderedBandSpans(processor.currentSettings().bands, rate);
    return dsp::constrainBandFrequency(spans, band, bandEdge, requested, rate);
}

void Editor::limitFrequencySlider(Slider& slider, dsp::BandEdge bandEdge)
{
    const auto id = param::id(param::band(selectedBand), bandEdge == dsp::BandEdge::low ? "low_hz" : "high_hz");
    const auto range = processor.state.getParameter(id)->getNormalisableRange();
    const auto band = static_cast<std::size_t>(selectedBand);
    NormalisableRange<double> limited { range.start, range.end,
        [range](double, double, double v) { return range.convertFrom0to1(static_cast<float>(v)); },
        [range](double, double, double v) { return range.convertTo0to1(static_cast<float>(v)); },
        [this, band, bandEdge](double, double, double v) { return limitFrequency(band, bandEdge, static_cast<float>(v)); }
    };
    limited.interval = range.interval;
    limited.skew = range.skew;
    slider.setNormalisableRange(std::move(limited));
}

void Editor::refreshBandControls()
{
    const auto span = edges(selectedBand);
    sliders[0].setValue(span.low, dontSendNotification);
    sliders[1].setValue(span.high, dontSendNotification);
    const auto* rangeLabel = settings.bands[static_cast<std::size_t>(selectedBand)].lift
        ? "Lift range (dB)" : "Cut range (dB)";
    if (labels.back().getText() != rangeLabel)
    {
        labels.back().setText(rangeLabel, dontSendNotification);
        sliders.back().setTitle(rangeLabel);
    }
}

void Editor::moveStage(int direction)
{
    auto next = order;
    const auto position = static_cast<int>(std::find(next.begin(), next.end(), selectedTile) - next.begin());
    const auto destination = position + direction;
    if (destination < 0 || destination >= static_cast<int>(next.size()))
        return;
    std::swap(next[static_cast<std::size_t>(position)], next[static_cast<std::size_t>(destination)]);
    processor.setTileOrder(next);
    applyOrder();
    resized();
    repaint();
}

void Editor::reorderTile(std::size_t tile, const MouseEvent& event, int phase)
{
    if (phase == 0 && event.mods.isLeftButtonDown())
    {
        finishGraphDrag();
        tileDrag = TileDrag { tile, order, order };
        return;
    }
    if (!tileDrag || tileDrag->tile != tile)
        return;
    if (phase == 2)
    {
        finishTileDrag(true);
        return;
    }
    if (event.getDistanceFromDragStart() < 5)
        return;
    auto& drag = *tileDrag;
    drag.active = true;
    stages[tile].dragging = true;
    stages[tile].setMouseCursor(MouseCursor::DraggingHandCursor);
    const auto point = event.getEventRelativeTo(this).position;
    const auto strip = busBounds().reduced(frameInset, 0).withY(windowTop).withHeight(windowHeight).toFloat();
    drag.valid = strip.expanded(14.0f).contains(point);
    drag.preview = drag.origin;
    if (drag.valid)
    {
        const auto source = std::find(drag.preview.begin(), drag.preview.end(), tile);
        const auto slot = jlimit(0, static_cast<int>(tileCount - 1),
                                static_cast<int>((point.x - strip.getX()) * static_cast<float>(tileCount) / strip.getWidth()));
        const auto destination = drag.preview.begin() + slot;
        if (source < destination)
            std::rotate(source, source + 1, destination + 1);
        else if (source > destination)
            std::rotate(destination, source, source + 1);
    }
    resized();
    repaint();
}

void Editor::finishTileDrag(bool commit)
{
    if (!tileDrag)
        return;
    const auto drag = *tileDrag;
    tileDrag.reset();
    stages[drag.tile].dragging = false;
    stages[drag.tile].setMouseCursor(MouseCursor::PointingHandCursor);
    if (commit && drag.active && drag.valid && processor.tileOrder() == drag.origin)
        processor.setTileOrder(drag.preview);
    applyOrder();
    resized();
    repaint();
}

Rectangle<int> Editor::busBounds() const
{
    return getLocalBounds().withTrimmedRight(presetDoorAllocated ? presetDoorWidth : 0);
}

void Editor::togglePresets()
{
    presetDoorTarget = !presetDoorTarget;
    presetsButton.setToggleState(presetDoorTarget, dontSendNotification);
    if (presetDoorTarget)
    {
        if (presetBrowser == nullptr)
        {
            presetBrowser = std::make_unique<PresetBrowser>(processor);
            addAndMakeVisible(*presetBrowser);
        }
        presetBrowser->setScope(presetScope());
        presetBrowser->setVisible(true);
        if (!presetDoorAllocated)
        {
            const auto closedWidth = getWidth();
            presetDoorAllocated = true;
            setResizeLimits(568 + presetDoorWidth, minimumBusHeight, 1040 + presetDoorWidth, 820);
            setSize(closedWidth + presetDoorWidth, getHeight());
        }
    }
    resized();
    repaint();
}

void Editor::animatePresetDoor(double elapsedMs)
{
    if (!presetDoorAllocated)
        return;
    presetDoorProgress = jlimit(0.0f, 1.0f, presetDoorProgress
        + static_cast<float>(elapsedMs / 180.0) * (presetDoorTarget ? 1.0f : -1.0f));
    if (presetDoorProgress <= 0.0f && !presetDoorTarget)
    {
        const auto openWidth = getWidth();
        presetBrowser->setVisible(false);
        presetDoorAllocated = false;
        setResizeLimits(568, minimumBusHeight, 1040, 820);
        setSize(openWidth - presetDoorWidth, getHeight());
    }
    resized();
}

void Editor::applyOrder()
{
    refreshSettings();
    order = processor.tileOrder();
    if (tileDrag && order != tileDrag->origin)
    {
        stages[tileDrag->tile].dragging = false;
        stages[tileDrag->tile].setMouseCursor(MouseCursor::PointingHandCursor);
        tileDrag.reset();
    }
    earlier.setEnabled(order.front() != selectedTile);
    later.setEnabled(order.back() != selectedTile);
    const auto& displayed = tileDrag && tileDrag->active ? tileDrag->preview : order;
    for (std::size_t i = 0; i < displayed.size(); ++i)
        stages[displayed[i]].position = static_cast<int>(i);
}

void Editor::resized()
{
    const auto content = busBounds().reduced(controlInset, 0);
    bypass.setBounds(busBounds().getWidth() - 82, getHeight() - bumperDepth + 4, 56, 56);
    presetsButton.setBounds(busBounds().getWidth() - 171, getHeight() - bumperDepth + 14, 81, 36);
    auto strip = busBounds().reduced(frameInset, 0).withY(windowTop).withHeight(windowHeight);
    const auto showPositions = strip.getWidth() >= static_cast<int>(tileCount) * 104;
    const auto& displayed = tileDrag && tileDrag->active ? tileDrag->preview : order;
    for (std::size_t i = 0; i < displayed.size(); ++i)
    {
        const auto slot = strip.removeFromLeft(strip.getWidth() / static_cast<int>(order.size() - i));
        stages[displayed[i]].position = static_cast<int>(i);
        stages[displayed[i]].showPosition = showPositions;
        stages[displayed[i]].setBounds(slot.withTrimmedRight(i + 1 < order.size() ? 6 : 0));
    }
    if (presetBrowser != nullptr && presetDoorAllocated)
    {
        presetBrowser->setBounds(busBounds().getWidth() - 9, windowTop, presetDoorWidth,
                                 getHeight() - windowTop - bumperDepth - 4);
        const auto fraction = presetDoorProgress * presetDoorProgress * (3.0f - 2.0f * presetDoorProgress);
        presetBrowser->setTransform(AffineTransform::scale(std::max(0.001f, fraction), 1.0f,
                                                         static_cast<float>(presetBrowser->getX()), 0.0f));
        presetBrowser->setEnabled(presetDoorTarget && presetDoorProgress >= 1.0f);
    }
    auto title = content.withY(inspectorTop).withHeight(24);
    later.setBounds(title.removeFromRight(27));
    title.removeFromRight(3);
    earlier.setBounds(title.removeFromRight(27));
    title.removeFromRight(45);
    stageOn.setBounds(title.removeFromRight(72));

    auto options = content.withY(optionTop).withHeight(22);
    auto tonalOptions = options.withTrimmedLeft(46);
    targetChoice.setBounds(tonalOptions.removeFromLeft(118));
    tonalOptions.removeFromLeft(6);
    tonalMode.setBounds(tonalOptions.removeFromLeft(67));
    tonalOptions.removeFromLeft(6);
    captureReference.setBounds(tonalOptions.removeFromLeft(82));
    rtaSensitivity.setBounds(tonalOptions.removeFromRight(94).withY(optionTop - 2).withHeight(26));
    mode.setBounds(options.withWidth(167));
    bandOn.setBounds(options.removeFromRight(72));
    for (auto& band : bands)
        band.setBounds(options.removeFromLeft(59).withTrimmedRight(4));

    const auto controlsTop = getHeight() - sliderRowsBottom - sliderRowHeight * 2;
    auto controls = content.withY(controlsTop).withHeight(sliderRowHeight * 2);
    for (std::size_t row = 0; row < 2; ++row)
    {
        auto line = controls.removeFromTop(sliderRowHeight);
        for (std::size_t column = 0; column < 4; ++column)
        {
            const auto cell = line.removeFromLeft(line.getWidth() / static_cast<int>(4 - column)).reduced(3, 0);
            const auto index = row * 4 + column;
            labels[index].setBounds(cell.withHeight(14));
            sliders[index].setBounds(cell.withTrimmedTop(14).withHeight(22));
        }
    }
    auto modeArea = plot().withLeft(transferArea().getRight() + 8.0f + 3.0f * stageMeterWidth).toNearestInt().reduced(8, 9);
    compressorModeBox.setBounds(modeArea.removeFromBottom(24));
    if (geqSelected())
    {
        auto bank = plot().toNearestInt().withTrimmedLeft(30).withTrimmedRight(28)
            .withTrimmedTop(8).withTrimmedBottom(29);
        for (std::size_t i = 0; i < geqFaders.size(); ++i)
        {
            const auto width = bank.getWidth() / static_cast<int>(geqFaders.size() - i);
            geqFaders[i].setBounds(bank.removeFromLeft(width).reduced(1, 0));
        }
        const auto controlsArea = geqControls();
        saturationDial.setBounds(controlsArea.withSizeKeepingCentre(166, 43).withTrimmedLeft(70));
        flatEq.setBounds(controlsArea.withTrimmedLeft(controlsArea.getWidth() - 74).withSizeKeepingCentre(74, 24));
    }
}

Rectangle<float> Editor::plot() const
{
    if (tonalSelected())
        return busBounds().reduced(controlInset, 0).withTrimmedTop(optionTop + 27)
            .withTrimmedBottom(bumperDepth + 20 + historySpace).toFloat();
    if (geqSelected())
        return busBounds().reduced(controlInset, 0).withTrimmedTop(optionTop)
            .withTrimmedBottom(bumperDepth + geqControlsDepth + historySpace).toFloat();
    const auto top = selected == Stage::multiband || selected == Stage::lowLevel ? optionTop + 27 : optionTop;
    return busBounds().reduced(controlInset, 0).withTrimmedTop(top)
        .withTrimmedBottom(sliderRowsBottom + sliderRowHeight * 2 + 5 + historySpace).toFloat();
}

Rectangle<float> Editor::historyBounds() const
{
    const auto bounds = plot();
    return bounds.withY(bounds.getBottom() + historyGap).withHeight(historyHeight);
}

Rectangle<int> Editor::geqControls() const
{
    const auto bounds = historyBounds().toNearestInt();
    return bounds.withY(bounds.getBottom() + 5).withHeight(43);
}

Rectangle<float> Editor::transferArea() const
{
    return plot().withTrimmedRight(3.0f * stageMeterWidth + transferReadoutWidth + 8.0f)
        .withTrimmedLeft(22.0f).reduced(0.0f, 18.0f);
}

Rectangle<float> Editor::spectrumArea() const
{
    return plot().reduced(spectrumSideWidth, 0.0f).withTrimmedBottom(19.0f).withTrimmedTop(17.0f);
}

void Editor::drawBus(Graphics& g)
{
    const auto width = static_cast<float>(busBounds().getWidth());
    const auto middle = width * 0.5f;
    const auto floor = static_cast<float>(getHeight() - bumperDepth + 5);
    const auto left = 10.0f;
    const auto right = width - left;

    Path shell;
    shell.startNewSubPath(left, floor);
    shell.lineTo(left, 119.0f);
    shell.cubicTo(left, 74.0f, 25.0f, 54.0f, 65.0f, 40.0f);
    shell.cubicTo(middle - 132.0f, 16.0f, middle - 68.0f, 8.0f, middle, 8.0f);
    shell.cubicTo(middle + 68.0f, 8.0f, middle + 132.0f, 16.0f, width - 65.0f, 40.0f);
    shell.cubicTo(width - 25.0f, 54.0f, right, 74.0f, right, 119.0f);
    shell.lineTo(right, floor);
    shell.closeSubPath();
    g.setColour(body);
    g.fillPath(shell);
    g.setColour(Colour(0xffffc64e));
    g.strokePath(shell, PathStrokeType(1.2f));

    const auto signWidth = width * 0.46f;
    {
        const Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(shell);
        const auto roofHeight = static_cast<float>(windowTop - 7);
        g.setGradientFill(ColourGradient(Colour(0xff515457), middle, 20.0f,
                                        Colour(0xff26292b), middle, roofHeight, false));
        g.fillRect(left, 8.0f, middle - signWidth * 0.5f - left, roofHeight - 8.0f);
        g.fillRect(middle + signWidth * 0.5f, 8.0f, right - middle - signWidth * 0.5f, roofHeight - 8.0f);
        g.setGradientFill(ColourGradient(body.brighter(0.27f), middle, 8.0f,
                                        body, middle, roofHeight, false));
        g.fillRect(middle - signWidth * 0.5f, 8.0f, signWidth, roofHeight - 8.0f);
        g.setColour(Colour(0xff9d6814));
        g.fillRect(left, roofHeight - 4.0f, right - left, 2.0f);
        g.setColour(Colour(0xfff7be48));
        g.fillRect(left, roofHeight - 2.0f, right - left, 2.0f);
    }

    const auto lampRadius = jlimit(17.0f, 26.0f, width * 0.035f);
    for (auto offset : { width * 0.16f, width * 0.083f })
    {
        const auto outer = offset > width * 0.1f;
        const auto lens = outer ? Colour(0xff8f322c) : Colour(0xff97501b);
        drawWarningLamp(g, { middle - signWidth * 0.5f + width * 0.025f - offset, 87.0f },
                        lampRadius, lens, outer ? lampGlow[0] : 0.0f);
        drawWarningLamp(g, { middle + signWidth * 0.5f - width * 0.025f + offset, 87.0f },
                        lampRadius, lens, outer ? lampGlow[1] : 0.0f);
    }
    const auto running = !processor.isBypassed();
    const auto brakeFadeMs = std::max(0.0, lastLampTick - brakeHoldUntil);
    const auto brake = running && brakeHoldUntil > 0.0 && brakeFadeMs < 360.0
        ? static_cast<float>(std::exp(-brakeFadeMs / 72.0)) : 0.0f;
    for (auto offset : { -23.0f, 0.0f, 23.0f })
        drawMarkerLamp(g, middle + offset, running, brake);

    for (const auto railX : { left + 7.0f, right - 7.0f })
    {
        g.setColour(Colour(0xffaa731c));
        g.drawVerticalLine(roundToInt(railX), 127.0f, floor - 7.0f);
        for (auto y = 141.0f; y < floor - 8.0f; y += 23.0f)
        {
            g.fillEllipse(railX - 1.3f, y, 2.6f, 2.6f);
            g.setColour(Colour(0xffffc654));
            g.fillEllipse(railX - 0.9f, y + 0.3f, 1.3f, 1.3f);
            g.setColour(Colour(0xffaa731c));
        }
    }

    const auto panelBounds = busBounds().reduced(frameInset, 0).withTrimmedTop(doorTop)
        .withTrimmedBottom(bumperDepth + 5).toFloat();
    g.setColour(ink);
    g.fillRoundedRectangle(panelBounds, 5.0f);
    g.setColour(door);
    g.fillRoundedRectangle(panelBounds.reduced(2.0f), 3.0f);
    g.setColour(Colour(0xff353a3d));
    g.drawRoundedRectangle(panelBounds.reduced(2.0f), 3.0f, 1.0f);
}

void Editor::drawPeakMeter(Graphics& g, const char* name, float peak, int x, int y)
{
    const auto db = dsp::dbFromGain(peak);
    label(g, theme.font(10.5f, true), name, { x, y, 26, 12 }, muted);
    const Rectangle<float> track { static_cast<float>(x + 28), static_cast<float>(y + 3), 96.0f, 6.0f };
    g.setColour(graph);
    g.fillRoundedRectangle(track, 3.0f);
    const auto fill = jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour(db >= 0.0f ? red : yellow);
    g.fillRoundedRectangle(track.reduced(1.0f).withWidth((track.getWidth() - 2.0f) * fill), 2.0f);
    label(g, theme.font(10.5f, true), db < -60.0f ? "-inf" : String(db, 1),
          { x + 128, y, 32, 12 }, db >= 0.0f ? red : white, Justification::centredRight);
}

void Editor::drawVerticalMeter(Graphics& g, const char* name, Rectangle<float> lane, float db, bool action)
{
    const auto track = lane.withSizeKeepingCentre(10.0f, lane.getHeight());
    const auto inner = track.reduced(1.0f);
    const auto clip = !action && db >= 0.0f;
    label(g, theme.font(9.5f, true), name, lane.withY(lane.getY() - 14.0f).withHeight(12.0f).toNearestInt(),
          muted, Justification::centred);
    g.setColour(panel);
    g.fillRoundedRectangle(track, 2.0f);

    g.setColour(action ? (db > 0.0f ? bandColours[1] : red) : (clip ? red : yellow));
    if (action)
    {
        const auto height = inner.getHeight() * std::min(1.0f, std::abs(db) / dsp::maxRangeDb);
        g.fillRect(db > 0.0f ? inner.withTop(inner.getBottom() - height) : inner.withHeight(height));
    }
    else
    {
        g.fillRect(inner.withTop(levelToY(inner, db, meterFloorDb, transferMaxDb)));
        g.setColour(white.withAlpha(0.55f));
        g.drawHorizontalLine(roundToInt(levelToY(inner, 0.0f, meterFloorDb, transferMaxDb)),
                             track.getX() - 2.0f, track.getRight() + 2.0f);
        label(g, theme.font(9.0f), db < meterFloorDb ? "-inf" : String(db, 1),
              lane.withY(lane.getBottom() + 2.0f).withHeight(12.0f).toNearestInt(),
              clip ? red : muted, Justification::centred);
    }
}

void Editor::drawHeader(Graphics& g)
{
    const auto signWidth = roundToInt(static_cast<float>(busBounds().getWidth()) * 0.46f);
    const auto fontSize = jlimit(39.0f, 60.0f, static_cast<float>(busBounds().getWidth()) * 0.078f);
    const auto sign = busBounds().withSizeKeepingCentre(signWidth, 60).withY(35);
    label(g, theme.font(fontSize, true), "SHORT BUS", sign.reduced(4, 0), ink, Justification::centred);
    label(g, theme.font(11.5f, true), "MASTERING DYNAMICS", sign.withY(96).withHeight(17),
          ink, Justification::centred);

    label(g, theme.font(16.0f, true), tonalSelected() ? "Tonal balance" : inspectorNames[dsp::index(selected)],
          { controlInset, inspectorTop, 218, 24 }, white);
    label(g, theme.font(10.5f, true), "MOVE", { busBounds().getWidth() - controlInset - 97, inspectorTop, 35, 24 },
          muted, Justification::centredRight);
    if (tonalSelected())
    {
        const auto tap = processor.rtaPosition();
        const auto& route = dsp::orders[static_cast<std::size_t>(settings.order)];
        const auto source = processor.isBypassed() ? String("Dry signal / bypass")
            : tap == 0 ? String("Rack input") : tap == dsp::stageCount ? String("Rack output")
            : "After " + String(stageNames[dsp::index(route[static_cast<std::size_t>(tap - 1)])]);
        label(g, theme.font(11.5f, true), source,
              { controlInset + 122, inspectorTop, earlier.getX() - controlInset - 170, 24 }, bandColours[1]);
        label(g, theme.font(10.5f, true), "TARGET", { controlInset, optionTop, 42, 22 }, muted);
        label(g, theme.font(10.5f, true), "SENSITIVITY",
              { rtaSensitivity.getX() - 67, optionTop, 63, 22 }, muted, Justification::centredRight);
    }
    else if (selected == Stage::multiband)
    {
        const auto band = static_cast<std::size_t>(selectedBand);
        const auto readout = Rectangle<int>::leftTopRightBottom(controlInset + 155, inspectorTop,
                                                                stageOn.getX() - 8, inspectorTop + 24);
        label(g, theme.font(10.5f, true), "Range " + signedDb(signedRange(settings.bands[band]))
              + "  /  Live " + signedDb(meters.bands[band]), readout, bandColours[band]);
        const auto x = bands.back().getRight() + 7;
        const auto width = bandOn.getX() - x - 7;
        const auto* hint = width >= 160 ? "Click band  /  Drag range bar" : "Click band to select";
        label(g, theme.font(10.5f), hint, { x, optionTop, width, 22 }, muted);
    }
    else if (geqSelected())
    {
        label(g, theme.font(10.0f, true), "RTA  POST EQ", { stageOn.getX() - 96, inspectorTop, 87, 24 },
              bandColours[2], Justification::centredRight);
    }
}

void Editor::drawBumper(Graphics& g)
{
    const auto top = getHeight() - bumperDepth;
    const auto bumper = Rectangle<int>(6, top, busBounds().getWidth() - 12, bumperDepth - 8).toFloat();
    g.setGradientFill(ColourGradient(bumperTop, bumper.getX(), bumper.getY(),
                                    bumperBottom, bumper.getX(), bumper.getBottom(), false));
    g.fillRoundedRectangle(bumper, 7.0f);
    g.setColour(bumperEdge);
    g.drawRoundedRectangle(bumper.reduced(0.5f), 6.5f, 1.0f);
    g.setColour(ink);
    g.drawHorizontalLine(top + 6, bumper.getX() + 9.0f, bumper.getRight() - 9.0f);

    drawPeakMeter(g, "IN", peaks[0], frameInset + 4, top + 17);
    drawPeakMeter(g, "OUT", peaks[1], frameInset + 4, top + 34);
    const auto center = Rectangle<int>(209, top + 12, presetsButton.getX() - 219, 39);
    const auto earlyLimiter = settings.limiter.enabled
        && dsp::orders[static_cast<std::size_t>(settings.order)].back() != Stage::limiter;
    const auto* caption = earlyLimiter ? "GAIN AFTER LIMITER CAN EXCEED CEILING"
        : center.getWidth() < 200 ? "STEREO / 3 MS" : "STEREO / 3 MS LOOKAHEAD";
    label(g, theme.font(10.5f, true),
          caption,
          center.withHeight(20), earlyLimiter ? yellow : white);
    label(g, theme.font(10.0f), "VST3  /  v" SHORT_BUS_VERSION, center.withTrimmedTop(20), muted);
}

void Editor::paint(Graphics& g)
{
    g.fillAll(background);
    drawBus(g);
    drawHeader(g);
    const auto bounds = plot();
    g.setColour(graph);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
    if (tonalSelected())
        drawTonalBalance(g, bounds);
    else if (selected == Stage::multiband)
        drawSpectrum(g, bounds);
    else if (geqSelected())
        drawGraphicEq(g);
    else
        drawTransfer(g, bounds);
    drawMeterHistory(g);
    drawBumper(g);
}

void Editor::drawGraphicEq(Graphics& g)
{
    const auto bounds = plot();
    const auto& first = geqFaders.front();
    const auto bankTop = static_cast<float>(first.getY()) + first.getPositionOfValue(dsp::geqRangeDb);
    const auto bankBottom = static_cast<float>(first.getY()) + first.getPositionOfValue(-dsp::geqRangeDb);
    const auto bank = bounds.withTop(bankTop).withBottom(bankBottom);
    for (std::size_t i = 0; i < geqFaders.size(); ++i)
    {
        const auto& fader = geqFaders[i];
        const auto top = levelToY(bank, geqSpectrum[i], geqRtaMinDb, 0.0f);
        if (!fader.isEnabled() || top >= bankBottom)
            continue;
        const auto bar = fader.getBounds().toFloat().withTop(top).withBottom(bankBottom);
        g.setColour(bandColours[2].withAlpha(0.27f));
        g.fillRect(bar);
        g.setColour(bandColours[2].withAlpha(0.72f));
        g.fillRect(bar.withHeight(1.2f));
    }
    for (const auto db : { 12, 0, -12 })
    {
        const auto y = first.getY() + roundToInt(first.getPositionOfValue(db));
        g.setColour(db == 0 ? edge : panel);
        g.drawHorizontalLine(y, bounds.getX() + 29.0f, bounds.getRight() - 28.0f);
        label(g, theme.font(9.5f, true), (db > 0 ? "+" : "") + String(db),
              { roundToInt(bounds.getX()) + 4, y - 7, 21, 14 }, muted, Justification::centredRight);
    }
    for (const auto db : { 0, -36, -72 })
    {
        const auto y = roundToInt(levelToY(bank, static_cast<float>(db), geqRtaMinDb, 0.0f));
        label(g, theme.font(9.5f), String(db), { roundToInt(bounds.getRight()) - 24, y - 7, 20, 14 }, bandColours[2]);
    }
    for (std::size_t i = 0; i < geqFaders.size(); ++i)
    {
        const auto& fader = geqFaders[i];
        const Point<float> centre { static_cast<float>(fader.getBounds().getCentreX()), bounds.getBottom() - 14.5f };
        const Graphics::ScopedSaveState scope(g);
        g.addTransform(AffineTransform::rotation(-MathConstants<float>::halfPi, centre.x, centre.y));
        const auto text = Rectangle<float>(26.0f, 12.0f).withCentre(centre).toNearestInt();
        label(g, theme.font(9.5f, true), eqFrequency(dsp::geqFrequencies[i]), text,
              fader.isEnabled() ? muted : muted.withAlpha(0.42f), Justification::centred);
    }
    const auto& meter = meters.stages[dsp::index(Stage::geq)];
    const auto readout = [](float amplitude)
    {
        return amplitude > 0.0f ? String(dsp::dbFromGain(amplitude), 1) : String("-inf");
    };
    label(g, theme.font(10.5f, true), "IN " + readout(meter.inputPeak) + "   OUT " + readout(meter.outputPeak),
          geqControls().withWidth(144), muted);
    label(g, theme.font(10.5f, true), "SATURATION",
          saturationDial.getBounds().translated(-70, 0).withWidth(66), muted, Justification::centredRight);
}

void Editor::drawMeterHistory(Graphics& g)
{
    const auto bounds = historyBounds();
    g.setColour(graph);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);

    const auto tapView = tonalSelected();
    const auto bandView = !tapView && selected == Stage::multiband;
    const auto showGain = !tapView && !geqSelected();
    const auto band = static_cast<std::size_t>(selectedBand);
    const auto gainColour = red;
    const auto levelColour = bandColours[2];
    const auto outputColour = bandColours[1];
    const auto legendFont = theme.font(9.5f, true);
    auto legend = bounds.toNearestInt().reduced(8, 0).withHeight(14);
    label(g, legendFont, tapView ? "TAP dBFS" : "IN dBFS", legend.removeFromLeft(58), levelColour);
    if (!tapView)
        label(g, legendFont, "OUT dBFS", legend.removeFromLeft(68), outputColour);
    if (showGain)
        label(g, legendFont, bandView ? "BAND " + String(selectedBand + 1) + " GAIN dB" : "GAIN dB", legend, gainColour);

    const auto area = bounds.reduced(27.0f, 0.0f).withTrimmedTop(15.0f).withTrimmedBottom(13.0f);
    for (int second = 0; second <= Processor::historySeconds; ++second)
    {
        const auto x = area.getRight() - area.getWidth() * static_cast<float>(second) / Processor::historySeconds;
        g.setColour(panel);
        g.drawVerticalLine(roundToInt(x), area.getY(), area.getBottom());
        const auto time = second == 0 ? String("NOW") : "-" + String(second) + " s";
        label(g, theme.font(9.0f), time, { roundToInt(x) - 18, roundToInt(area.getBottom()), 36, 12 },
              muted, Justification::centred);
    }
    g.setColour(panel);
    g.drawHorizontalLine(roundToInt(area.getCentreY()), area.getX(), area.getRight());
    const auto scaleLabel = [&](const String& text, float y, bool right, Colour colour)
    {
        const auto x = right ? roundToInt(area.getRight()) + 4 : roundToInt(bounds.getX()) + 3;
        label(g, theme.font(8.5f), text, { x, roundToInt(y) - 5, 20, 10 }, colour,
              right ? Justification::centredLeft : Justification::centredRight);
    };
    scaleLabel("0", area.getY(), false, levelColour);
    scaleLabel("-60", area.getBottom(), false, levelColour);
    if (showGain)
    {
        scaleLabel("+24", area.getY(), true, gainColour);
        scaleLabel("0", area.getCentreY(), true, gainColour);
        scaleLabel("-24", area.getBottom(), true, gainColour);
    }

    int firstFrame = 0;
    if (tapView)
    {
        firstFrame = historyCount;
        const auto key = currentTonalKey();
        while (firstFrame > 0 && historyAt(firstFrame - 1).tapKey == key)
            --firstFrame;
    }
    if (firstFrame >= historyCount)
        return;
    const auto xAt = [&](int i)
    {
        return area.getRight() - area.getWidth() * static_cast<float>(historyCount - 1 - i)
            / static_cast<float>(Processor::historyLength - 1);
    };
    Path input, output, gain;
    for (int i = firstFrame; i < historyCount; ++i)
    {
        const auto& frame = historyAt(i);
        const auto& stage = frame.levels.stages[dsp::index(selected)];
        const auto x = xAt(i);
        const auto add = [i, firstFrame, x](Path& path, float y)
        {
            if (i == firstFrame)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        };
        add(input, levelToY(area, dsp::dbFromGain(tapView ? frame.tapPeak : stage.inputPeak), meterFloorDb, 0.0f));
        if (!tapView)
            add(output, levelToY(area, dsp::dbFromGain(stage.outputPeak), meterFloorDb, 0.0f));
        if (showGain)
            add(gain, levelToY(area, bandView ? frame.levels.bands[band] : stage.gainDb,
                               -dsp::maxRangeDb, dsp::maxRangeDb));
    }
    auto fill = input;
    fill.lineTo(area.getRight(), area.getBottom());
    fill.lineTo(xAt(firstFrame), area.getBottom());
    fill.closeSubPath();
    g.setColour(levelColour.withAlpha(0.25f));
    g.fillPath(fill);
    g.setColour(levelColour.withAlpha(0.80f));
    g.strokePath(input, PathStrokeType(1.0f));
    g.setColour(outputColour);
    g.strokePath(output, PathStrokeType(1.1f));
    g.setColour(gainColour);
    g.strokePath(gain, PathStrokeType(1.4f));
}

void Editor::drawTransfer(Graphics& g, Rectangle<float> bounds)
{
    const auto area = transferArea();
    for (const auto db : { -60.0f, -48.0f, -36.0f, -24.0f, -12.0f, 0.0f })
    {
        const auto x = inputToX(area, db);
        const auto y = levelToY(area, db);
        g.setColour(panel);
        g.drawVerticalLine(roundToInt(x), area.getY(), area.getBottom());
        g.drawHorizontalLine(roundToInt(y), area.getX(), area.getRight());
        label(g, theme.font(10.0f), String(roundToInt(db)),
              { roundToInt(x) - 12, roundToInt(area.getBottom()) + 2, 24, 12 }, muted, Justification::centred);
    }
    g.setColour(edge);
    g.drawLine(area.getBottomLeft().getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

    const auto isLow = selected == Stage::lowLevel;
    const auto curve = isLow ? (settings.lowMode == dsp::LowMode::expand ? dsp::Curve::expand : dsp::Curve::lift)
                             : dsp::Curve::compress;
    const auto dynamics = isLow ? settings.lowLevel : settings.compressor;
    const auto on = selected == Stage::limiter ? settings.limiter.enabled : dynamics.enabled;
    const auto outputAt = [&](float input)
    {
        return selected == Stage::limiter ? std::min(input + settings.limiter.drive, settings.limiter.ceiling)
            : input + dsp::staticGain(input, dynamics, curve) + dynamics.makeup;
    };
    Path trace;
    for (int point = 0; point <= 156; ++point)
    {
        const auto input = transferMinDb + static_cast<float>(point) * 0.5f;
        const auto output = outputAt(input);
        if (point == 0)
            trace.startNewSubPath(inputToX(area, input), levelToY(area, output));
        else
            trace.lineTo(inputToX(area, input), levelToY(area, output));
    }
    g.setColour(on ? yellow : muted);
    g.strokePath(trace, PathStrokeType(1.7f));

    const auto limiterSelected = selected == Stage::limiter;
    g.setColour(yellow.withAlpha(0.40f));
    if (limiterSelected)
    {
        const auto y = levelToY(area, settings.limiter.ceiling);
        g.drawHorizontalLine(roundToInt(y), area.getX(), area.getRight());
        drawNode(g, { area.getRight() - 7.0f, y }, yellow, true);
    }
    else
    {
        const auto x = inputToX(area, dynamics.threshold);
        const auto output = outputAt(dynamics.threshold);
        g.drawVerticalLine(roundToInt(x), area.getY(), area.getBottom());
        drawNode(g, { x, levelToY(area, output) }, yellow, true);
    }
    if (historyCount > 0 && Time::getMillisecondCounterHiRes() - lastHistoryUpdate < 320.0)
    {
        const auto& newest = historyAt(historyCount - 1).levels.stages[dsp::index(selected)];
        const auto input = dsp::dbFromGain(newest.inputPeak);
        if (input >= transferMinDb)
        {
            const auto db = std::min(input, transferMaxDb);
            const Point<float> point { inputToX(area, db), levelToY(area, outputAt(db)) };
            g.setColour(graph);
            g.fillEllipse(Rectangle<float>(11.0f, 11.0f).withCentre(point));
            g.setColour(on && !processor.isBypassed() ? bandColours[2] : muted);
            g.fillEllipse(Rectangle<float>(7.5f, 7.5f).withCentre(point));
        }
    }

    auto sidebar = bounds.withLeft(area.getRight() + 8.0f);
    const auto meterLane = [&](const char* name, float db, bool action = false)
    {
        const auto lane = sidebar.removeFromLeft(stageMeterWidth).withY(area.getY()).withHeight(area.getHeight());
        drawVerticalMeter(g, name, lane, db, action);
    };
    const auto& stageLevels = meters.stages[dsp::index(selected)];
    const auto* action = limiterSelected ? "LIM" : isLow ? (settings.lowMode == dsp::LowMode::lift ? "LIFT" : "EXP") : "COMP";
    meterLane("IN", dsp::dbFromGain(stageLevels.inputPeak));
    meterLane(action, stageLevels.gainDb, true);
    meterLane("OUT", dsp::dbFromGain(stageLevels.outputPeak));

    auto readout = sidebar.toNearestInt().reduced(8, 9);
    label(g, theme.font(10.5f, true), "GAIN CHANGE", readout.removeFromTop(14), muted);
    label(g, theme.font(24.0f, true), signedDb(stageLevels.gainDb), readout.removeFromTop(25), on ? yellow : muted);
    const auto hint = [&](const char* line)
    {
        label(g, theme.font(10.5f), line, readout.removeFromTop(13), muted);
    };
    if (limiterSelected)
    {
        hint("Ceiling up/down");
    }
    else
    {
        hint("Threshold left/right");
        hint(isLow ? "Below  /  range up/down" : bounds.getHeight() < 145.0f ? "Gain / ratio up/down" : "Below  /  gain up/down");
        if (!isLow && bounds.getHeight() >= 145.0f)
            hint("Above  /  ratio up/down");
    }
    if (selected == Stage::compressor && bounds.getHeight() >= 145.0f)
        label(g, theme.font(10.0f, true), "MODE", compressorModeBox.getBounds().translated(0, -14).withHeight(12), muted);
}

dsp::Edges Editor::edges(int band) const
{
    return bandSpans[static_cast<std::size_t>(band)];
}

float Editor::frequencyToX(float hz) const
{
    return logFrequencyX(spectrumArea(), hz);
}

Point<float> Editor::bandNode(int band) const
{
    const auto span = edges(band);
    const auto threshold = settings.bands[static_cast<std::size_t>(band)].dynamics.threshold;
    return { frequencyToX(std::sqrt(span.low * span.high)), levelToY(spectrumArea(), threshold, spectrumMinDb, 0.0f) };
}

Rectangle<float> Editor::bandRangeBar(int band) const
{
    const auto span = edges(band);
    const auto left = frequencyToX(span.low);
    const auto width = frequencyToX(span.high) - left;
    const auto rangeArea = spectrumArea().reduced(0.0f, bandRangeInset);
    const auto range = signedRange(settings.bands[static_cast<std::size_t>(band)]);
    const auto y = levelToY(rangeArea, range, -dsp::maxRangeDb, dsp::maxRangeDb);
    return Rectangle<float>(left, y - 3.0f, width, 6.0f).reduced(std::min(2.0f, width * 0.07f), 0.0f);
}

std::optional<int> Editor::bandAt(float x) const
{
    for (int band = 0; band < dsp::bandCount; ++band)
    {
        const auto span = edges(band);
        if (x >= frequencyToX(span.low) && x < frequencyToX(span.high))
            return band;
    }
    return {};
}

void Editor::drawSpectrum(Graphics& g, Rectangle<float> bounds)
{
    const auto area = spectrumArea();
    const auto rangeArea = area.reduced(0.0f, bandRangeInset);
    const auto zero = rangeArea.getCentreY();
    const auto& stageLevels = meters.stages[dsp::index(Stage::multiband)];
    const auto lane = area.withWidth(spectrumSideWidth);
    drawVerticalMeter(g, "IN", lane.withX(bounds.getX()), dsp::dbFromGain(stageLevels.inputPeak));
    drawVerticalMeter(g, "OUT", lane.withX(area.getRight()), dsp::dbFromGain(stageLevels.outputPeak));
    for (auto frequency : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const auto x = frequencyToX(frequency);
        g.setColour(panel);
        g.drawVerticalLine(roundToInt(x), area.getY(), area.getBottom());
        const auto position = jlimit(area.getX(), area.getRight() - 24.0f, x - 12.0f);
        const auto text = frequency < 1000.0f ? String(roundToInt(frequency)) : String(roundToInt(frequency / 1000.0f)) + "k";
        label(g, theme.font(10.0f), text, { roundToInt(position), roundToInt(area.getBottom()) + 3, 24, 12 },
              muted, Justification::centred);
    }
    Path trace;
    bool first = true;
    const auto hzPerBin = processor.displayRate() / static_cast<float>(Processor::fftSize);
    for (std::size_t bin = 1; bin < levels.size(); ++bin)
    {
        const auto hz = hzPerBin * static_cast<float>(bin);
        if (hz < dsp::minHz)
            continue;
        if (hz > dsp::maxHz)
            break;
        const auto x = frequencyToX(hz);
        const auto y = levelToY(area, levels[bin], spectrumMinDb, 0.0f);
        if (first)
            trace.startNewSubPath(x, y);
        else
            trace.lineTo(x, y);
        first = false;
    }
    g.setColour(Colour(0xff8b9db6));
    g.strokePath(trace, PathStrokeType(1.1f));

    for (int i = 0; i < dsp::bandCount; ++i)
    {
        const auto band = (selectedBand + i + 1) % dsp::bandCount;
        const auto n = static_cast<std::size_t>(band);
        const auto span = edges(band);
        const auto x = frequencyToX(span.low);
        const auto end = frequencyToX(span.high);
        const auto colour = bandColours[n];
        const auto on = settings.multiEnabled && settings.bands[n].dynamics.enabled;
        g.setColour(colour.withAlpha(on ? 0.17f : 0.055f));
        g.fillRect(x, area.getY(), end - x, area.getHeight());
        g.setColour(colour.withAlpha(band == selectedBand ? 0.95f : 0.40f));
        g.drawVerticalLine(roundToInt(x), area.getY(), area.getBottom());
        g.drawVerticalLine(roundToInt(end), area.getY(), area.getBottom());
        if (band == selectedBand)
        {
            for (const auto handle : { x, end })
                g.fillRoundedRectangle(handle - 3.5f, area.getY() - 12.0f, 7.0f, 13.0f, 2.0f);
        }
        if (on)
        {
            const auto live = levelToY(rangeArea, meters.bands[n], -dsp::maxRangeDb, dsp::maxRangeDb);
            g.setColour(colour.withAlpha(0.24f));
            g.fillRect(x, std::min(zero, live), end - x, std::abs(live - zero));
        }
    }
    constexpr float dashes[] { 3.0f, 4.0f };
    g.setColour(muted.withAlpha(0.55f));
    g.drawDashedLine({ area.getX(), zero, area.getRight(), zero }, dashes, 2);
    label(g, theme.font(8.5f, true), "0", { roundToInt(area.getX()) - 8, roundToInt(zero) - 6, 8, 12 },
          muted, Justification::centred);

    const auto span = edges(selectedBand);
    const auto selectedColour = bandColours[static_cast<std::size_t>(selectedBand)];
    g.setColour(selectedColour.withAlpha(0.40f));
    g.drawHorizontalLine(roundToInt(bandNode(selectedBand).y), frequencyToX(span.low), frequencyToX(span.high));
    for (int i = 0; i < dsp::bandCount; ++i)
    {
        const auto band = (selectedBand + i + 1) % dsp::bandCount;
        const auto colour = bandColours[static_cast<std::size_t>(band)];
        const auto bar = bandRangeBar(band);
        g.setColour(colour.withAlpha(band == selectedBand ? 0.95f : 0.62f));
        g.fillRect(bar.withSizeKeepingCentre(bar.getWidth(), 1.5f));
        drawNode(g, bandNode(band), colour, band == selectedBand);
        g.setColour(colour.withAlpha(band == selectedBand ? 1.0f : 0.76f));
        g.fillRoundedRectangle(rangeGrip(bar), 1.8f);
    }
}

void Editor::drawTonalBalance(Graphics& g, Rectangle<float> bounds)
{
    const auto targetId = targetChoice.getSelectedId();
    const auto captured = targetId == 4 ? processor.tonalReference() : std::nullopt;
    auto count = dsp::tonalBandsAtRate(processor.displayRate());
    if (captured)
        count = std::min(count, captured->bands);
    const auto targetIndex = static_cast<std::size_t>(targetId >= 1 && targetId <= 3 ? targetId - 1 : 0);
    const auto target = dsp::normalizedTone(captured ? captured->power : dsp::tonalTargets[targetIndex], count);
    const auto hasFreshSignal = lastTonalFrame > 0.0 && tonalKey == currentTonalKey();
    const auto live = hasFreshSignal ? dsp::normalizedTone(tonalAverage, count) : dsp::TonalCurve {};
    const auto broad = tonalMode.getSelectedId() == 2;

    auto legend = bounds.toNearestInt().reduced(12, 0).withHeight(19);
    label(g, theme.font(10.0f, true), broad ? "LIVE  /  dB VS TARGET" : "LIVE AVERAGE  /  dB REL",
          legend.removeFromLeft(150), yellow);
    const auto* targetLabel = targetId == 4 && !captured ? "BALANCED  +/-4 dB" : "TARGET  +/-4 dB";
    label(g, theme.font(10.0f, true), targetLabel, legend, bandColours[1], Justification::centredRight);
    if (count > 0)
    {
        if (broad)
            drawBroadTone(g, bounds, target, live, count);
        else
            drawFineTone(g, bounds, target, live, count);
    }

    if (dsp::tonalEnergy(live) <= 0.0f)
    {
        const auto notice = bounds.withSizeKeepingCentre(176.0f, 22.0f);
        g.setColour(graph.withAlpha(0.92f));
        g.fillRoundedRectangle(notice, 3.0f);
        label(g, theme.font(11.5f, true), count > 0 ? "Waiting for audio at this tap" : "Sample rate below analyzer range",
              notice.toNearestInt(), muted, Justification::centred);
    }
    const auto footer = bounds.toNearestInt().withY(roundToInt(historyBounds().getBottom()) + 1).withHeight(14);
    label(g, theme.font(10.0f), targetId == 4 && !captured
          ? "Play audio and press Capture to save a reference" : "Level-matched spectral balance   /   Move to compare taps",
          footer, muted, Justification::centred);
}

void Editor::drawFineTone(Graphics& g, Rectangle<float> bounds, const dsp::TonalCurve& target,
                         const dsp::TonalCurve& live, int count)
{
    constexpr auto floor = -30.0f;
    constexpr auto ceiling = 18.0f;
    const auto area = bounds.withTrimmedLeft(37.0f).withTrimmedRight(16.0f)
        .withTrimmedTop(21.0f).withTrimmedBottom(23.0f);
    for (const auto db : { -30, -24, -12, 0, 12, 18 })
    {
        const auto y = roundToInt(levelToY(area, static_cast<float>(db), floor, ceiling));
        g.setColour(db == 0 ? edge : panel);
        g.drawHorizontalLine(y, area.getX(), area.getRight());
        label(g, theme.font(9.0f), String(db), { roundToInt(bounds.getX()) + 4, y - 6, 27, 12 },
              muted, Justification::centredRight);
    }
    for (const auto hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const auto x = roundToInt(logFrequencyX(area, hz));
        g.setColour(panel);
        g.drawVerticalLine(x, area.getY(), area.getBottom());
        const auto tick = hz < 1000.0f ? String(roundToInt(hz)) : String(roundToInt(hz / 1000.0f)) + "k";
        label(g, theme.font(9.5f), tick, { x - 13, roundToInt(area.getBottom()) + 2, 26, 12 },
              muted, Justification::centred);
    }

    const auto point = [&](const dsp::TonalCurve& curve, int band, float offset)
    {
        const auto fraction = (static_cast<float>(band) + 0.5f) / dsp::tonalBandCount;
        const auto db = dsp::tonalDb(curve[static_cast<std::size_t>(band)] * static_cast<float>(count)) + offset;
        return Point<float>(area.getX() + fraction * area.getWidth(), levelToY(area, db, floor, ceiling));
    };
    Path corridor;
    corridor.startNewSubPath(point(target, 0, tonalTolerance));
    for (int band = 1; band < count; ++band)
        corridor.lineTo(point(target, band, tonalTolerance));
    for (int band = count - 1; band >= 0; --band)
        corridor.lineTo(point(target, band, -tonalTolerance));
    corridor.closeSubPath();
    g.setColour(bandColours[1].withAlpha(0.21f));
    g.fillPath(corridor);
    g.setColour(bandColours[1].withAlpha(0.60f));
    g.strokePath(corridor, PathStrokeType(1.0f));

    if (dsp::tonalEnergy(live) > 0.0f)
    {
        Path trace;
        trace.startNewSubPath(point(live, 0, 0.0f));
        for (int band = 1; band < count; ++band)
            trace.lineTo(point(live, band, 0.0f));
        g.setColour(yellow);
        g.strokePath(trace, PathStrokeType(1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void Editor::drawBroadTone(Graphics& g, Rectangle<float> bounds, const dsp::TonalCurve& target,
                          const dsp::TonalCurve& live, int count)
{
    constexpr std::array boundaries { 0, 7, 14, 21, dsp::tonalBandCount };
    constexpr std::array<const char*, 4> names { "LOW", "LOW MID", "HIGH MID", "HIGH" };
    constexpr float scale = 18.0f;
    auto area = bounds.withTrimmedLeft(37.0f).withTrimmedRight(14.0f)
        .withTrimmedTop(38.0f).withTrimmedBottom(25.0f);
    const auto hasSignal = dsp::tonalEnergy(live) > 0.0f;
    for (const auto db : { -18, -12, -6, 0, 6, 12, 18 })
    {
        const auto y = roundToInt(levelToY(area, static_cast<float>(db), -scale, scale));
        g.setColour(db == 0 ? edge : panel);
        g.drawHorizontalLine(y, area.getX(), area.getRight());
        label(g, theme.font(9.0f), String(db), { roundToInt(bounds.getX()) + 4, y - 6, 27, 12 },
              muted, Justification::centredRight);
    }
    const auto width = area.getWidth() / static_cast<float>(names.size());
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        const auto column = area.removeFromLeft(width).reduced(4.0f, 0.0f);
        const auto first = boundaries[i];
        const auto end = std::min(count, boundaries[i + 1]);
        label(g, theme.font(10.5f, true), names[i], column.withY(column.getY() - 17.0f).withHeight(16.0f).toNearestInt(),
              muted, Justification::centred);
        if (first >= end)
            continue;
        const auto top = levelToY(column, tonalTolerance, -scale, scale);
        const auto bottom = levelToY(column, -tonalTolerance, -scale, scale);
        const auto range = column.withTop(top).withBottom(bottom);
        g.setColour(bandColours[1].withAlpha(0.21f));
        g.fillRoundedRectangle(range, 2.0f);
        g.setColour(bandColours[1].withAlpha(0.60f));
        g.drawRoundedRectangle(range, 2.0f, 1.0f);
        if (hasSignal)
        {
            const auto db = dsp::tonalDb(dsp::tonalEnergy(live, first, end))
                - dsp::tonalDb(dsp::tonalEnergy(target, first, end));
            const auto y = levelToY(column, db, -scale, scale);
            g.setColour(yellow);
            g.fillRoundedRectangle(column.getCentreX() - 13.0f, y - 2.5f, 26.0f, 5.0f, 2.0f);
            label(g, theme.font(11.0f, true), signedDb(db),
                  column.withY(column.getBottom() + 4.0f).withHeight(15.0f).toNearestInt(), yellow, Justification::centred);
        }
    }
}

void Editor::updateSpectrum()
{
    const auto key = processor.spectrumKey(Processor::SpectrumView::output, 0, 0, false);
    if (!processor.readSpectrum(fftBuffer[0].data(), fftBuffer[1].data(), Processor::fftSize, key))
        return;
    for (auto& channel : fftBuffer)
    {
        window.multiplyWithWindowingTable(channel.data(), Processor::fftSize);
        std::fill(channel.begin() + Processor::fftSize, channel.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(channel.data(), true);
    }
    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        const auto left = fftBuffer[0][i];
        const auto right = fftBuffer[1][i];
        const auto magnitude = std::sqrt(0.5f * (left * left + right * right)) * (4.0f / Processor::fftSize);
        levels[i] = std::max(dsp::dbFromGain(magnitude), levels[i] - 3.0f);
    }
}

std::uint32_t Editor::currentTonalKey() const
{
    return processor.spectrumKey(Processor::SpectrumView::tonal, settings.order, processor.rtaPosition(),
                                 processor.isBypassed());
}

void Editor::transformLongSpectrum()
{
    for (auto& channel : tonalBuffer)
    {
        tonalWindow.multiplyWithWindowingTable(channel.data(), Processor::tonalFftSize);
        std::fill(channel.begin() + Processor::tonalFftSize, channel.end(), 0.0f);
        tonalFft.performFrequencyOnlyForwardTransform(channel.data(), true);
    }
}

void Editor::updateTonalSpectrum()
{
    const auto key = currentTonalKey();
    if (key != tonalKey)
    {
        tonalKey = key;
        lastTonalFrame = 0.0;
    }

    const auto now = Time::getMillisecondCounterHiRes();
    if (processor.readSpectrum(tonalBuffer[0].data(), tonalBuffer[1].data(), Processor::tonalFftSize, key))
    {
        if (!dsp::tonalAboveFloor(tonalBuffer[0].data(), tonalBuffer[1].data(), Processor::tonalFftSize,
                                 static_cast<float>(rtaSensitivity.getValue()), lastTonalFrame > 0.0))
        {
            lastTonalFrame = 0.0;
            captureReference.setEnabled(false);
            return;
        }
        transformLongSpectrum();
        const auto power = dsp::tonalBandPower(tonalBuffer[0].data(), tonalBuffer[1].data(),
                                             Processor::tonalFftSize, processor.displayRate());
        const auto total = dsp::tonalEnergy(power);
        if (std::isfinite(total) && total > 0.0f)
        {
            const auto retained = lastTonalFrame > 0.0
                ? static_cast<float>(std::exp(-(now - lastTonalFrame) / 900.0)) : 0.0f;
            for (std::size_t i = 0; i < power.size(); ++i)
                tonalAverage[i] = retained * tonalAverage[i] + (1.0f - retained) * power[i];
            lastTonalFrame = now;
        }
        else
        {
            lastTonalFrame = 0.0;
        }
    }
    else if (now - lastTonalFrame > 650.0)
    {
        lastTonalFrame = 0.0;
    }
    captureReference.setEnabled(lastTonalFrame > 0.0);
}

void Editor::updateGeqSpectrum()
{
    const auto& route = dsp::orders[static_cast<std::size_t>(settings.order)];
    const auto position = dsp::stagePosition(route, Stage::geq) + 1;
    const auto key = processor.spectrumKey(Processor::SpectrumView::geq, settings.order, position, processor.isBypassed());
    const auto clear = [this]
    {
        geqSpectrum.fill(geqRtaMinDb);
        lastGeqFrame = 0.0;
    };
    if (key != geqSpectrumKey)
    {
        geqSpectrumKey = key;
        clear();
    }
    const auto now = Time::getMillisecondCounterHiRes();
    if (!processor.readSpectrum(tonalBuffer[0].data(), tonalBuffer[1].data(), Processor::tonalFftSize, key))
    {
        if (now - lastGeqFrame > 650.0)
            clear();
        return;
    }
    if (!dsp::tonalAboveFloor(tonalBuffer[0].data(), tonalBuffer[1].data(), Processor::tonalFftSize,
                             static_cast<float>(rtaSensitivity.getValue()), lastGeqFrame > 0.0))
    {
        clear();
        return;
    }
    transformLongSpectrum();
    const auto power = dsp::geqBandPower(tonalBuffer[0].data(), tonalBuffer[1].data(), Processor::tonalFftSize,
                                       processor.displayRate());
    const auto fall = static_cast<float>((now - lastGeqFrame) * 0.045);
    for (std::size_t i = 0; i < geqSpectrum.size(); ++i)
    {
        // Convert the one-sided Hann-windowed FFT power to RMS dBFS.
        const auto db = dsp::tonalDb(power[i] * (16.0f / 3.0f));
        geqSpectrum[i] = std::clamp(std::max(db, geqSpectrum[i] - fall), geqRtaMinDb, 0.0f);
    }
    lastGeqFrame = now;
}

const Processor::HistoryFrame& Editor::historyAt(int i) const
{
    const auto slot = (historyHead + Processor::historyLength - historyCount + i) % Processor::historyLength;
    return history[static_cast<std::size_t>(slot)];
}

void Editor::updateMeterHistory()
{
    std::array<Processor::HistoryFrame, Processor::historyLength> frames;
    const auto count = processor.readMeterHistory(frames.data(), static_cast<int>(frames.size()));
    for (int i = 0; i < count; ++i)
    {
        const auto& frame = frames[static_cast<std::size_t>(i)];
        if (historyCount > 0)
        {
            const auto& previous = historyAt(historyCount - 1);
            if (frame.epoch != previous.epoch || frame.sequence != previous.sequence + 1u)
                historyCount = 0;
        }
        history[static_cast<std::size_t>(historyHead)] = frame;
        historyHead = (historyHead + 1) % Processor::historyLength;
        historyCount = std::min(historyCount + 1, Processor::historyLength);
    }
    if (count > 0)
        lastHistoryUpdate = Time::getMillisecondCounterHiRes();
}

void Editor::animateLights()
{
    const auto now = Time::getMillisecondCounterHiRes();
    const auto elapsed = std::max(0.0, now - lastLampTick);
    lastLampTick = now;
    animatePresetDoor(elapsed);
    const auto bypassed = processor.isBypassed();
    const auto peakHit = processor.consumeOutputPeakEvent();
    if (bypassed)
        brakeHoldUntil = 0.0;
    else if (peakHit)
        brakeHoldUntil = now + 130.0;

    const auto activeSide = static_cast<std::size_t>(now / 500.0) % lampGlow.size();
    for (std::size_t i = 0; i < lampGlow.size(); ++i)
    {
        const auto target = bypassed && i == activeSide ? 1.0f : 0.0f;
        const auto thermalTimeMs = target > lampGlow[i] ? 52.0 : 105.0;
        const auto retained = static_cast<float>(std::exp(-elapsed / thermalTimeMs));
        lampGlow[i] = target + (lampGlow[i] - target) * retained;
        if (lampGlow[i] < 0.004f)
            lampGlow[i] = 0.0f;
    }
}

void Editor::timerCallback()
{
    const auto previous = order;
    applyOrder();
    if (!tonalSelected() && selected == Stage::compressor && boundCompressorMode != settings.compressorMode)
        bindControls();
    if (order != previous)
        resized();
    meters = processor.readMeters();
    updateMeterHistory();
    peaks = processor.readPeaks();
    const std::array on { settings.lowLevel.enabled, settings.multiEnabled, settings.compressor.enabled,
                          settings.limiter.enabled, settings.geq.enabled };
    for (std::size_t i = 0; i < on.size(); ++i)
    {
        stages[i].gain = meters.stages[i].gainDb;
        stages[i].enabled = on[i];
        stages[i].repaint();
    }
    if (tonalSelected())
    {
        updateTonalSpectrum();
    }
    else if (selected == Stage::multiband)
    {
        refreshBandControls();
        updateSpectrum();
    }
    else if (geqSelected())
    {
        const auto nyquist = processor.displayRate() * 0.5f;
        for (std::size_t i = 0; i < geqFaders.size(); ++i)
            geqFaders[i].setEnabled(dsp::geqFrequencies[i] < nyquist);
        updateGeqSpectrum();
    }
    animateLights();
    repaint();
}

std::optional<Editor::GraphHit> Editor::graphHit(Point<float> position) const
{
    if (tonalSelected() || geqSelected() || !plot().contains(position))
        return {};

    if (selected != Stage::multiband)
    {
        const auto area = transferArea();
        if (!area.expanded(8.0f).contains(position))
            return {};
        if (selected == Stage::limiter)
        {
            if (std::abs(position.y - levelToY(area, settings.limiter.ceiling)) <= 9.0f)
                return GraphHit { GraphControl::ceiling };
        }
        else
        {
            const auto threshold = selected == Stage::lowLevel ? settings.lowLevel.threshold : settings.compressor.threshold;
            const auto thresholdX = inputToX(area, threshold);
            if (std::abs(position.x - thresholdX) <= 9.0f)
                return GraphHit { GraphControl::threshold };
            if (area.contains(position))
            {
                if (selected == Stage::compressor)
                    return GraphHit { position.x < thresholdX ? GraphControl::makeup : GraphControl::ratio };
                if (position.x < thresholdX)
                    return GraphHit { GraphControl::range };
            }
        }
        return {};
    }

    const auto area = spectrumArea();
    if (position.y >= area.getY() - 14.0f && position.y < area.getY() + 2.0f)
    {
        const auto span = edges(selectedBand);
        const auto low = std::abs(position.x - frequencyToX(span.low));
        const auto high = std::abs(position.x - frequencyToX(span.high));
        if (std::min(low, high) <= 6.0f)
            return GraphHit { low <= high ? GraphControl::bandLow : GraphControl::bandHigh, selectedBand };
    }

    if (area.contains(position))
    {
        if (const auto band = bandAt(position.x))
        {
            const auto bar = bandRangeBar(*band);
            if (rangeGrip(bar).expanded(0.0f, 2.0f).contains(position))
                return GraphHit { GraphControl::bandRange, *band };
            if (position.getDistanceFrom(bandNode(*band)) <= 9.0f)
                return GraphHit { GraphControl::bandNode, *band };
            if (bar.expanded(0.0f, 2.0f).contains(position))
                return GraphHit { GraphControl::bandRange, *band };
        }
    }
    return {};
}

void Editor::mouseMove(const MouseEvent& event)
{
    const auto hit = graphHit(event.position);
    auto graphCursor = MouseCursor::NormalCursor;
    if (hit)
    {
        const auto control = hit->control;
        const auto horizontal = control == GraphControl::threshold || control == GraphControl::bandLow
            || control == GraphControl::bandHigh;
        graphCursor = horizontal ? MouseCursor::LeftRightResizeCursor : MouseCursor::UpDownResizeCursor;
    }
    setMouseCursor(graphCursor);
}

void Editor::mouseDown(const MouseEvent& event)
{
    if (tonalSelected() || !event.mods.isLeftButtonDown())
        return;

    finishGraphDrag();
    refreshSettings();
    const auto hit = graphHit(event.position);
    if (selected == Stage::multiband)
    {
        const auto band = hit ? std::optional<int>(hit->band)
                              : (spectrumArea().contains(event.position) ? bandAt(event.position.x) : std::nullopt);
        if (band && *band != selectedBand)
        {
            selectedBand = *band;
            bindControls();
        }
    }
    if (!hit)
        return;

    const auto name = [control = hit->control]
    {
        switch (control)
        {
            case GraphControl::threshold:
            case GraphControl::bandNode: return "threshold";
            case GraphControl::ceiling: return "ceiling";
            case GraphControl::makeup: return "makeup";
            case GraphControl::ratio: return "ratio";
            case GraphControl::range:
            case GraphControl::bandRange: return "range";
            case GraphControl::bandLow: return "low_hz";
            case GraphControl::bandHigh: return "high_hz";
        }
        return "";
    }();
    const auto prefix = selected == Stage::multiband ? param::band(hit->band) : param::stage(selected);
    auto* parameter = processor.state.getParameter(param::id(prefix, name));
    if (parameter == nullptr)
        return;

    auto& drag = graphDrag.emplace(GraphDrag { *hit, event.position, parameter,
                                               parameter->convertFrom0to1(parameter->getValue()) });
    if (hit->control == GraphControl::bandLow)
        drag.startEdgeHz = edges(hit->band).low;
    else if (hit->control == GraphControl::bandHigh)
        drag.startEdgeHz = edges(hit->band).high;
    else if (hit->control == GraphControl::ratio)
    {
        const auto area = transferArea();
        const auto distance = event.position.x - inputToX(area, settings.compressor.threshold);
        drag.ratioDepthDb = std::max(24.0f, distance * transferSpanDb / area.getWidth());
    }
    else if (hit->control == GraphControl::range && settings.lowMode == dsp::LowMode::expand)
    {
        drag.rangeDirection = -1.0f;
    }
    else if (hit->control == GraphControl::bandRange)
    {
        drag.initialValue = signedRange(settings.bands[static_cast<std::size_t>(hit->band)]);
        drag.liftParameter = processor.state.getParameter(param::id(prefix, param::bandLift));
        drag.liftParameter->beginChangeGesture();
    }
    parameter->beginChangeGesture();
}

void Editor::mouseDrag(const MouseEvent& event)
{
    if (!graphDrag)
        return;

    const auto& drag = *graphDrag;
    const auto delta = event.position - drag.start;
    const auto area = transferArea();
    auto next = drag.initialValue;
    switch (drag.target.control)
    {
        case GraphControl::threshold:
            next += delta.x * transferSpanDb / area.getWidth();
            break;
        case GraphControl::ceiling:
        case GraphControl::makeup:
        case GraphControl::range:
            next -= drag.rangeDirection * delta.y * transferSpanDb / area.getHeight();
            break;
        case GraphControl::ratio:
        {
            const auto& range = drag.parameter->getNormalisableRange();
            const auto slopeChange = -delta.y * transferSpanDb / (area.getHeight() * drag.ratioDepthDb);
            const auto slope = std::clamp(1.0f / drag.initialValue + slopeChange, 1.0f / range.end, 1.0f / range.start);
            next = 1.0f / slope;
            break;
        }
        case GraphControl::bandNode:
            next += delta.y * spectrumMinDb / spectrumArea().getHeight();
            break;
        case GraphControl::bandRange:
        {
            const auto height = spectrumArea().reduced(0.0f, bandRangeInset).getHeight();
            const auto rangeDb = next - delta.y * (2.0f * dsp::maxRangeDb) / height;
            const auto lift = rangeDb > 0.0f;
            next = std::abs(rangeDb);
            if (next >= 0.05f && lift != (drag.liftParameter->getValue() > 0.5f))
            {
                // Cross zero before switching direction so the old range can't jump to the new mode.
                setGraphValue(*drag.parameter, 0.0f);
                setGraphValue(*drag.liftParameter, lift ? 1.0f : 0.0f);
            }
            break;
        }
        case GraphControl::bandLow:
        case GraphControl::bandHigh:
        {
            if (std::abs(delta.x) < 1.0e-4f)
                break;
            const auto factor = std::pow(dsp::maxHz / dsp::minHz, delta.x / spectrumArea().getWidth());
            const auto band = static_cast<std::size_t>(drag.target.band);
            const auto edge = drag.target.control == GraphControl::bandLow ? dsp::BandEdge::low : dsp::BandEdge::high;
            next = limitFrequency(band, edge, drag.startEdgeHz * factor);
            break;
        }
    }

    setGraphValue(*drag.parameter, next);
    refreshSettings();
    if (selected == Stage::multiband)
        refreshBandControls();
    repaint(plot().getSmallestIntegerContainer());
}

void Editor::finishGraphDrag()
{
    if (graphDrag)
    {
        graphDrag->parameter->endChangeGesture();
        if (graphDrag->liftParameter != nullptr)
            graphDrag->liftParameter->endChangeGesture();
        graphDrag.reset();
    }
}

void Editor::mouseUp(const MouseEvent& event)
{
    finishGraphDrag();
    mouseMove(event);
}

} // namespace shortbus
