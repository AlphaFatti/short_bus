// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "dsp/Rack.h"
#include "dsp/TonalBalance.h"
#include "Presets.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

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
inline constexpr auto geq = "geq";
inline constexpr auto geqEnabled = "geq_enabled";
inline constexpr auto saturation = "geq_saturation";
inline constexpr auto geqPosition = "rack_geq_position";
inline constexpr auto bandLift = "lift";
inline constexpr auto rtaPosition = "rta_position";
inline constexpr auto rtaTarget = "rta_target";
inline constexpr auto rtaView = "rta_view";
inline constexpr auto rtaFloor = "rta_floor";
inline constexpr auto rtaAfterGeq = "rack_rta_after_geq";
inline constexpr auto compMode = "comp_mode";
inline constexpr auto optoAttack = "comp_opto_attack";
inline constexpr auto optoRelease = "comp_opto_release";
inline constexpr auto fetAttack = "comp_fet_attack";
inline constexpr auto fetRelease = "comp_fet_release";

inline juce::String id(const juce::String& prefix, const char* control) { return prefix + "_" + control; }
inline juce::String band(int i) { return "band" + juce::String(i + 1); }
inline juce::String geqGain(std::size_t i) { return "geq_band_" + juce::String(static_cast<int>(i + 1)); }
inline juce::String stage(dsp::Stage stage)
{
    constexpr std::array prefixes { low, multi, comp, limit, geq };
    return prefixes[dsp::index(stage)];
}
} // namespace param

class Processor final : public juce::AudioProcessor
{
public:
    enum class SpectrumView { none, output, tonal };
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int tonalFftOrder = 14;
    static constexpr int tonalFftSize = 1 << tonalFftOrder;

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
    using TileOrder = std::array<std::size_t, dsp::stageCount + 1>;
    TileOrder tileOrder() const;
    void setTileOrder(const TileOrder&);
    int rtaPosition() const;
    void setRtaPosition(int position);
    std::optional<dsp::TonalReference> tonalReference() const;
    void setTonalReference(std::optional<dsp::TonalReference>);
    juce::ValueTree presetState(PresetScope);
    juce::Result applyPreset(const juce::ValueTree&, PresetScope);
    unsigned presetRecallSafes() const { return recallSafes.load(std::memory_order_relaxed); }
    void setPresetRecallSafes(unsigned mask) { recallSafes.store(mask & ((1u << presetSafeCount) - 1u), std::memory_order_relaxed); }
    dsp::Metering readMeters() const;
    std::array<float, 2> readPeaks() const;
    bool isBypassed() const;
    bool consumeOutputPeakEvent() { return outputPeakPending.exchange(false, std::memory_order_relaxed); }
    std::uint32_t spectrumKey(SpectrumView, int orderIndex, int position, bool bypassed) const;
    bool readSpectrum(float* left, float* right, int count, std::uint32_t key);
    void discardSpectrum() { spectrumFifo.finishedRead(spectrumFifo.getNumReady()); }
    float displayRate() const { return meterRate.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState state;
    std::atomic<SpectrumView> spectrumRequested { SpectrumView::none };

private:
    using Atomic = std::atomic<float>;
    struct DynamicsRefs
    {
        Atomic *enabled, *threshold, *ratio, *attack, *release, *knee, *range, *makeup;
        dsp::DynamicsSettings read() const;
    };
    struct BandRefs
    {
        Atomic *low, *high, *lift;
        DynamicsRefs dynamics;
    };
    struct StageMeter
    {
        Atomic inputPeak {}, gainDb {}, outputPeak {};
    };
    struct SpectrumSample
    {
        dsp::Frame signal;
        std::uint32_t key, sequence;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    DynamicsRefs dynamicsRefs(const juce::String& prefix, bool withMakeup = false);
    int stripOrderIndex() const;
    int rtaPositionForOrder(int orderIndex) const;
    void setRoute(const dsp::Order&, int tap);
    void render(juce::AudioBuffer<float>&, bool hostBypass);

    DynamicsRefs lowRefs {}, compRefs {};
    std::array<BandRefs, dsp::bandCount> bandRefs {};
    Atomic *mode = nullptr, *order = nullptr, *bypass = nullptr, *multiOn = nullptr;
    Atomic *rtaSlot = nullptr, *geqSlot = nullptr, *rtaAfterEq = nullptr;
    Atomic *compressorMode = nullptr, *optoAttack = nullptr, *optoRelease = nullptr;
    Atomic *fetAttack = nullptr, *fetRelease = nullptr;
    Atomic *limitOn = nullptr, *ceiling = nullptr, *drive = nullptr, *limitRelease = nullptr;
    Atomic *geqOn = nullptr, *saturation = nullptr;
    std::array<Atomic*, dsp::geqBandCount> geqGains {};
    dsp::Rack rack;
    std::array<StageMeter, dsp::stageCount> stageMeters {};
    std::array<Atomic, dsp::bandCount> bandMeter {};
    Atomic inputPeak {}, outputPeak {}, meterRate { 48000.0f };
    std::atomic<bool> hostBypassed { false }, outputPeakPending { false };
    juce::AbstractFifo spectrumFifo { tonalFftSize * 2 };
    std::array<SpectrumSample, tonalFftSize * 2> spectrum {};
    std::atomic<std::uint32_t> spectrumEpoch { 0 };
    std::uint32_t spectrumSequence = 0;
    mutable juce::CriticalSection referenceLock;
    std::optional<dsp::TonalReference> reference;
    mutable juce::CriticalSection presetLock;
    std::atomic<unsigned> recallSafes { defaultPresetSafes };
    std::atomic<std::uint32_t> settingsRevision { 0 };
    dsp::Settings renderSettings;
    int renderRtaPosition = dsp::stageCount;
};
} // namespace shortbus
