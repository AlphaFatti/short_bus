// SPDX-License-Identifier: AGPL-3.0-only
#include "Editor.h"

namespace shortbus
{
using namespace juce;
using dsp::Stage;

namespace
{
const auto background = Colour(0xff14171c);
const auto panel = Colour(0xff20252d);
const auto graph = Colour(0xff101318);
const auto edge = Colour(0xff393f49);
const auto white = Colour(0xfff5f1e9);
const auto muted = Colour(0xffaeb8c4);
const auto yellow = Colour(0xffffca54);
const auto red = Colour(0xfff6877f);
const std::array<Colour, dsp::bandCount> bandColours {
    Colour(0xfff0bf58), Colour(0xff73d6b6), Colour(0xff79bbea),
    Colour(0xffb7a7f6), Colour(0xfff090ab)
};
constexpr std::array<const char*, dsp::stageCount> stageNames {
    "Expand / Lift", "5-Band", "Compressor", "Limiter"
};
constexpr std::array<const char*, dsp::stageCount> inspectorNames {
    "Low-level dynamics", "5-band compressor", "Bus compressor", "Lookahead limiter"
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

void label(Graphics& g, String text, Rectangle<int> bounds, float size, Colour colour,
           Justification alignment = Justification::centredLeft, bool bold = false)
{
    g.setFont(FontOptions(size, bold ? Font::bold : Font::plain));
    g.setColour(colour);
    g.drawText(text, bounds, alignment, false);
}

void busMark(Graphics& g, float x, float y)
{
    g.setColour(yellow);
    g.fillRoundedRectangle(x, y, 36.0f, 17.0f, 3.5f);
    g.fillRoundedRectangle(x + 32.0f, y + 6.0f, 12.0f, 11.0f, 3.0f);
    g.setColour(background);
    for (int i = 0; i < 4; ++i)
        g.fillRoundedRectangle(x + 4.0f + 7.0f * static_cast<float>(i), y + 3.0f, 5.0f, 5.0f, 0.7f);
    g.fillRect(x + 1.0f, y + 10.0f, 33.0f, 1.5f);
    for (auto offset : { 8.0f, 32.0f })
    {
        g.fillEllipse(x + offset - 1.0f, y + 13.0f, 9.0f, 9.0f);
        g.setColour(muted);
        g.fillEllipse(x + offset + 1.5f, y + 15.5f, 4.0f, 4.0f);
        g.setColour(background);
    }
    g.setColour(red);
    g.fillEllipse(x + 39.0f, y + 9.0f, 3.0f, 3.0f);
}

void peakMeter(Graphics& g, String name, float peak, int x, int y)
{
    const auto db = dsp::dbFromGain(peak);
    label(g, name, { x, y, 28, 12 }, 10.5f, muted, Justification::centredLeft, true);
    const Rectangle<float> track { static_cast<float>(x + 29), static_cast<float>(y + 4), 94.0f, 4.0f };
    g.setColour(edge);
    g.fillRoundedRectangle(track, 2.0f);
    const auto fill = jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour(db >= 0.0f ? red : yellow);
    g.fillRoundedRectangle(track.withWidth(track.getWidth() * fill), 2.0f);
    label(g, db < -60.0f ? "-inf" : String(db, 1), { x + 125, y, 40, 12 },
          10.5f, db >= 0.0f ? red : white, Justification::centredRight);
}

float levelToY(Rectangle<float> area, float db)
{
    return area.getBottom() - area.getHeight() * jlimit(0.0f, 1.0f, (db + 72.0f) / 78.0f);
}
} // namespace

void Editor::StageButton::paintButton(Graphics& g, bool hovered, bool pressed)
{
    const auto chosen = getToggleState();
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour((chosen ? Colour(0xff30291d) : panel).brighter(hovered || pressed ? 0.10f : 0.0f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(chosen || hasKeyboardFocus(false) ? yellow : edge);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    const auto area = getLocalBounds().reduced(9, 5);
    label(g, "0" + String(position + 1), area.withWidth(22).withHeight(17), 10.5f, chosen ? yellow : muted,
          Justification::centredLeft, true);
    label(g, getName(), area.withTrimmedLeft(23).withHeight(17), 13.0f, white, Justification::centredLeft, true);
    label(g, enabled ? signedDb(gain) : "OFF", area.withTrimmedLeft(23).withTrimmedTop(19),
          11.0f, enabled ? yellow : muted);
}

Editor::Editor(Processor& owner)
    : AudioProcessorEditor(owner), processor(owner), settings(owner.currentSettings()),
      order(dsp::orders[static_cast<std::size_t>(settings.order)])
{
    levels.fill(-85.0f);
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        auto& button = stages[i];
        button.setName(stageNames[i]);
        button.onClick = [this, i] { selectStage(static_cast<Stage>(i)); };
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
    for (auto* button : { &bypass, &stageOn, &bandOn, &earlier, &later })
    {
        style(*button, button != &earlier && button != &later);
        addAndMakeVisible(*button);
    }
    earlier.setTitle("Move selected stage earlier");
    later.setTitle("Move selected stage later");
    earlier.onClick = [this] { moveStage(-1); };
    later.onClick = [this] { moveStage(1); };
    mode.setTitle("Low-level mode");
    mode.setColour(ComboBox::backgroundColourId, panel);
    mode.setColour(ComboBox::textColourId, white);
    mode.setColour(ComboBox::outlineColourId, edge);
    mode.addItem("Downward expansion", 1);
    mode.addItem("Upward compression", 2);
    addAndMakeVisible(mode);
    modeLink = std::make_unique<ChoiceLink>(processor.state, param::mode, mode);
    bypassLink = std::make_unique<ButtonLink>(processor.state, param::bypass, bypass);
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        auto& slider = sliders[i];
        slider.setSliderStyle(Slider::LinearHorizontal);
        slider.setTextBoxStyle(Slider::TextBoxRight, false, 51, 24);
        slider.setColour(Slider::trackColourId, yellow.withAlpha(0.77f));
        slider.setColour(Slider::thumbColourId, yellow);
        slider.setColour(Slider::backgroundColourId, edge);
        slider.setColour(Slider::textBoxTextColourId, white);
        slider.setColour(Slider::textBoxOutlineColourId, edge);
        labels[i].setColour(Label::textColourId, muted);
        labels[i].setFont(FontOptions(11.5f));
        addChildComponent(slider);
        addChildComponent(labels[i]);
    }

    setResizable(true, true);
    setResizeLimits(620, 392, 1120, 720);
    setSize(700, 438);
    selectStage(Stage::lowLevel);
    startTimerHz(24);
}

Editor::~Editor()
{
    stopTimer();
    processor.spectrumRequested.store(false, std::memory_order_relaxed);
    if (draggedEdge != nullptr)
        draggedEdge->endChangeGesture();
}

void Editor::selectStage(Stage stage)
{
    selected = stage;
    for (std::size_t i = 0; i < stages.size(); ++i)
        stages[i].setToggleState(i == dsp::index(stage), dontSendNotification);
    processor.spectrumRequested.store(stage == Stage::multiband, std::memory_order_relaxed);
    applyOrder();
    bindControls();
}

void Editor::bindControls()
{
    const auto isMulti = selected == Stage::multiband;
    mode.setVisible(selected == Stage::lowLevel);
    const auto stagePrefix = param::stage(selected);
    const auto bandPrefix = param::band(selectedBand);
    stageLink.reset();
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

    std::size_t slot = 0;
    const auto bind = [&](const String& prefix, Control control)
    {
        sliders[slot].setTitle(control.label);
        labels[slot].setText(control.label, dontSendNotification);
        sliderLinks[slot] = std::make_unique<SliderLink>(processor.state, param::id(prefix, control.id), sliders[slot]);
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
        }
        for (const auto& control : dynamicsControls)
            bind(isMulti ? bandPrefix : stagePrefix, control);
        if (selected == Stage::compressor)
            bind(stagePrefix, { "makeup", "Makeup (dB)" });
    }
    resized();
    repaint();
}

void Editor::moveStage(int direction)
{
    auto next = order;
    const auto position = static_cast<int>(std::find(next.begin(), next.end(), selected) - next.begin());
    const auto destination = position + direction;
    if (destination < 0 || destination >= dsp::stageCount)
        return;
    std::swap(next[static_cast<std::size_t>(position)], next[static_cast<std::size_t>(destination)]);
    processor.setStripOrder(next);
    applyOrder();
    resized();
    repaint();
}

void Editor::applyOrder()
{
    settings = processor.currentSettings();
    order = dsp::orders[static_cast<std::size_t>(settings.order)];
    earlier.setEnabled(order.front() != selected);
    later.setEnabled(order.back() != selected);
    for (std::size_t i = 0; i < order.size(); ++i)
        stages[dsp::index(order[i])].position = static_cast<int>(i);
}

void Editor::resized()
{
    const auto content = getLocalBounds().reduced(14, 0);
    bypass.setBounds(content.getRight() - 72, 18, 72, 25);
    auto strip = content.withY(58).withHeight(49);
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        const auto slot = strip.removeFromLeft(strip.getWidth() / static_cast<int>(order.size() - i));
        stages[dsp::index(order[i])].setBounds(slot.withTrimmedRight(i + 1 < order.size() ? 6 : 0));
    }
    auto title = content.withY(114).withHeight(25);
    later.setBounds(title.removeFromRight(27));
    title.removeFromRight(3);
    earlier.setBounds(title.removeFromRight(27));
    title.removeFromRight(45);
    stageOn.setBounds(title.removeFromRight(72));
    title.removeFromRight(7);
    mode.setBounds(title.removeFromRight(160));

    auto bandRow = content.withY(145).withHeight(23);
    bandOn.setBounds(bandRow.removeFromRight(72));
    for (auto& band : bands)
        band.setBounds(bandRow.removeFromLeft(61).withTrimmedRight(4));

    auto controls = content.withY(getHeight() - 106).withHeight(82);
    for (std::size_t row = 0; row < 2; ++row)
    {
        auto line = controls.removeFromTop(41);
        for (std::size_t column = 0; column < 4; ++column)
        {
            const auto cell = line.removeFromLeft(line.getWidth() / static_cast<int>(4 - column)).reduced(4, 0);
            const auto index = row * 4 + column;
            labels[index].setBounds(cell.withHeight(16));
            sliders[index].setBounds(cell.withTrimmedTop(16).withHeight(24));
        }
    }
}

Rectangle<float> Editor::plot() const
{
    return getLocalBounds().reduced(14, 0).withTrimmedTop(selected == Stage::multiband ? 174 : 147)
        .withTrimmedBottom(112).toFloat();
}

void Editor::drawHeader(Graphics& g)
{
    busMark(g, 14.0f, 17.0f);
    label(g, "Short Bus", { 68, 8, 166, 31 }, 24.0f, white, Justification::centredLeft, true);
    label(g, "MASTERING DYNAMICS", { 69, 37, 190, 13 }, 10.5f, yellow, Justification::centredLeft, true);
    peakMeter(g, "IN", peaks[0], getWidth() - 264, 16);
    peakMeter(g, "OUT", peaks[1], getWidth() - 264, 33);

    label(g, inspectorNames[dsp::index(selected)], { 14, 112, 218, 29 }, 16.0f, white,
          Justification::centredLeft, true);
    label(g, "MOVE", { getWidth() - 111, 114, 38, 25 }, 10.5f, muted, Justification::centredRight, true);
}

void Editor::paint(Graphics& g)
{
    g.fillAll(background);
    drawHeader(g);
    const auto bounds = plot();
    g.setColour(graph);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    if (selected == Stage::multiband)
        drawSpectrum(g, bounds);
    else
        drawTransfer(g, bounds);

    const auto footer = Rectangle<int>(14, getHeight() - 22, getWidth() - 28, 17);
    const auto earlyLimiter = settings.limiter.enabled && order.back() != Stage::limiter;
    label(g, earlyLimiter ? "Stages after the limiter can raise the final output." : "Stereo linked   /   3 ms lookahead",
          footer, 11.0f, earlyLimiter ? yellow : muted);
    label(g, "VST3   /   v" SHORT_BUS_VERSION, footer, 11.0f, muted, Justification::centredRight);
}

void Editor::drawTransfer(Graphics& g, Rectangle<float> bounds)
{
    const auto area = bounds.withTrimmedRight(182.0f).reduced(22.0f, 18.0f);
    const auto xForDb = [&](float db) { return area.getX() + area.getWidth() * (db + 72.0f) / 78.0f; };
    for (const auto db : { -60.0f, -48.0f, -36.0f, -24.0f, -12.0f, 0.0f })
    {
        const auto x = xForDb(db);
        const auto y = levelToY(area, db);
        g.setColour(panel);
        g.drawVerticalLine(roundToInt(x), area.getY(), area.getBottom());
        g.drawHorizontalLine(roundToInt(y), area.getX(), area.getRight());
        label(g, String(roundToInt(db)), { roundToInt(x) - 12, roundToInt(area.getBottom()) + 2, 24, 12 },
              10.0f, muted, Justification::centred);
    }
    g.setColour(edge);
    g.drawLine(area.getBottomLeft().getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

    const auto isLow = selected == Stage::lowLevel;
    const auto curve = isLow ? (settings.lowMode == dsp::LowMode::expand ? dsp::Curve::expand : dsp::Curve::lift)
                             : dsp::Curve::compress;
    const auto dynamics = isLow ? settings.lowLevel : settings.compressor;
    const auto on = selected == Stage::limiter ? settings.limiter.enabled : dynamics.enabled;
    Path trace;
    for (int point = 0; point <= 156; ++point)
    {
        const auto input = -72.0f + static_cast<float>(point) * 0.5f;
        const auto output = selected == Stage::limiter
            ? std::min(input + settings.limiter.drive, settings.limiter.ceiling)
            : input + dsp::staticGain(input, dynamics, curve) + dynamics.makeup;
        if (point == 0)
            trace.startNewSubPath(xForDb(input), levelToY(area, output));
        else
            trace.lineTo(xForDb(input), levelToY(area, output));
    }
    g.setColour(on ? yellow : muted);
    g.strokePath(trace, PathStrokeType(1.7f));

    auto readout = bounds.toNearestInt().removeFromRight(175).reduced(11, 9);
    label(g, "GAIN CHANGE", readout.removeFromTop(16), 10.5f, muted, Justification::centredLeft, true);
    const auto change = meters.stages[dsp::index(selected)];
    label(g, signedDb(change), readout.removeFromTop(35), 24.0f, on ? yellow : muted);
    label(g, selected == Stage::limiter ? "Sample-peak ceiling" : "Static transfer / dBFS",
          readout.removeFromTop(18), 11.5f, muted);
}

dsp::Edges Editor::edges(int band) const
{
    const auto& range = settings.bands[static_cast<std::size_t>(band)];
    return dsp::effectiveEdges(range.lowHz, range.highHz, processor.displayRate());
}

float Editor::frequencyToX(float hz) const
{
    const auto bounds = plot().reduced(9.0f, 0.0f);
    return bounds.getX() + bounds.getWidth() * std::log(hz / dsp::minHz) / std::log(dsp::maxHz / dsp::minHz);
}

float Editor::xToFrequency(float x) const
{
    const auto bounds = plot().reduced(9.0f, 0.0f);
    const auto proportion = jlimit(0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
    return dsp::minHz * std::pow(dsp::maxHz / dsp::minHz, proportion);
}

void Editor::drawSpectrum(Graphics& g, Rectangle<float> bounds)
{
    const auto area = bounds.withTrimmedBottom(19.0f).withTrimmedTop(5.0f);
    for (auto frequency : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const auto x = frequencyToX(frequency);
        g.setColour(panel);
        g.drawVerticalLine(roundToInt(x), area.getY(), area.getBottom());
        const auto position = jlimit(bounds.getX() + 1.0f, bounds.getRight() - 25.0f, x - 12.0f);
        const auto text = frequency < 1000.0f ? String(roundToInt(frequency)) : String(roundToInt(frequency / 1000.0f)) + "k";
        label(g, text, { roundToInt(position), roundToInt(area.getBottom()) + 3, 24, 12 }, 10.0f, muted,
              Justification::centred);
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
        const auto y = area.getY() - jlimit(-85.0f, 0.0f, levels[bin]) * area.getHeight() / 85.0f;
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
                g.fillRoundedRectangle(handle - 3.0f, area.getY() + 2.0f, 7.0f, 17.0f, 2.0f);
        }
        if (on)
            g.fillRect(x, area.getY() + 23.0f, end - x,
                jlimit(0.0f, 1.0f, -meters.bands[n] / 24.0f) * (area.getHeight() - 23.0f));
    }
    label(g, signedDb(meters.bands[static_cast<std::size_t>(selectedBand)]),
          area.toNearestInt().withHeight(16).reduced(13, 0), 11.0f,
          bandColours[static_cast<std::size_t>(selectedBand)], Justification::centredRight, true);
}

void Editor::updateSpectrum()
{
    if (!processor.readSpectrum(fftBuffer[0].data(), fftBuffer[1].data()))
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

void Editor::timerCallback()
{
    const auto previous = order;
    applyOrder();
    if (order != previous)
        resized();
    meters = processor.readMeters();
    peaks = processor.readPeaks();
    const std::array on { settings.lowLevel.enabled, settings.multiEnabled, settings.compressor.enabled, settings.limiter.enabled };
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        stages[i].gain = meters.stages[i];
        stages[i].enabled = on[i];
        stages[i].repaint();
    }
    if (selected == Stage::multiband)
        updateSpectrum();
    repaint();
}

void Editor::mouseDown(const MouseEvent& event)
{
    if (selected != Stage::multiband || !plot().contains(event.position))
        return;
    const auto span = edges(selectedBand);
    const auto lowDistance = std::abs(event.position.x - frequencyToX(span.low));
    const auto highDistance = std::abs(event.position.x - frequencyToX(span.high));
    if (std::min(lowDistance, highDistance) > 12.0f)
        return;
    dragLowEdge = lowDistance <= highDistance;
    draggedEdge = processor.state.getParameter(param::id(param::band(selectedBand), dragLowEdge ? "low_hz" : "high_hz"));
    draggedEdge->beginChangeGesture();
}

void Editor::mouseDrag(const MouseEvent& event)
{
    if (draggedEdge == nullptr)
        return;
    const auto span = edges(selectedBand);
    const auto hz = xToFrequency(event.position.x);
    const auto next = dragLowEdge ? std::floor(std::min(hz, span.high / dsp::edgeRatio))
                                  : std::ceil(std::max(hz, span.low * dsp::edgeRatio));
    draggedEdge->setValueNotifyingHost(draggedEdge->convertTo0to1(jlimit(dsp::minHz, dsp::maxHz, next)));
}

void Editor::mouseUp(const MouseEvent&)
{
    if (draggedEdge != nullptr)
    {
        draggedEdge->endChangeGesture();
        draggedEdge = nullptr;
    }
}

} // namespace shortbus
