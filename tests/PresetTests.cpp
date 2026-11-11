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

void set(Processor& processor, const char* id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

bool near(float a, float b) { return std::abs(a - b) < 0.001f; }
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

    const Processor::TileOrder route { 4, 3, 2, 1, 0 };
    processor->setTileOrder(route);
    require(processor->tileOrder() == route, "full tile order round trips");
    processor->setTileOrder({ 4, 3, 2, 1, 1 });
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
    std::cout << "Preset files, scopes, safes, validation and tile orders passed\n";
}
