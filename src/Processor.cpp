// SPDX-License-Identifier: AGPL-3.0-only
#include "Processor.h"
#include "Editor.h"

namespace shortbus
{
using namespace juce;

namespace
{
using Layout = AudioProcessorValueTreeState::ParameterLayout;
// Include rounding at the default limiter ceiling.
const auto brakeThreshold = dsp::gainFromDb(-1.0f) - 1.0e-6f;
constexpr auto referenceProperty = "tonalReference";
constexpr auto referenceBandsProperty = "tonalReferenceBands";

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
    add("range", "Range", {0.0f, dsp::maxRangeDb, 0.1f}, defaults.range);
    if (makeup)
        add("makeup", "Makeup", {-12.0f, 18.0f, 0.1f}, defaults.makeup);
}

float read(const std::atomic<float>* value) { return value->load(std::memory_order_relaxed); }

void setDiscreteValue(AudioProcessorValueTreeState& state, const char* id, int value)
{
    auto* parameter = state.getParameter(id);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(value)));
    parameter->endChangeGesture();
}

std::optional<dsp::TonalReference> readReference(const ValueTree& restored)
{
    const auto count = static_cast<int>(restored.getProperty(referenceBandsProperty, 0));
    const auto values = StringArray::fromTokens(restored.getProperty(referenceProperty).toString(), " ", "");
    if (count < 1 || count > dsp::tonalBandCount || values.size() != dsp::tonalBandCount)
        return {};
    dsp::TonalCurve power {};
    for (int i = 0; i < count; ++i)
    {
        const auto value = values[i].getFloatValue();
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            return {};
        power[static_cast<std::size_t>(i)] = value;
    }
    if (dsp::tonalEnergy(power) <= 0.0f)
        return {};
    return dsp::TonalReference { dsp::normalizedTone(power), count };
}
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
    for (int i = 0; i < dsp::bandCount; ++i)
        addToggle(layout, param::id(param::band(i), param::bandLift), "Band " + String(i + 1) + " Upward Compression", false);
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { param::rtaPosition, 1 }, "RTA Position",
                                                   0, dsp::stageCount, dsp::stageCount));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { param::rtaTarget, 1 }, "RTA Target",
        StringArray { "Balanced", "Bass-forward", "Speech", "Captured" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { param::rtaView, 1 }, "RTA View",
        StringArray { "Fine", "Broad" }, 0));
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
            state.getRawParameterValue(param::id(prefix, param::bandLift)),
            dynamicsRefs(prefix)
        };
    }
    mode = state.getRawParameterValue(param::mode);
    order = state.getRawParameterValue(param::order);
    rtaSlot = state.getRawParameterValue(param::rtaPosition);
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
        settings.bands[i] = { read(refs.low), read(refs.high), refs.dynamics.read(), read(refs.lift) > 0.5f };
    }
    settings.limiter = { read(limitOn) > 0.5f, read(ceiling), read(drive), read(limitRelease) };
    return settings;
}

void Processor::setStripOrder(const dsp::Order& requested)
{
    const auto position = std::find(dsp::orders.begin(), dsp::orders.end(), requested);
    if (position == dsp::orders.end())
        return;
    setDiscreteValue(state, param::order, static_cast<int>(position - dsp::orders.begin()));
}

int Processor::rtaPosition() const
{
    return std::clamp(roundToInt(read(rtaSlot)), 0, dsp::stageCount);
}

void Processor::setRtaPosition(int position)
{
    setDiscreteValue(state, param::rtaPosition, position);
}

std::optional<dsp::TonalReference> Processor::tonalReference() const
{
    const ScopedLock lock(referenceLock);
    return reference;
}

void Processor::setTonalReference(std::optional<dsp::TonalReference> next)
{
    const ScopedLock lock(referenceLock);
    reference = std::move(next);
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
    spectrumEpoch.fetch_add(1, std::memory_order_relaxed);
    inputPeak.store(0.0f, std::memory_order_relaxed);
    outputPeak.store(0.0f, std::memory_order_relaxed);
    hostBypassed.store(false, std::memory_order_relaxed);
    outputPeakPending.store(false, std::memory_order_relaxed);
    for (auto& meter : stageMeters)
    {
        meter.inputPeak.store(0.0f, std::memory_order_relaxed);
        meter.gainDb.store(0.0f, std::memory_order_relaxed);
        meter.outputPeak.store(0.0f, std::memory_order_relaxed);
    }
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
    hostBypassed.store(hostBypass, std::memory_order_relaxed);
    const auto frames = audio.getNumSamples();
    if (frames == 0 || audio.getNumChannels() == 0)
        return;

    auto settings = currentSettings();
    settings.bypass = settings.bypass || hostBypass;
    rack.configure(settings);
    const auto view = spectrumRequested.load(std::memory_order_relaxed);
    const auto position = view == SpectrumView::tonal ? rtaPosition() : dsp::stageCount;
    const auto key = spectrumKey(view, settings.order, position, settings.bypass);
    const auto tap = settings.bypass && position < dsp::stageCount ? 0 : position;
    const auto capture = view != SpectrumView::none && (view != SpectrumView::tonal || !rack.isReordering());
    const auto region = spectrumFifo.write(capture ? frames : 0);
    const auto captured = region.blockSize1 + region.blockSize2;
    const auto sequence = spectrumSequence;
    spectrumSequence += static_cast<std::uint32_t>(frames);
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getNumChannels() == 2 ? audio.getWritePointer(1) : nullptr;
    float in = 0.0f, out = 0.0f;
    const auto finite = [](float value) { return std::isfinite(value) ? value : 0.0f; };
    for (int i = 0; i < frames; ++i)
    {
        const auto l = finite(left[i]);
        const dsp::Frame input { l, right != nullptr ? finite(right[i]) : l };
        dsp::Frame tapped {};
        const auto interstage = i < captured && tap > 0 && tap < dsp::stageCount;
        const auto output = rack.process(input, tap, interstage ? &tapped : nullptr);
        in = std::max(in, dsp::peak(input));
        out = std::max(out, dsp::peak(output));
        left[i] = output[0];
        if (right != nullptr)
            right[i] = output[1];
        if (i < captured)
        {
            const auto slot = i < region.blockSize1 ? region.startIndex1 + i : region.startIndex2 + i - region.blockSize1;
            const auto signal = interstage ? tapped : (tap == 0 ? input : output);
            spectrum[static_cast<std::size_t>(slot)] = { signal, key, sequence + static_cast<std::uint32_t>(i) };
        }
    }

    const auto decay = std::exp(-static_cast<float>(frames) / (0.25f * displayRate()));
    const auto updatePeak = [decay](Atomic& meter, float level)
    {
        meter.store(std::max(level, read(&meter) * decay), std::memory_order_relaxed);
    };
    updatePeak(inputPeak, in);
    updatePeak(outputPeak, out);
    if (settings.bypass || out >= brakeThreshold)
        outputPeakPending.store(!settings.bypass, std::memory_order_relaxed);
    const auto metering = rack.meters();
    for (std::size_t i = 0; i < stageMeters.size(); ++i)
    {
        auto& meter = stageMeters[i];
        const auto& levels = metering.stages[i];
        updatePeak(meter.inputPeak, levels.inputPeak);
        meter.gainDb.store(levels.gainDb, std::memory_order_relaxed);
        updatePeak(meter.outputPeak, levels.outputPeak);
    }
    for (std::size_t i = 0; i < bandMeter.size(); ++i)
        bandMeter[i].store(metering.bands[i], std::memory_order_relaxed);
}

dsp::Metering Processor::readMeters() const
{
    dsp::Metering result;
    for (std::size_t i = 0; i < stageMeters.size(); ++i)
    {
        const auto& meter = stageMeters[i];
        result.stages[i] = { read(&meter.inputPeak), read(&meter.gainDb), read(&meter.outputPeak) };
    }
    for (std::size_t i = 0; i < bandMeter.size(); ++i)
        result.bands[i] = read(&bandMeter[i]);
    return result;
}

std::array<float, 2> Processor::readPeaks() const
{
    return { read(&inputPeak), read(&outputPeak) };
}

bool Processor::isBypassed() const
{
    return read(bypass) > 0.5f || hostBypassed.load(std::memory_order_relaxed);
}

std::uint32_t Processor::spectrumKey(SpectrumView view, int orderIndex, int position, bool bypassed) const
{
    const auto source = view == SpectrumView::tonal
        ? 2 + 2 * (orderIndex * (dsp::stageCount + 1) + position) + static_cast<int>(bypassed) : 1;
    return spectrumEpoch.load(std::memory_order_relaxed) * 256u + static_cast<std::uint32_t>(source);
}

bool Processor::readSpectrum(float* left, float* right, int count, std::uint32_t key)
{
    const auto available = spectrumFifo.getNumReady();
    if (available < count)
        return false;
    int first, firstSize, second, secondSize;
    spectrumFifo.prepareToRead(available, first, firstSize, second, secondSize);
    const auto sampleAt = [&](int offset) -> const SpectrumSample&
    {
        return spectrum[static_cast<std::size_t>(offset < firstSize ? first + offset : second + offset - firstSize)];
    };
    const auto skip = available - count;
    const auto end = sampleAt(available - 1).sequence;
    for (int i = count - 1; i >= 0; --i)
    {
        const auto& sample = sampleAt(skip + i);
        if (sample.key != key || end - sample.sequence != static_cast<std::uint32_t>(count - 1 - i))
        {
            // Keep the fresh tail for the next window and discard the old or discontinuous source.
            spectrumFifo.finishedRead(skip + i + 1);
            return false;
        }
        left[i] = sample.signal[0];
        right[i] = sample.signal[1];
    }
    spectrumFifo.finishedRead(skip + count / 4);
    return true;
}

void Processor::getStateInformation(MemoryBlock& destination)
{
    auto snapshot = state.copyState();
    if (const auto captured = tonalReference())
    {
        StringArray values;
        for (const auto value : captured->power)
            values.add(String(value, 10));
        snapshot.setProperty(referenceProperty, values.joinIntoString(" "), nullptr);
        snapshot.setProperty(referenceBandsProperty, captured->bands, nullptr);
    }
    else
    {
        snapshot.removeProperty(referenceProperty, nullptr);
        snapshot.removeProperty(referenceBandsProperty, nullptr);
    }
    if (auto xml = snapshot.createXml())
        copyXmlToBinary(*xml, destination);
}

void Processor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (xml->hasTagName(state.state.getType()))
        {
            auto restored = ValueTree::fromXml(*xml);
            const auto restoreDefault = [&](const String& id)
            {
                if (!restored.getChildWithProperty("id", id).isValid())
                {
                    auto* parameter = state.getParameter(id);
                    ValueTree defaultState { "PARAM" };
                    defaultState.setProperty("id", id, nullptr);
                    defaultState.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
                    restored.appendChild(defaultState, nullptr);
                }
            };
            for (int i = 0; i < dsp::bandCount; ++i)
                restoreDefault(param::id(param::band(i), param::bandLift));
            for (const auto* id : { param::rtaPosition, param::rtaTarget, param::rtaView })
                restoreDefault(id);
            setTonalReference(readReference(restored));
            state.replaceState(restored);
        }
    }
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
