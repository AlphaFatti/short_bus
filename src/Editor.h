// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Processor.h"
#include <juce_dsp/juce_dsp.h>
#include <optional>

namespace shortbus
{
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    class BusLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        BusLookAndFeel();
        juce::Font font(float size, bool strong = false) const;
        juce::Font getTextButtonFont(juce::TextButton&, int height) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        juce::Font getLabelFont(juce::Label&) override;
        juce::Font getPopupMenuFont() override;

    private:
        juce::Typeface::Ptr regular, bold;
    };

    struct StageButton : juce::Button
    {
        StageButton() : Button(juce::String()) {}
        void paintButton(juce::Graphics&, bool hovered, bool pressed) override;
        int position = 0;
        float gain = 0.0f;
        bool enabled = false;
    };

    struct BypassButton final : juce::Button
    {
        BypassButton();
        bool hitTest(int x, int y) override;
        void paintButton(juce::Graphics&, bool hovered, bool pressed) override;
    };

    enum class GraphControl { threshold, ceiling, makeup, ratio, range, bandLow, bandHigh, bandNode };

    struct GraphHit
    {
        GraphControl control;
        int band = 0;
    };

    struct GraphDrag
    {
        GraphHit target;
        juce::Point<float> start;
        juce::RangedAudioParameter* parameter;
        float initialValue;
        float startEdgeHz = 0.0f;
        float ratioDepthDb = 24.0f;
        float rangeDirection = 1.0f;
    };

    void timerCallback() override;
    void selectStage(dsp::Stage);
    void bindControls();
    void refreshSettings();
    void limitFrequencySlider(juce::Slider&, dsp::BandEdge);
    void refreshFrequencySliders();
    float limitFrequency(std::size_t band, dsp::BandEdge, float requested) const;
    void animateBypassLights();
    void moveStage(int direction);
    void applyOrder();
    void updateSpectrum();
    juce::Rectangle<float> plot() const;
    juce::Rectangle<float> transferArea() const;
    juce::Rectangle<float> spectrumArea() const;
    juce::Point<float> bandNode(int band) const;
    std::optional<GraphHit> graphHit(juce::Point<float>) const;
    void finishGraphDrag();
    void drawBus(juce::Graphics&);
    void drawHeader(juce::Graphics&);
    void drawBumper(juce::Graphics&);
    void drawPeakMeter(juce::Graphics&, const char* name, float peak, int x, int y);
    void drawSpectrum(juce::Graphics&, juce::Rectangle<float>);
    void drawTransfer(juce::Graphics&, juce::Rectangle<float>);
    float frequencyToX(float) const;
    dsp::Edges edges(int band) const;

    using SliderLink = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonLink = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ChoiceLink = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    static constexpr std::size_t controlCount = 8;
    BusLookAndFeel theme;
    Processor& processor;
    dsp::Settings settings;
    dsp::BandSpans bandSpans {};
    dsp::Metering meters;
    std::array<float, 2> peaks {};
    std::array<float, 2> lampGlow {};
    double lastLampTick = juce::Time::getMillisecondCounterHiRes();
    dsp::Order order = dsp::defaultOrder;
    dsp::Stage selected = dsp::Stage::lowLevel;
    int selectedBand = 0;
    std::array<StageButton, dsp::stageCount> stages;
    std::array<juce::TextButton, dsp::bandCount> bands;
    BypassButton bypass;
    juce::TextButton stageOn { "Stage on" }, bandOn { "Band on" };
    juce::TextButton earlier { "<" }, later { ">" };
    juce::ComboBox mode;
    std::array<juce::Slider, controlCount> sliders;
    std::array<juce::Label, controlCount> labels;
    std::array<std::unique_ptr<SliderLink>, controlCount> sliderLinks;
    std::unique_ptr<ButtonLink> bypassLink, stageLink, bandLink;
    std::unique_ptr<ChoiceLink> modeLink;
    std::optional<GraphDrag> graphDrag;

    juce::dsp::FFT fft { Processor::fftOrder };
    juce::dsp::WindowingFunction<float> window { Processor::fftSize,
                                                juce::dsp::WindowingFunction<float>::hann, false };
    std::array<std::array<float, Processor::fftSize * 2>, 2> fftBuffer {};
    std::array<float, Processor::fftSize / 2> levels {};
};
} // namespace shortbus
