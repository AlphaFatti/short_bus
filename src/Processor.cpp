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
constexpr auto recallSafesProperty = "presetRecallSafes";
constexpr std::array compressorCharacterParameters { param::compMode, param::optoAttack, param::optoRelease,
                                                    param::fetAttack, param::fetRelease };

void restoreMissingParameter(AudioProcessorValueTreeState& state, ValueTree& restored, const String& id)
{
    if (restored.getChildWithProperty("id", id).isValid())
        return;
    auto* parameter = state.getParameter(id);
    ValueTree defaultState { "PARAM" };
    defaultState.setProperty("id", id, nullptr);
    defaultState.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
    restored.appendChild(defaultState, nullptr);
}

void restoreMissingPresetParameters(AudioProcessorValueTreeState& state, ValueTree& restored, PresetScope scope)
{
    if (scope == PresetScope::bus || scope == PresetScope::compressor)
        for (const auto* id : compressorCharacterParameters)
            restoreMissingParameter(state, restored, id);
    if (scope == PresetScope::bus || scope == PresetScope::rta)
        restoreMissingParameter(state, restored, param::rtaFloor);
    if (scope == PresetScope::bus || scope == PresetScope::geq)
    {
        restoreMissingParameter(state, restored, param::geqEnabled);
        restoreMissingParameter(state, restored, param::saturation);
        for (std::size_t i = 0; i < dsp::geqBandCount; ++i)
            restoreMissingParameter(state, restored, param::geqGain(i));
    }
    if (scope == PresetScope::bus)
    {
        restoreMissingParameter(state, restored, param::geqPosition);
        restoreMissingParameter(state, restored, param::rtaAfterGeq);
    }
}

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

void mergeMeterPeaks(dsp::Metering& destination, const dsp::Metering& source)
{
    for (std::size_t i = 0; i < destination.stages.size(); ++i)
    {
        auto& level = destination.stages[i];
        level.inputPeak = std::max(level.inputPeak, source.stages[i].inputPeak);
        level.outputPeak = std::max(level.outputPeak, source.stages[i].outputPeak);
    }
}

void setDiscreteValue(AudioProcessorValueTreeState& state, const char* id, int value)
{
    auto* parameter = state.getParameter(id);
    const auto normalised = parameter->convertTo0to1(static_cast<float>(value));
    if (std::abs(parameter->getValue() - normalised) < 1.0e-7f)
        return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalised);
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
    constexpr std::array<const char*, dsp::dynamicsStageCount> names { "Low Level", "5-Band", "Compressor", "Limiter" };
    StringArray orders;
    for (const auto& order : dsp::dynamicsOrders)
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
                                                   0, dsp::dynamicsStageCount, dsp::dynamicsStageCount));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { param::rtaTarget, 1 }, "RTA Target",
        StringArray { "Balanced", "Bass-forward", "Speech", "Captured" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { param::rtaView, 1 }, "RTA View",
        StringArray { "Fine", "Broad" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { param::compMode, 1 }, "Compressor Mode",
        StringArray { "Clean", "Opto", "FET" }, 0));
    addFloat(layout, param::optoAttack, "Compressor Opto Attack", { 1.0f, 100.0f, 0.1f, 0.5f }, dsp::optoTimingDefaults.attack);
    addFloat(layout, param::optoRelease, "Compressor Opto Release", { 20.0f, 500.0f, 1.0f, 0.5f }, dsp::optoTimingDefaults.release);
    addFloat(layout, param::fetAttack, "Compressor FET Attack", { 0.02f, 0.8f, 0.001f, 0.5f }, dsp::fetTimingDefaults.attack);
    addFloat(layout, param::fetRelease, "Compressor FET Release", { 50.0f, 1100.0f, 1.0f, 0.5f }, dsp::fetTimingDefaults.release);
    addFloat(layout, param::rtaFloor, "RTA Sensitivity Floor", { -90.0f, -24.0f, 1.0f }, dsp::defaultRtaFloorDb);
    addToggle(layout, param::geqEnabled, "GEQ Enabled", false);
    for (std::size_t i = 0; i < dsp::geqBandCount; ++i)
    {
        const auto hz = dsp::geqFrequencies[i];
        addFloat(layout, param::geqGain(i), "GEQ " + String(hz, std::floor(hz) < hz ? 1 : 0) + " Hz",
                 { -dsp::geqRangeDb, dsp::geqRangeDb, 0.1f }, 0.0f);
    }
    addFloat(layout, param::saturation, "GEQ Saturation", { 0.0f, 100.0f, 1.0f }, 0.0f);
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { param::geqPosition, 1 }, "GEQ Position",
        StringArray { "Before limiter", "First", "Second", "Third", "Fourth", "Fifth" }, 0));
    addToggle(layout, param::rtaAfterGeq, "RTA After GEQ At Shared Position", true);
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
    geqSlot = state.getRawParameterValue(param::geqPosition);
    rtaAfterEq = state.getRawParameterValue(param::rtaAfterGeq);
    compressorMode = state.getRawParameterValue(param::compMode);
    optoAttack = state.getRawParameterValue(param::optoAttack);
    optoRelease = state.getRawParameterValue(param::optoRelease);
    fetAttack = state.getRawParameterValue(param::fetAttack);
    fetRelease = state.getRawParameterValue(param::fetRelease);
    bypass = state.getRawParameterValue(param::bypass);
    multiOn = state.getRawParameterValue(param::id(param::multi, "enabled"));
    limitOn = state.getRawParameterValue(param::id(param::limit, "enabled"));
    ceiling = state.getRawParameterValue(param::id(param::limit, "ceiling"));
    drive = state.getRawParameterValue(param::id(param::limit, "drive"));
    limitRelease = state.getRawParameterValue(param::id(param::limit, "release"));
    geqOn = state.getRawParameterValue(param::geqEnabled);
    saturation = state.getRawParameterValue(param::saturation);
    for (std::size_t i = 0; i < geqGains.size(); ++i)
        geqGains[i] = state.getRawParameterValue(param::geqGain(i));
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
    settings.order = stripOrderIndex();
    settings.lowMode = read(mode) < 0.5f ? dsp::LowMode::expand : dsp::LowMode::lift;
    settings.lowLevel = lowRefs.read();
    settings.compressor = compRefs.read();
    settings.compressorMode = static_cast<dsp::CompressorMode>(std::clamp(roundToInt(read(compressorMode)), 0, 2));
    settings.optoTiming = { read(optoAttack), read(optoRelease) };
    settings.fetTiming = { read(fetAttack), read(fetRelease) };
    settings.multiEnabled = read(multiOn) > 0.5f;
    for (std::size_t i = 0; i < bandRefs.size(); ++i)
    {
        const auto& refs = bandRefs[i];
        settings.bands[i] = { read(refs.low), read(refs.high), refs.dynamics.read(), read(refs.lift) > 0.5f };
    }
    settings.limiter = { read(limitOn) > 0.5f, read(ceiling), read(drive), read(limitRelease) };
    settings.geq.enabled = read(geqOn) > 0.5f;
    settings.geq.saturation = read(saturation);
    for (std::size_t i = 0; i < geqGains.size(); ++i)
        settings.geq.gains[i] = read(geqGains[i]);
    return settings;
}

int Processor::stripOrderIndex() const
{
    const auto dynamics = std::clamp(roundToInt(read(order)), 0, dsp::dynamicsOrderCount - 1);
    const auto geq = std::clamp(roundToInt(read(geqSlot)), 0, dsp::stageCount) - 1;
    return dsp::rackOrderIndex(dynamics, geq);
}

Processor::TileOrder Processor::tileOrder() const
{
    const auto orderIndex = stripOrderIndex();
    const auto& route = dsp::orders[static_cast<std::size_t>(orderIndex)];
    const auto tap = static_cast<std::size_t>(rtaPositionForOrder(orderIndex));
    TileOrder tiles {};
    auto stage = route.begin();
    for (std::size_t i = 0; i < tiles.size(); ++i)
        tiles[i] = i == tap ? static_cast<std::size_t>(dsp::stageCount) : dsp::index(*stage++);
    return tiles;
}

void Processor::setTileOrder(const TileOrder& tiles)
{
    auto sorted = tiles;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i)
        if (sorted[i] != i)
            return;
    dsp::Order route {};
    auto next = route.begin();
    int tap = 0;
    for (std::size_t i = 0; i < tiles.size(); ++i)
    {
        if (tiles[i] == dsp::stageCount)
            tap = static_cast<int>(i);
        else
            *next++ = static_cast<dsp::Stage>(tiles[i]);
    }
    setRoute(route, tap);
}

ValueTree Processor::presetState(PresetScope scope)
{
    MemoryBlock data;
    getStateInformation(data);
    auto snapshot = ValueTree::fromXml(*getXmlFromBinary(data.getData(), static_cast<int>(data.getSize())));
    snapshot.removeProperty(recallSafesProperty, nullptr);
    for (int i = snapshot.getNumChildren(); --i >= 0;)
        if (!presetIncludesParameter(scope, snapshot.getChild(i).getProperty("id").toString()))
            snapshot.removeChild(i, nullptr);
    if (scope != PresetScope::bus && scope != PresetScope::rta)
    {
        snapshot.removeProperty(referenceProperty, nullptr);
        snapshot.removeProperty(referenceBandsProperty, nullptr);
    }
    return snapshot;
}

Result Processor::applyPreset(const ValueTree& preset, PresetScope scope)
{
    if (!preset.hasType(state.state.getType()))
        return Result::fail("The preset settings are incompatible.");
    const auto hasSettings = std::any_of(preset.begin(), preset.end(), [this, scope](const ValueTree& child)
    {
        const auto id = child.getProperty("id").toString();
        return state.getParameter(id) != nullptr && presetIncludesParameter(scope, id);
    });
    if (!hasSettings)
        return Result::fail("The preset is empty.");
    auto complete = preset.createCopy();
    restoreMissingPresetParameters(state, complete, scope);
    struct Change { RangedAudioParameter* parameter; float value; };
    std::vector<Change> changes;
    int recognized = 0;
    const auto safeMask = scope == PresetScope::bus ? presetRecallSafes() : 0u;
    const auto isSafe = [safeMask](const String& id)
    {
        if (id == param::order || id == param::rtaPosition || id == param::geqPosition || id == param::rtaAfterGeq)
            return (safeMask & orderRecallSafe) != 0;
        if (id.startsWith("rta_"))
            return (safeMask & rtaRecallSafe) != 0;
        if (id == param::bypass)
            return (safeMask & bypassRecallSafe) != 0;
        if (presetIncludesParameter(PresetScope::geq, id))
            return (safeMask & geqRecallSafe) != 0;
        for (int i = 0; i < dsp::dynamicsStageCount; ++i)
            if (presetIncludesParameter(static_cast<PresetScope>(i), id))
                return (safeMask & (1u << i)) != 0;
        return false;
    };
    for (const auto& child : complete)
    {
        const auto id = child.getProperty("id").toString();
        auto* parameter = state.getParameter(id);
        if (parameter == nullptr || !presetIncludesParameter(scope, id))
            continue;
        ++recognized;
        const auto text = child.getProperty("value").toString().trim();
        auto end = text.getCharPointer();
        const auto start = end;
        const auto value = static_cast<float>(CharacterFunctions::readDoubleValue(end));
        const auto& range = parameter->getNormalisableRange();
        if (end == start || !end.isEmpty() || !std::isfinite(value) || value < range.start - 0.001f || value > range.end + 0.001f)
            return Result::fail("The preset contains an invalid value for " + parameter->name + ".");
        if (!isSafe(id))
            changes.push_back({ parameter, parameter->convertTo0to1(value) });
    }
    if (recognized == 0)
        return Result::fail("The preset is empty.");
    const auto captured = readReference(preset);
    if (preset.hasProperty(referenceProperty) && !captured)
        return Result::fail("The captured reference in the preset is invalid.");
    const ScopedLock lock(presetLock);
    settingsRevision.fetch_add(1, std::memory_order_acq_rel);
    const ScopeGuard finished { [this] { settingsRevision.fetch_add(1, std::memory_order_release); } };
    for (const auto& change : changes)
    {
        change.parameter->beginChangeGesture();
        change.parameter->setValueNotifyingHost(change.value);
        change.parameter->endChangeGesture();
    }
    if ((scope == PresetScope::bus || scope == PresetScope::rta) && (safeMask & rtaRecallSafe) == 0)
        setTonalReference(captured);
    return Result::ok();
}

void Processor::setRoute(const dsp::Order& requested, int tap)
{
    const auto position = std::find(dsp::orders.begin(), dsp::orders.end(), requested);
    if (position == dsp::orders.end())
        return;
    const auto orderIndex = static_cast<int>(position - dsp::orders.begin());
    const auto eq = dsp::stagePosition(requested, dsp::Stage::geq);
    const auto dynamicsTap = tap > eq ? tap - 1 : tap;
    const ScopedLock lock(presetLock);
    settingsRevision.fetch_add(1, std::memory_order_acq_rel);
    const ScopeGuard finished { [this] { settingsRevision.fetch_add(1, std::memory_order_release); } };
    if (stripOrderIndex() != orderIndex)
    {
        setDiscreteValue(state, param::order, orderIndex % dsp::dynamicsOrderCount);
        setDiscreteValue(state, param::geqPosition, orderIndex < dsp::dynamicsOrderCount ? 0 : eq + 1);
    }
    setDiscreteValue(state, param::rtaPosition, dynamicsTap);
    if (dynamicsTap == eq)
        setDiscreteValue(state, param::rtaAfterGeq, tap > eq ? 1 : 0);
}

int Processor::rtaPosition() const
{
    return rtaPositionForOrder(stripOrderIndex());
}

int Processor::rtaPositionForOrder(int orderIndex) const
{
    // The original RTA parameter counts dynamics stages. The tie flag locates the tap on either side of the GEQ.
    const auto dynamicsTap = std::clamp(roundToInt(read(rtaSlot)), 0, dsp::dynamicsStageCount);
    const auto eq = dsp::stagePosition(dsp::orders[static_cast<std::size_t>(orderIndex)], dsp::Stage::geq);
    const auto afterEq = dynamicsTap > eq || (dynamicsTap == eq && read(rtaAfterEq) > 0.5f);
    return afterEq ? dynamicsTap + 1 : dynamicsTap;
}

void Processor::setRtaPosition(int position)
{
    setRoute(dsp::orders[static_cast<std::size_t>(stripOrderIndex())], std::clamp(position, 0, dsp::stageCount));
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
    renderSettings = currentSettings();
    renderRtaPosition = rtaPositionForOrder(renderSettings.order);
    setLatencySamples(rack.latency());
    meterRate.store(static_cast<float>(rate), std::memory_order_relaxed);
    analysisEpoch.fetch_add(1, std::memory_order_relaxed);
    historyWindowSamples = std::max(1, static_cast<int>(std::lround(rate / historyHz)));
    historySamples = 0;
    historySequence = 0;
    historyWindow = {};
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

    const auto revision = settingsRevision.load(std::memory_order_acquire);
    if ((revision & 1u) == 0)
    {
        const auto candidate = currentSettings();
        const auto position = rtaPositionForOrder(candidate.order);
        if (settingsRevision.load(std::memory_order_acquire) == revision)
        {
            renderSettings = candidate;
            renderRtaPosition = position;
        }
    }
    auto settings = renderSettings;
    settings.bypass = settings.bypass || hostBypass;
    rack.configure(settings);
    const auto& route = dsp::orders[static_cast<std::size_t>(settings.order)];
    const auto view = spectrumRequested.load(std::memory_order_relaxed);
    const auto position = view == SpectrumView::tonal ? renderRtaPosition
        : view == SpectrumView::geq ? dsp::stagePosition(route, dsp::Stage::geq) + 1 : dsp::stageCount;
    const auto key = spectrumKey(view, settings.order, position, settings.bypass);
    const auto tap = settings.bypass && position < dsp::stageCount ? 0 : position;
    const auto capture = view != SpectrumView::none && (view == SpectrumView::output || !rack.isReordering());
    const auto region = spectrumFifo.write(capture ? frames : 0);
    const auto captured = region.blockSize1 + region.blockSize2;
    const auto sequence = spectrumSequence;
    spectrumSequence += static_cast<std::uint32_t>(frames);
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getNumChannels() == 2 ? audio.getWritePointer(1) : nullptr;
    float in = 0.0f, out = 0.0f;
    dsp::Metering earlierPeaks;
    const auto collectHistory = historyRequested.load(std::memory_order_relaxed);
    const auto tapKey = collectHistory && !rack.isReordering()
        ? spectrumKey(SpectrumView::tonal, settings.order, renderRtaPosition, settings.bypass) : 0u;
    if (!collectHistory && historySamples > 0)
    {
        historySamples = 0;
        historyWindow = {};
    }
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
        if (collectHistory && ++historySamples >= historyWindowSamples)
        {
            const auto window = rack.takeMeters();
            mergeMeterPeaks(earlierPeaks, window);
            accumulateHistory(window, route, renderRtaPosition, settings.bypass, tapKey);
            publishHistory();
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
    auto metering = rack.meters();
    if (collectHistory && historySamples > 0)
        accumulateHistory(metering, route, renderRtaPosition, settings.bypass, tapKey);
    mergeMeterPeaks(metering, earlierPeaks);
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

void Processor::accumulateHistory(dsp::Metering slice, const dsp::Order& route, int tap, bool bypassed, std::uint32_t key)
{
    const auto input = slice.stages[dsp::index(route.front())].inputPeak;
    if (bypassed)
    {
        slice.stages.fill({ input, 0.0f, input });
        slice.bands.fill(0.0f);
    }
    if (historyWindow.tapKey != key)
    {
        historyWindow.tapKey = key;
        historyWindow.tapPeak = 0.0f;
    }
    const auto tapPeak = tap == 0 ? input : slice.stages[dsp::index(route[static_cast<std::size_t>(tap - 1)])].outputPeak;
    historyWindow.tapPeak = std::max(historyWindow.tapPeak, tapPeak);
    auto& levels = historyWindow.levels;
    mergeMeterPeaks(levels, slice);
    for (std::size_t i = 0; i < levels.stages.size(); ++i)
        if (std::abs(slice.stages[i].gainDb) > std::abs(levels.stages[i].gainDb))
            levels.stages[i].gainDb = slice.stages[i].gainDb;
    for (std::size_t i = 0; i < levels.bands.size(); ++i)
        if (std::abs(slice.bands[i]) > std::abs(levels.bands[i]))
            levels.bands[i] = slice.bands[i];
}

void Processor::publishHistory()
{
    historyWindow.epoch = analysisEpoch.load(std::memory_order_relaxed);
    historyWindow.sequence = historySequence++;
    const auto region = historyFifo.write(1);
    if (region.blockSize1 > 0)
        historyQueue[static_cast<std::size_t>(region.startIndex1)] = historyWindow;
    historyWindow = {};
    historySamples = 0;
}

void Processor::setMeterHistoryActive(bool active)
{
    if (active)
        historyFifo.finishedRead(historyFifo.getNumReady());
    historyRequested.store(active, std::memory_order_relaxed);
}

int Processor::readMeterHistory(HistoryFrame* destination, int capacity)
{
    const auto available = historyFifo.getNumReady();
    const auto region = historyFifo.read(available);
    const auto count = std::min(available, capacity);
    const auto epoch = analysisEpoch.load(std::memory_order_relaxed);
    int copied = 0;
    for (int i = available - count; i < available; ++i)
    {
        const auto slot = i < region.blockSize1 ? region.startIndex1 + i : region.startIndex2 + i - region.blockSize1;
        const auto& frame = historyQueue[static_cast<std::size_t>(slot)];
        if (frame.epoch == epoch)
            destination[copied++] = frame;
    }
    return copied;
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
    constexpr auto keysPerTapView = 2u * dsp::orderCount * (dsp::stageCount + 1u);
    constexpr auto keysPerEpoch = 2u + 2u * keysPerTapView;
    auto source = 1u;
    if (view == SpectrumView::tonal || view == SpectrumView::geq)
    {
        source = 2u + static_cast<std::uint32_t>(2 * (orderIndex * (dsp::stageCount + 1) + position))
            + static_cast<std::uint32_t>(bypassed);
        if (view == SpectrumView::geq)
            source += keysPerTapView;
    }
    return analysisEpoch.load(std::memory_order_relaxed) * keysPerEpoch + source;
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
    const ScopedLock lock(presetLock);
    auto snapshot = state.copyState();
    snapshot.setProperty(recallSafesProperty, static_cast<int>(presetRecallSafes()), nullptr);
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
            const ScopedLock lock(presetLock);
            settingsRevision.fetch_add(1, std::memory_order_acq_rel);
            const ScopeGuard finished { [this] { settingsRevision.fetch_add(1, std::memory_order_release); } };
            auto restored = ValueTree::fromXml(*xml);
            setPresetRecallSafes(static_cast<unsigned>(static_cast<int>(restored.getProperty(recallSafesProperty,
                                                                                           static_cast<int>(defaultPresetSafes)))));
            for (int i = 0; i < dsp::bandCount; ++i)
                restoreMissingParameter(state, restored, param::id(param::band(i), param::bandLift));
            for (const auto* id : { param::rtaPosition, param::rtaTarget, param::rtaView })
                restoreMissingParameter(state, restored, id);
            restoreMissingPresetParameters(state, restored, PresetScope::bus);
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
