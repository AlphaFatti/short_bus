// SPDX-License-Identifier: AGPL-3.0-only
#include "Processor.h"
#include <cstdlib>
#include <iostream>

using namespace juce;
using namespace shortbus;

namespace
{
void require(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << "Failed: " << message << '\n';
        std::exit(1);
    }
}

void set(Processor& processor, const String& id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

bool near(float a, float b) { return std::abs(a - b) < 0.001f; }

void assistAndStageResets(Processor& processor)
{
    const auto parameterValue = [&processor](const char* id)
    {
        return processor.state.getRawParameterValue(id)->load();
    };
    set(processor, param::multiAssist, 1.0f);
    set(processor, param::assistProfile, 1.0f);
    set(processor, param::assistFocus, 72.0f);
    set(processor, "band2_threshold", -36.0f);
    const auto assistPreset = processor.presetState(PresetScope::multiband);
    auto oldAssistPreset = assistPreset.createCopy();
    constexpr std::array assistIds { param::multiAssist, param::assistProfile, param::assistFocus };
    for (const auto* id : assistIds)
        oldAssistPreset.removeChild(oldAssistPreset.getChildWithProperty("id", id), nullptr);
    require(processor.applyPreset(oldAssistPreset, PresetScope::multiband).wasOk()
            && !processor.currentSettings().assist.enabled
            && near(processor.currentSettings().assist.focus, dsp::defaultAssistFocus),
            "older 5-Band presets load manual mode and default Focus");
    require(processor.applyPreset(assistPreset, PresetScope::multiband).wasOk()
            && processor.currentSettings().assist.enabled
            && processor.currentSettings().assist.profile == dsp::AssistProfile::music
            && near(processor.currentSettings().assist.focus, 72.0f)
            && near(processor.currentSettings().bands[1].dynamics.threshold, -36.0f), "Assist preset saves manual and Assist settings");

    set(processor, param::rtaPink, 1.0f);
    set(processor, param::rtaView, 1.0f);
    const auto rtaPreset = processor.presetState(PresetScope::rta);
    auto oldRtaPreset = rtaPreset.createCopy();
    oldRtaPreset.removeChild(oldRtaPreset.getChildWithProperty("id", param::rtaPink), nullptr);
    require(processor.applyPreset(oldRtaPreset, PresetScope::rta).wasOk()
            && parameterValue(param::rtaPink) < 0.5f && parameterValue(param::rtaView) > 0.5f,
            "older RTA presets keep Broad and receive the original target display");
    require(processor.applyPreset(rtaPreset, PresetScope::rta).wasOk() && parameterValue(param::rtaPink) > 0.5f,
            "RTA presets recall Pink Slope");

    const auto assistBus = processor.presetState(PresetScope::bus);
    set(processor, param::multiAssist, 0.0f);
    set(processor, param::rtaPink, 0.0f);
    processor.setPresetRecallSafes((1u << dsp::index(dsp::Stage::multiband)) | rtaRecallSafe);
    require(processor.applyPreset(assistBus, PresetScope::bus).wasOk() && !processor.currentSettings().assist.enabled
            && parameterValue(param::rtaPink) < 0.5f, "5-Band and RTA recall safes protect their new settings");
    processor.setPresetRecallSafes(0);
    require(processor.applyPreset(assistBus, PresetScope::bus).wasOk() && processor.currentSettings().assist.enabled
            && parameterValue(param::rtaPink) > 0.5f, "Bus preset recalls Assist and Pink Slope");

    MemoryBlock hostState;
    processor.getStateInformation(hostState);
    auto older = ValueTree::fromXml(*AudioProcessor::getXmlFromBinary(hostState.getData(), static_cast<int>(hostState.getSize())));
    for (const auto* id : assistIds)
        older.removeChild(older.getChildWithProperty("id", id), nullptr);
    older.removeChild(older.getChildWithProperty("id", param::rtaPink), nullptr);
    AudioProcessor::copyXmlToBinary(*older.createXml(), hostState);
    processor.setStateInformation(hostState.getData(), static_cast<int>(hostState.getSize()));
    require(!processor.currentSettings().assist.enabled && parameterValue(param::rtaPink) < 0.5f
            && parameterValue(param::rtaView) > 0.5f, "older sessions receive compatible defaults");
    require(processor.applyPreset(assistBus, PresetScope::bus).wasOk(), "restore current Bus settings");

    const Processor::TileOrder route { 1, 4, 2, 5, 0, 3 };
    processor.setTileOrder(route);
    processor.setPresetRecallSafes(defaultPresetSafes | geqRecallSafe | rtaRecallSafe);
    const auto safes = processor.presetRecallSafes();
    const auto tap = processor.rtaPosition();
    set(processor, param::bypass, 1.0f);
    set(processor, "low_enabled", 1.0f);
    set(processor, "low_threshold", -55.0f);
    set(processor, "multi_enabled", 1.0f);
    set(processor, param::compMode, 2.0f);
    set(processor, param::fetAttack, 0.03f);
    set(processor, param::limitAdaptive, 0.0f);
    set(processor, param::geqGain(17), 7.0f);
    set(processor, param::saturation, 65.0f);
    set(processor, param::rtaFloor, -50.0f);
    processor.setTonalReference(dsp::TonalReference { dsp::tonalTargets[2], dsp::tonalBandCount });
    constexpr std::array scopes { PresetScope::lowLevel, PresetScope::multiband, PresetScope::compressor,
                                 PresetScope::limiter, PresetScope::geq, PresetScope::rta };
    for (const auto scope : scopes)
    {
        const auto before = processor.presetState(PresetScope::bus);
        require(processor.resetStage(scope).wasOk(), "reset each stage");
        const auto after = processor.presetState(PresetScope::bus);
        for (const auto& child : after)
        {
            const auto id = child.getProperty("id").toString();
            auto* parameter = processor.state.getParameter(id);
            const auto expected = presetIncludesParameter(scope, id)
                ? parameter->convertFrom0to1(parameter->getDefaultValue())
                : static_cast<float>(before.getChildWithProperty("id", id).getProperty("value"));
            require(near(static_cast<float>(child.getProperty("value")), expected), "reset restores only the stage's factory values");
        }
        require(processor.tileOrder() == route && processor.rtaPosition() == tap
                && processor.currentSettings().bypass && processor.presetRecallSafes() == safes,
                "reset preserves routing, bypass and recall safes");
        require(processor.tonalReference().has_value() == (scope != PresetScope::rta), "only RTA reset clears its capture");
    }
}
}

int main()
{
    const ScopedJuceInitialiser_GUI init;
    auto processor = std::make_unique<Processor>();
    processor->prepareToPlay(48000.0, 256);
    const auto folder = File::getSpecialLocation(File::tempDirectory)
        .getNonexistentChildFile("ShortBusPresetTests", "", true);
    const ScopeGuard cleanup { [&folder] { folder.deleteRecursively(); } };
    PresetStore store(folder), otherHost(folder);

    set(*processor, "comp_threshold", -28.0f);
    set(*processor, "comp_makeup", 3.0f);
    const auto unit = processor->presetState(PresetScope::compressor);
    File first, second;
    require(store.save(PresetScope::compressor, "My compressor", unit, first).wasOk(), "save unit preset");
    require(store.save(PresetScope::compressor, "My compressor", unit, second).wasOk() && first != second,
            "saving a duplicate name preserves the existing file");
    const auto entries = otherHost.list(PresetScope::compressor);
    require(entries.size() == 2, "another host can enumerate the shared presets");
    ValueTree loaded;
    require(otherHost.load(entries[0], PresetScope::compressor, loaded).wasOk(), "read unit preset");
    set(*processor, "comp_threshold", -10.0f);
    set(*processor, "low_threshold", -61.0f);
    processor->setRtaPosition(1);
    require(processor->applyPreset(loaded, PresetScope::compressor).wasOk(), "recall unit preset");
    require(near(processor->currentSettings().compressor.threshold, -28.0f)
            && near(processor->currentSettings().lowLevel.threshold, -61.0f) && processor->rtaPosition() == 1,
            "unit recall changes only its own settings");
    require(otherHost.load(entries[0], PresetScope::bus, loaded).failed(), "reject wrong preset scope");

    processor->setTonalReference(dsp::TonalReference { dsp::tonalTargets[0], dsp::tonalBandCount });
    set(*processor, "strip_order", 7.0f);
    processor->setRtaPosition(3);
    const auto bus = processor->presetState(PresetScope::bus);
    File busFile;
    require(store.save(PresetScope::bus, "Sunday stream", bus, busFile).wasOk(), "save bus show file");
    require(otherHost.load(otherHost.list(PresetScope::bus)[0], PresetScope::bus, loaded).wasOk(), "read bus show file");
    set(*processor, "low_threshold", -32.0f);
    set(*processor, "comp_makeup", 10.0f);
    set(*processor, "strip_order", 12.0f);
    processor->setRtaPosition(0);
    set(*processor, "bypass", 1.0f);
    processor->setTonalReference(dsp::TonalReference { dsp::tonalTargets[2], dsp::tonalBandCount });
    processor->setPresetRecallSafes(defaultPresetSafes | (1u << 2) | (1u << 4) | (1u << 5));
    require(processor->applyPreset(loaded, PresetScope::bus).wasOk(), "recall bus with safes");
    require(near(processor->currentSettings().lowLevel.threshold, -61.0f)
            && near(processor->currentSettings().compressor.makeup, 10.0f)
            && processor->currentSettings().order == 12 && processor->rtaPosition() == 0
            && processor->currentSettings().bypass, "safes protect stages, route, tap and bypass");
    require(near(processor->tonalReference()->power[12], dsp::tonalTargets[2][12]), "RTA safe preserves reference");

    MemoryBlock saved;
    processor->getStateInformation(saved);
    auto recalled = std::make_unique<Processor>();
    recalled->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(recalled->presetRecallSafes() == processor->presetRecallSafes(), "recall safes persist with the project");
    processor->setPresetRecallSafes(0);
    require(processor->applyPreset(loaded, PresetScope::bus).wasOk(), "recall full bus");
    require(near(processor->currentSettings().compressor.makeup, 3.0f)
            && processor->currentSettings().order == 7 && processor->rtaPosition() == 3
            && !processor->currentSettings().bypass, "full bus restores all parameters and positions");
    require(near(processor->tonalReference()->power[12], dsp::tonalTargets[0][12]), "bus restores captured reference");

    auto invalid = loaded.createCopy();
    invalid.getChildWithProperty("id", "comp_ratio").setProperty("value", "invalid", nullptr);
    set(*processor, "low_threshold", -35.0f);
    require(processor->applyPreset(invalid, PresetScope::bus).failed(), "invalid data is rejected");
    require(near(processor->currentSettings().lowLevel.threshold, -35.0f), "invalid recall has no partial changes");

    const Processor::TileOrder route { 5, 4, 3, 2, 1, 0 };
    processor->setTileOrder(route);
    require(processor->tileOrder() == route, "full tile order round trips");
    processor->setTileOrder({ 5, 4, 3, 2, 1, 1 });
    require(processor->tileOrder() == route, "invalid tile order is rejected");
    auto audible = unit.createCopy();
    audible.getChildWithProperty("id", "comp_enabled").setProperty("value", 1.0f, nullptr);
    audible.getChildWithProperty("id", "comp_ratio").setProperty("value", 1.0f, nullptr);
    audible.getChildWithProperty("id", "comp_makeup").setProperty("value", 6.0f, nullptr);
    require(processor->applyPreset(audible, PresetScope::compressor).wasOk(), "recall an enabled processor");
    AudioBuffer<float> audio(2, 256);
    MidiBuffer midi;
    for (int block = 0; block < 180; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            std::fill_n(audio.getWritePointer(channel), audio.getNumSamples(), channel == 0 ? 0.2f : -0.1f);
        processor->processBlock(audio, midi);
    }
    require(near(audio.getSample(0, 255), 0.2f * dsp::gainFromDb(6.0f)), "preset recall reaches the audio callback");
    set(*processor, param::compMode, 1.0f);
    set(*processor, param::optoAttack, 32.0f);
    const auto optoPreset = processor->presetState(PresetScope::compressor);
    set(*processor, param::compMode, 2.0f);
    set(*processor, param::optoAttack, 10.0f);
    require(processor->applyPreset(optoPreset, PresetScope::compressor).wasOk()
            && processor->currentSettings().compressorMode == dsp::CompressorMode::opto
            && near(processor->currentSettings().optoTiming.attack, 32.0f), "unit preset recalls mode and its timing");

    auto legacyUnit = unit.createCopy();
    constexpr std::array characterIds { param::compMode, param::optoAttack, param::optoRelease, param::fetAttack, param::fetRelease };
    for (const auto* id : characterIds)
        legacyUnit.removeChild(legacyUnit.getChildWithProperty("id", id), nullptr);
    set(*processor, param::compMode, 2.0f);
    require(processor->applyPreset(legacyUnit, PresetScope::compressor).wasOk()
            && processor->currentSettings().compressorMode == dsp::CompressorMode::clean, "older unit presets restore Clean");
    auto legacyBus = loaded.createCopy();
    for (const auto* id : characterIds)
        legacyBus.removeChild(legacyBus.getChildWithProperty("id", id), nullptr);
    set(*processor, param::compMode, 2.0f);
    processor->setPresetRecallSafes(1u << 2);
    require(processor->applyPreset(legacyBus, PresetScope::bus).wasOk()
            && processor->currentSettings().compressorMode == dsp::CompressorMode::fet, "Compressor safe protects the mode");
    processor->setPresetRecallSafes(0);
    require(processor->applyPreset(legacyBus, PresetScope::bus).wasOk()
            && processor->currentSettings().compressorMode == dsp::CompressorMode::clean, "older Bus presets restore Clean");

    MemoryBlock hostState;
    processor->getStateInformation(hostState);
    auto legacyHost = ValueTree::fromXml(*AudioProcessor::getXmlFromBinary(hostState.getData(), static_cast<int>(hostState.getSize())));
    for (const auto* id : characterIds)
        legacyHost.removeChild(legacyHost.getChildWithProperty("id", id), nullptr);
    AudioProcessor::copyXmlToBinary(*legacyHost.createXml(), hostState);
    set(*processor, param::compMode, 2.0f);
    processor->setStateInformation(hostState.getData(), static_cast<int>(hostState.getSize()));
    require(processor->currentSettings().compressorMode == dsp::CompressorMode::clean, "older host sessions restore Clean");

    Processor::TileOrder permutation { 0, 1, 2, 3, 4, 5 };
    do
    {
        processor->setTileOrder(permutation);
        require(processor->tileOrder() == permutation, "GEQ and RTA route permutations round trip");
    } while (std::next_permutation(permutation.begin(), permutation.end()));

    set(*processor, param::geqEnabled, 1.0f);
    set(*processor, param::geqGain(17), 6.0f);
    set(*processor, param::saturation, 65.0f);
    set(*processor, param::rtaFloor, -50.0f);
    const auto eqPreset = processor->presetState(PresetScope::geq);
    set(*processor, param::geqGain(17), -6.0f);
    set(*processor, param::saturation, 0.0f);
    require(processor->applyPreset(eqPreset, PresetScope::geq).wasOk()
            && near(processor->currentSettings().geq.gains[17], 6.0f)
            && near(processor->currentSettings().geq.saturation, 65.0f), "GEQ presets recall faders and saturation");

    const auto rtaPreset = processor->presetState(PresetScope::rta);
    set(*processor, param::rtaFloor, -80.0f);
    require(processor->applyPreset(rtaPreset, PresetScope::rta).wasOk()
            && near(processor->state.getRawParameterValue(param::rtaFloor)->load(), -50.0f), "RTA preset recalls the floor");

    for (int i = legacyBus.getNumChildren(); --i >= 0;)
    {
        const auto child = legacyBus.getChild(i);
        const auto id = child.getProperty("id").toString();
        if (presetIncludesParameter(PresetScope::geq, id) || id == param::rtaFloor
            || id == param::geqPosition || id == param::rtaAfterGeq)
            legacyBus.removeChild(i, nullptr);
    }
    processor->setPresetRecallSafes(geqRecallSafe | rtaRecallSafe | orderRecallSafe);
    const auto savedRoute = processor->tileOrder();
    require(processor->applyPreset(legacyBus, PresetScope::bus).wasOk()
            && processor->currentSettings().geq.enabled && processor->tileOrder() == savedRoute
            && near(processor->state.getRawParameterValue(param::rtaFloor)->load(), -50.0f),
            "recall safes protect the GEQ, sensitivity and both routing positions");
    processor->setPresetRecallSafes(0);
    require(processor->applyPreset(legacyBus, PresetScope::bus).wasOk()
            && !processor->currentSettings().geq.enabled && near(processor->currentSettings().geq.saturation, 0.0f)
            && near(processor->state.getRawParameterValue(param::rtaFloor)->load(), dsp::defaultRtaFloorDb)
            && processor->currentSettings().order == 7,
            "older Bus presets load with a neutral GEQ and their original dynamics order");
    set(*processor, param::limitAdaptive, 0.0f);
    const auto fixedLimiter = processor->presetState(PresetScope::limiter);
    set(*processor, param::limitAdaptive, 1.0f);
    require(processor->applyPreset(fixedLimiter, PresetScope::limiter).wasOk()
            && !processor->currentSettings().limiter.adaptive, "limiter presets recall fixed recovery");
    auto oldLimiter = fixedLimiter.createCopy();
    oldLimiter.removeChild(oldLimiter.getChildWithProperty("id", param::limitAdaptive), nullptr);
    require(processor->applyPreset(oldLimiter, PresetScope::limiter).wasOk()
            && processor->currentSettings().limiter.adaptive, "older limiter presets receive adaptive recovery");
    processor->getStateInformation(hostState);
    auto oldSession = ValueTree::fromXml(*AudioProcessor::getXmlFromBinary(hostState.getData(), static_cast<int>(hostState.getSize())));
    oldSession.removeChild(oldSession.getChildWithProperty("id", param::limitAdaptive), nullptr);
    AudioProcessor::copyXmlToBinary(*oldSession.createXml(), hostState);
    set(*processor, param::limitAdaptive, 0.0f);
    processor->setStateInformation(hostState.getData(), static_cast<int>(hostState.getSize()));
    require(processor->currentSettings().limiter.adaptive, "older host sessions receive adaptive recovery");
    assistAndStageResets(*processor);
    std::cout << "Preset files, scopes, safes, validation and tile orders passed\n";
}
