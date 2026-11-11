// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Processor.h"
#include <juce_dsp/juce_dsp.h>
#include <optional>

namespace shortbus
{
class PresetBrowser;
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
        void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float position,
                              float min, float max, juce::Slider::SliderStyle, juce::Slider&) override;
        void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position,
                              float startAngle, float endAngle, juce::Slider&) override;

    private:
        juce::Typeface::Ptr regular, bold;
    };

    struct StageButton : juce::Button
    {
        StageButton() : Button(juce::String()) {}
        void paintButton(juce::Graphics&, bool hovered, bool pressed) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        std::function<void(const juce::MouseEvent&, int)> onReorder;
        int position = 0;
        float gain = 0.0f;
        bool enabled = false;
        bool isAnalyzer = false;
        bool showsGain = true;
        bool showPosition = true;
        bool dragging = false;
    };

    struct BypassButton final : juce::Button
    {
        BypassButton();
        bool hitTest(int x, int y) override;
        void paintButton(juce::Graphics&, bool hovered, bool pressed) override;
    };
    struct PresetButton final : juce::Button
    {
        PresetButton() : Button("Presets") {}
        void paintButton(juce::Graphics&, bool, bool) override;
    };
    struct TileDrag
    {
        std::size_t tile;
        Processor::TileOrder origin, preview;
        bool active = false, valid = false;
    };

    enum class GraphControl { threshold, ceiling, makeup, ratio, range, bandLow, bandHigh, bandNode, bandRange };

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
        juce::RangedAudioParameter* liftParameter = nullptr;
    };

    void timerCallback() override;
    void selectTile(std::size_t);
    void bindControls();
    void refreshSettings();
    void limitFrequencySlider(juce::Slider&, dsp::BandEdge);
    void refreshBandControls();
    float limitFrequency(std::size_t band, dsp::BandEdge, float requested) const;
    void animateLights();
    void moveStage(int direction);
    void reorderTile(std::size_t, const juce::MouseEvent&, int phase);
    void finishTileDrag(bool commit);
    void togglePresets();
    void animatePresetDoor(double elapsedMs);
    juce::Rectangle<int> busBounds() const;
    void applyOrder();
    void updateSpectrum();
    void updateTonalSpectrum();
    std::uint32_t currentTonalKey() const;
    bool tonalSelected() const { return selectedTile == tonalTile; }
    bool geqSelected() const { return !tonalSelected() && selected == dsp::Stage::geq; }
    PresetScope presetScope() const;
    juce::Rectangle<float> plot() const;
    juce::Rectangle<int> geqControls() const;
    juce::Rectangle<float> transferArea() const;
    juce::Rectangle<float> spectrumArea() const;
    juce::Point<float> bandNode(int band) const;
    juce::Rectangle<float> bandRangeBar(int band) const;
    std::optional<int> bandAt(float x) const;
    std::optional<GraphHit> graphHit(juce::Point<float>) const;
    void finishGraphDrag();
    void drawBus(juce::Graphics&);
    void drawHeader(juce::Graphics&);
    void drawBumper(juce::Graphics&);
    void drawPeakMeter(juce::Graphics&, const char* name, float peak, int x, int y);
    void drawVerticalMeter(juce::Graphics&, const char* name, juce::Rectangle<float>, float db, bool action = false);
    void drawSpectrum(juce::Graphics&, juce::Rectangle<float>);
    void drawTransfer(juce::Graphics&, juce::Rectangle<float>);
    void drawTonalBalance(juce::Graphics&, juce::Rectangle<float>);
    void drawGraphicEq(juce::Graphics&);
    void drawFineTone(juce::Graphics&, juce::Rectangle<float>, const dsp::TonalCurve& target,
                      const dsp::TonalCurve& live, int count);
    void drawBroadTone(juce::Graphics&, juce::Rectangle<float>, const dsp::TonalCurve& target,
                       const dsp::TonalCurve& live, int count);
    float frequencyToX(float) const;
    dsp::Edges edges(int band) const;

    using SliderLink = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonLink = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ChoiceLink = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    static constexpr std::size_t controlCount = 8;
    static constexpr std::size_t tonalTile = dsp::stageCount;
    static constexpr std::size_t tileCount = tonalTile + 1;
    BusLookAndFeel theme;
    Processor& processor;
    dsp::Settings settings;
    dsp::BandSpans bandSpans {};
    dsp::Metering meters;
    std::array<float, 2> peaks {};
    std::array<float, 2> lampGlow {};
    double lastLampTick = juce::Time::getMillisecondCounterHiRes();
    double brakeHoldUntil = 0.0;
    Processor::TileOrder order {};
    std::size_t selectedTile = 0;
    dsp::Stage selected = dsp::Stage::lowLevel;
    dsp::CompressorMode boundCompressorMode = dsp::CompressorMode::clean;
    int selectedBand = 0;
    std::array<StageButton, tileCount> stages;
    std::array<juce::TextButton, dsp::bandCount> bands;
    BypassButton bypass;
    juce::TextButton stageOn { "Stage on" }, bandOn { "Band on" };
    juce::TextButton earlier { "<" }, later { ">" };
    juce::TextButton captureReference { "Capture" };
    juce::TextButton flatEq { "Flat EQ" };
    juce::ComboBox mode, targetChoice, tonalMode, compressorModeBox;
    juce::Slider rtaSensitivity, saturationDial;
    std::array<juce::Slider, dsp::geqBandCount> geqFaders;
    std::array<juce::Slider, controlCount> sliders;
    std::array<juce::Label, controlCount> labels;
    std::array<std::unique_ptr<SliderLink>, controlCount> sliderLinks;
    std::array<std::unique_ptr<SliderLink>, dsp::geqBandCount> geqLinks;
    std::unique_ptr<SliderLink> sensitivityLink, saturationLink;
    std::unique_ptr<ButtonLink> bypassLink, stageLink, bandLink;
    std::unique_ptr<ChoiceLink> modeLink, targetLink, tonalModeLink, compressorModeLink;
    std::optional<GraphDrag> graphDrag;
    std::optional<TileDrag> tileDrag;
    PresetButton presetsButton;
    std::unique_ptr<PresetBrowser> presetBrowser;
    bool presetDoorTarget = false, presetDoorAllocated = false;
    float presetDoorProgress = 0.0f;

    juce::dsp::FFT fft { Processor::fftOrder };
    juce::dsp::WindowingFunction<float> window { Processor::fftSize,
                                                juce::dsp::WindowingFunction<float>::hann, false };
    std::array<std::array<float, Processor::fftSize * 2>, 2> fftBuffer {};
    std::array<float, Processor::fftSize / 2> levels {};
    juce::dsp::FFT tonalFft { Processor::tonalFftOrder };
    juce::dsp::WindowingFunction<float> tonalWindow { Processor::tonalFftSize,
                                                     juce::dsp::WindowingFunction<float>::hann, false };
    std::array<std::array<float, Processor::tonalFftSize * 2>, 2> tonalBuffer {};
    dsp::TonalCurve tonalAverage {};
    std::uint32_t tonalKey = 0;
    double lastTonalFrame = 0.0;
};
} // namespace shortbus
