// SPDX-License-Identifier: AGPL-3.0-only
#include "Processor.h"
#include "Editor.h"

namespace shortbus
{
using namespace juce;

namespace
{
using Layout = AudioProcessorValueTreeState::ParameterLayout;

void addFloat(Layout& layout, String id, String title, NormalisableRange<float> range, float initial)
{
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, title, range, initial));
}

void addToggle(Layout& layout, String id, String title, bool initial)
{
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, title, initial));
}

void addDynamics(Layout& layout, String prefix, String name, dsp::DynamicsSettings defaults, bool makeup = false)
{
    const auto add = [&](const char* control, const char* label, NormalisableRange<float> range, float initial)
    {
        addFloat(layout, param::id(prefix, control), name + " " + label, range, initial);
    };
    addToggle(layout, param::id(prefix, "enabled"), name + " Enabled", defaults.enabled);
    add("threshold", "Threshold", {-70.0f, 0.0f, 0.1f}, defaults.threshold);
    add("ratio", "Ratio", {1.0f, 12.0f, 0.1f, 0.5f}, defaults.ratio);
    add("attack", "Attack", {0.5f, 250.0f, 0.1f, 0.35f}, defaults.attack);
    add("release", "Release", {10.0f, 2500.0f, 1.0f, 0.45f}, defaults.release);
    add("knee", "Knee", {0.0f, 18.0f, 0.1f}, defaults.knee);
    add("range", "Range", {0.0f, 24.0f, 0.1f}, defaults.range);
    if (makeup)
        add("makeup", "Makeup", {-12.0f, 18.0f, 0.1f}, defaults.makeup);
}

float read(const std::atomic<float>* value) { return value->load(std::memory_order_relaxed); }
} // namespace

AudioProcessorValueTreeState::ParameterLayout Processor::makeLayout()
{
    Layout layout;
    addToggle(layout, param::bypass, "Bypass", false);
    constexpr std::array<const char*, dsp::stageCount> names { "Low Level", "5-Band", "Compressor", "Limiter" };
    StringArray orders;
    for (const auto& order : dsp::orders)
    {
        StringArray stages;
        for (auto stage : order)
            stages.add(names[dsp::index(stage)]);
        orders.add(stages.joinIntoString(" > "));
    }
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { param::order, 1 }, "Strip Order", orders, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { param::mode, 1 }, "Low-Level Mode",
        StringArray { "Downward Expansion", "Upward Compression" }, 0));

    addDynamics(layout, param::low, "Low Level", dsp::lowDefaults);
    addToggle(layout, param::id(param::multi, "enabled"), "5-Band Enabled", false);
    NormalisableRange<float> frequencies { dsp::minHz, dsp::maxHz, 1.0f };
    frequencies.setSkewForCentre(850.0f);
    for (int i = 0; i < dsp::bandCount; ++i)
    {
        const auto& defaults = dsp::bandDefaults[static_cast<std::size_t>(i)];
        const auto prefix = param::band(i);
        const auto name = "Band " + String(i + 1);
        addFloat(layout, param::id(prefix, "low_hz"), name + " Low Edge", frequencies, defaults.lowHz);
        addFloat(layout, param::id(prefix, "high_hz"), name + " High Edge", frequencies, defaults.highHz);
        addDynamics(layout, prefix, name, defaults.dynamics);
    }
    addDynamics(layout, param::comp, "Compressor", dsp::compDefaults, true);
    addToggle(layout, param::id(param::limit, "enabled"), "Limiter Enabled", false);
    addFloat(layout, param::id(param::limit, "ceiling"), "Limiter Ceiling", {-18.0f, 0.0f, 0.1f}, -1.0f);
    addFloat(layout, param::id(param::limit, "drive"), "Limiter Drive", {-12.0f, 18.0f, 0.1f}, 0.0f);
    addFloat(layout, param::id(param::limit, "release"), "Limiter Release", {20.0f, 1000.0f, 1.0f, 0.45f}, 100.0f);
    return layout;
}

Processor::Processor()
    : AudioProcessor(BusesProperties().withInput("Input", AudioChannelSet::stereo(), true)
                                      .withOutput("Output", AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "ShortBusState", makeLayout())
{
    lowRefs = dynamicsRefs(param::low);
    compRefs = dynamicsRefs(param::comp, true);
    for (int i = 0; i < dsp::bandCount; ++i)
    {
        const auto prefix = param::band(i);
        bandRefs[static_cast<std::size_t>(i)] = {
            state.getRawParameterValue(param::id(prefix, "low_hz")),
            state.getRawParameterValue(param::id(prefix, "high_hz")),
            dynamicsRefs(prefix)
        };
    }
    mode = state.getRawParameterValue(param::mode);
    order = state.getRawParameterValue(param::order);
    bypass = state.getRawParameterValue(param::bypass);
    multiOn = state.getRawParameterValue(param::id(param::multi, "enabled"));
    limitOn = state.getRawParameterValue(param::id(param::limit, "enabled"));
    ceiling = state.getRawParameterValue(param::id(param::limit, "ceiling"));
    drive = state.getRawParameterValue(param::id(param::limit, "drive"));
    limitRelease = state.getRawParameterValue(param::id(param::limit, "release"));
}

Processor::DynamicsRefs Processor::dynamicsRefs(const String& prefix, bool withMakeup)
{
    const auto at = [this, &prefix](const char* key) { return state.getRawParameterValue(param::id(prefix, key)); };
    return { at("enabled"), at("threshold"), at("ratio"), at("attack"), at("release"),
             at("knee"), at("range"), withMakeup ? at("makeup") : nullptr };
}

dsp::DynamicsSettings Processor::DynamicsRefs::read() const
{
    const auto load = [](const Atomic* value) { return value->load(std::memory_order_relaxed); };
    return { load(enabled) > 0.5f, load(threshold), load(ratio), load(attack), load(release),
             load(knee), load(range), makeup != nullptr ? load(makeup) : 0.0f };
}

dsp::Settings Processor::currentSettings() const
{
    dsp::Settings settings;
    settings.bypass = read(bypass) > 0.5f;
    settings.order = std::clamp(roundToInt(read(order)), 0, dsp::orderCount - 1);
    settings.lowMode = read(mode) < 0.5f ? dsp::LowMode::expand : dsp::LowMode::lift;
    settings.lowLevel = lowRefs.read();
    settings.compressor = compRefs.read();
    settings.multiEnabled = read(multiOn) > 0.5f;
    for (std::size_t i = 0; i < bandRefs.size(); ++i)
    {
        const auto& refs = bandRefs[i];
        settings.bands[i] = { read(refs.low), read(refs.high), refs.dynamics.read() };
    }
    settings.limiter = { read(limitOn) > 0.5f, read(ceiling), read(drive), read(limitRelease) };
    return settings;
}

void Processor::setStripOrder(const dsp::Order& requested)
{
    const auto position = std::find(dsp::orders.begin(), dsp::orders.end(), requested);
    if (position == dsp::orders.end())
        return;
    auto* parameter = state.getParameter(param::order);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(position - dsp::orders.begin())));
    parameter->endChangeGesture();
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto input = layout.getMainInputChannelSet();
    return input == layout.getMainOutputChannelSet()
        && (input == AudioChannelSet::mono() || input == AudioChannelSet::stereo());
}

void Processor::prepareToPlay(double rate, int)
{
    rack.prepare(rate);
    setLatencySamples(rack.latency());
    meterRate.store(static_cast<float>(rate), std::memory_order_relaxed);
    inputPeak.store(0.0f, std::memory_order_relaxed);
    outputPeak.store(0.0f, std::memory_order_relaxed);
    for (auto& meter : stageMeter)
        meter.store(0.0f, std::memory_order_relaxed);
    for (auto& meter : bandMeter)
        meter.store(0.0f, std::memory_order_relaxed);
}

double Processor::getTailLengthSeconds() const
{
    return static_cast<double>(getLatencySamples()) / static_cast<double>(displayRate());
}

void Processor::processBlock(AudioBuffer<float>& audio, MidiBuffer&)
{
    render(audio, false);
}

void Processor::processBlockBypassed(AudioBuffer<float>& audio, MidiBuffer&)
{
    render(audio, true);
}

void Processor::render(AudioBuffer<float>& audio, bool hostBypass)
{
    ScopedNoDenormals noDenormals;
    const auto frames = audio.getNumSamples();
    if (frames == 0 || audio.getNumChannels() == 0)
        return;

    auto settings = currentSettings();
    settings.bypass = settings.bypass || hostBypass;
    rack.configure(settings);
    const auto region = spectrumFifo.write(spectrumRequested.load(std::memory_order_relaxed) ? frames : 0);
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getNumChannels() == 2 ? audio.getWritePointer(1) : nullptr;
    float in = 0.0f, out = 0.0f;
    const auto finite = [](float value) { return std::isfinite(value) ? value : 0.0f; };
    for (int i = 0; i < frames; ++i)
    {
        const auto l = finite(left[i]);
        const dsp::Frame input { l, right != nullptr ? finite(right[i]) : l };
        const auto output = rack.process(input);
        in = std::max(in, dsp::peak(input));
        out = std::max(out, dsp::peak(output));
        left[i] = output[0];
        if (right != nullptr)
            right[i] = output[1];
        if (i < region.blockSize1)
            spectrum[static_cast<std::size_t>(region.startIndex1 + i)] = output;
        else if (i < region.blockSize1 + region.blockSize2)
            spectrum[static_cast<std::size_t>(region.startIndex2 + i - region.blockSize1)] = output;
    }

    const auto decay = std::exp(-static_cast<float>(frames) / (0.25f * displayRate()));
    inputPeak.store(std::max(in, read(&inputPeak) * decay), std::memory_order_relaxed);
    outputPeak.store(std::max(out, read(&outputPeak) * decay), std::memory_order_relaxed);
    const auto metering = rack.meters();
    for (std::size_t i = 0; i < stageMeter.size(); ++i)
        stageMeter[i].store(metering.stages[i], std::memory_order_relaxed);
    for (std::size_t i = 0; i < bandMeter.size(); ++i)
        bandMeter[i].store(metering.bands[i], std::memory_order_relaxed);
}

dsp::Metering Processor::readMeters() const
{
    dsp::Metering result;
    for (std::size_t i = 0; i < stageMeter.size(); ++i)
        result.stages[i] = read(&stageMeter[i]);
    for (std::size_t i = 0; i < bandMeter.size(); ++i)
        result.bands[i] = read(&bandMeter[i]);
    return result;
}

std::array<float, 2> Processor::readPeaks() const
{
    return { read(&inputPeak), read(&outputPeak) };
}

bool Processor::readSpectrum(float* left, float* right)
{
    const auto available = spectrumFifo.getNumReady();
    if (available < fftSize)
        return false;
    const auto region = spectrumFifo.read(available);
    const auto skip = available - fftSize;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto offset = skip + i;
        const auto position = offset < region.blockSize1
            ? region.startIndex1 + offset : region.startIndex2 + offset - region.blockSize1;
        const auto frame = spectrum[static_cast<std::size_t>(position)];
        left[i] = frame[0];
        right[i] = frame[1];
    }
    return true;
}

void Processor::getStateInformation(MemoryBlock& destination)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void Processor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(ValueTree::fromXml(*xml));
}

AudioProcessorParameter* Processor::getBypassParameter() const
{
    return state.getParameter(param::bypass);
}

AudioProcessorEditor* Processor::createEditor()
{
    return new Editor(*this);
}

} // namespace shortbus

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new shortbus::Processor();
}
