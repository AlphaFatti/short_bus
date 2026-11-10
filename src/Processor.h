// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/Rack.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace shortbus
{
namespace param
{
inline constexpr auto bypass = "bypass";
inline constexpr auto order = "strip_order";
inline constexpr auto mode = "low_mode";
inline constexpr auto low = "low";
inline constexpr auto multi = "multi";
inline constexpr auto comp = "comp";
inline constexpr auto limit = "limit";

inline juce::String id(const juce::String& prefix, const char* control) { return prefix + "_" + control; }
inline juce::String band(int i) { return "band" + juce::String(i + 1); }
inline juce::String stage(dsp::Stage stage)
{
    constexpr std::array prefixes { low, multi, comp, limit };
    return prefixes[dsp::index(stage)];
}
} // namespace param

class Processor final : public juce::AudioProcessor
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;

    Processor();
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Short Bus"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    dsp::Settings currentSettings() const;
    void setStripOrder(const dsp::Order&);
    dsp::Metering readMeters() const;
    std::array<float, 2> readPeaks() const;
    bool readSpectrum(float* left, float* right);
    float displayRate() const { return meterRate.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState state;
    std::atomic<bool> spectrumRequested { false };

private:
    using Atomic = std::atomic<float>;
    struct DynamicsRefs
    {
        Atomic *enabled, *threshold, *ratio, *attack, *release, *knee, *range, *makeup;
        dsp::DynamicsSettings read() const;
    };
    struct BandRefs
    {
        Atomic *low, *high;
        DynamicsRefs dynamics;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    DynamicsRefs dynamicsRefs(const juce::String& prefix, bool withMakeup = false);
    void render(juce::AudioBuffer<float>&, bool hostBypass);

    DynamicsRefs lowRefs {}, compRefs {};
    std::array<BandRefs, dsp::bandCount> bandRefs {};
    Atomic *mode = nullptr, *order = nullptr, *bypass = nullptr, *multiOn = nullptr;
    Atomic *limitOn = nullptr, *ceiling = nullptr, *drive = nullptr, *limitRelease = nullptr;
    dsp::Rack rack;
    std::array<Atomic, dsp::stageCount> stageMeter {};
    std::array<Atomic, dsp::bandCount> bandMeter {};
    Atomic inputPeak {}, outputPeak {}, meterRate { 48000.0f };
    juce::AbstractFifo spectrumFifo { fftSize * 4 };
    std::array<dsp::Frame, fftSize * 4> spectrum {};
};
} // namespace shortbus
