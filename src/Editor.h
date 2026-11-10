// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Processor.h"
#include <juce_dsp/juce_dsp.h>

namespace shortbus
{
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    struct StageButton : juce::Button
    {
        StageButton() : Button(juce::String()) {}
        void paintButton(juce::Graphics&, bool hovered, bool pressed) override;
        int position = 0;
        float gain = 0.0f;
        bool enabled = false;
    };

    void timerCallback() override;
    void selectStage(dsp::Stage);
    void bindControls();
    void moveStage(int direction);
    void applyOrder();
    void updateSpectrum();
    juce::Rectangle<float> plot() const;
    void drawHeader(juce::Graphics&);
    void drawSpectrum(juce::Graphics&, juce::Rectangle<float>);
    void drawTransfer(juce::Graphics&, juce::Rectangle<float>);
    float frequencyToX(float) const;
    float xToFrequency(float) const;
    dsp::Edges edges(int band) const;

    using SliderLink = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonLink = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ChoiceLink = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    static constexpr std::size_t controlCount = 8;
    Processor& processor;
    dsp::Settings settings;
    dsp::Metering meters;
    std::array<float, 2> peaks {};
    dsp::Order order = dsp::defaultOrder;
    dsp::Stage selected = dsp::Stage::lowLevel;
    int selectedBand = 0;
    std::array<StageButton, dsp::stageCount> stages;
    std::array<juce::TextButton, dsp::bandCount> bands;
    juce::TextButton bypass { "Bypass" }, stageOn { "Stage on" }, bandOn { "Band on" };
    juce::TextButton earlier { "<" }, later { ">" };
    juce::ComboBox mode;
    std::array<juce::Slider, controlCount> sliders;
    std::array<juce::Label, controlCount> labels;
    std::array<std::unique_ptr<SliderLink>, controlCount> sliderLinks;
    std::unique_ptr<ButtonLink> bypassLink, stageLink, bandLink;
    std::unique_ptr<ChoiceLink> modeLink;
    juce::RangedAudioParameter* draggedEdge = nullptr;
    bool dragLowEdge = false;

    juce::dsp::FFT fft { Processor::fftOrder };
    juce::dsp::WindowingFunction<float> window { Processor::fftSize,
                                                juce::dsp::WindowingFunction<float>::hann, false };
    std::array<std::array<float, Processor::fftSize * 2>, 2> fftBuffer {};
    std::array<float, Processor::fftSize / 2> levels {};
};
} // namespace shortbus
