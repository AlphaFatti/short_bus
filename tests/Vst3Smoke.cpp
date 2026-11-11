// SPDX-License-Identifier: AGPL-3.0-only
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
using namespace juce;
constexpr double rate = 48000.0;
constexpr int blockSize = 256;

void require(bool result, const String& message)
{
    if (!result)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

AudioProcessorParameter& parameter(AudioPluginInstance& plugin, const String& name)
{
    for (auto* candidate : plugin.getParameters())
        if (candidate->getName(100) == name)
            return *candidate;
    require(false, "Missing parameter " + name);
    std::abort();
}

void set(AudioPluginInstance& plugin, const String& name, const String& value)
{
    auto& target = parameter(plugin, name);
    target.setValueNotifyingHost(target.getValueForText(value));
}

float processConstant(AudioPluginInstance& plugin, int blocks, float level = 0.62f)
{
    AudioBuffer<float> block(2, blockSize);
    MidiBuffer midi;
    for (int i = 0; i < blocks; ++i)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            auto* samples = block.getWritePointer(channel);
            std::fill(samples, samples + blockSize, channel == 0 ? level : -0.5f * level);
        }
        plugin.processBlock(block, midi);
    }
    return block.getSample(0, blockSize - 1);
}
} // namespace

int main(int argc, char** argv)
{
    using namespace juce;
    require(argc == 2, "Pass the Short Bus VST3 bundle path");
    const ScopedJuceInitialiser_GUI init;
    AudioPluginFormatManager formats;
    auto format = std::make_unique<VST3PluginFormatHeadless>();
    OwnedArray<PluginDescription> descriptions;
    format->findAllTypesForFile(descriptions, File::getCurrentWorkingDirectory().getChildFile(argv[1]).getFullPathName());
    require(descriptions.size() == 1 && descriptions[0]->name == "Short Bus", "VST3 scan failed");
    formats.addFormat(std::move(format));

    const auto open = [&]
    {
        String error;
        auto instance = formats.createPluginInstance(*descriptions[0], rate, blockSize, error);
        require(instance != nullptr, "VST3 load failed: " + error);
        require(instance->getMainBusNumInputChannels() == 2 && instance->getMainBusNumOutputChannels() == 2,
                "Stereo bus unavailable");
        return instance;
    };

    auto plugin = open();
    plugin->prepareToPlay(rate, blockSize);
    require(plugin->getLatencySamples() == 144, "VST3 must report 3 ms at 48 kHz");
    require(std::abs(processConstant(*plugin, 8) - 0.62f) < 0.0001f, "Default rack must pass audio at unity");

    parameter(*plugin, "Compressor Enabled").setValueNotifyingHost(1.0f);
    set(*plugin, "Compressor Ratio", "1");
    set(*plugin, "Compressor Makeup", "6");
    parameter(*plugin, "Limiter Enabled").setValueNotifyingHost(1.0f);
    set(*plugin, "Limiter Ceiling", "-3");
    require(std::abs(processConstant(*plugin, 180) - 0.707946f) < 0.002f, "Limiter-last routing failed");

    parameter(*plugin, "Strip Order").setValueNotifyingHost(1.0f / 23.0f);
    require(processConstant(*plugin, 180) > 1.20f, "Changing strip order must change processing");
    parameter(*plugin, "5-Band Enabled").setValueNotifyingHost(1.0f);
    parameter(*plugin, "Band 1 Upward Compression").setValueNotifyingHost(1.0f);
    set(*plugin, "Band 1 Range", "6");
    require(std::abs(processConstant(*plugin, 180, 0.01f) - 0.03981f) < 0.001f, "VST3 band lift failed");
    set(*plugin, "RTA Position", "2");
    set(*plugin, "RTA Target", "Speech");
    set(*plugin, "RTA View", "Broad");
    set(*plugin, "Compressor Mode", "FET");
    set(*plugin, "Compressor FET Attack", "0.02");
    set(*plugin, "RTA Sensitivity Floor", "-54");
    set(*plugin, "GEQ 1000 Hz", "4.5");
    set(*plugin, "GEQ Saturation", "75");
    set(*plugin, "GEQ Position", "Fifth");
    require(std::abs(processConstant(*plugin, 20, 0.01f) - 0.03981f) < 0.001f, "moving RTA changed audio");
    MemoryBlock saved;
    plugin->getStateInformation(saved);
    require(saved.getSize() > 0, "VST3 state block is empty");
    plugin->releaseResources();

    auto recalled = open();
    recalled->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    recalled->prepareToPlay(rate, blockSize);
    require(std::abs(parameter(*recalled, "RTA Position").getValue() - 0.5f) < 0.0001f, "RTA position failed to recall");
    require(std::abs(parameter(*recalled, "RTA Target").getValue() - 2.0f / 3.0f) < 0.0001f, "RTA target failed to recall");
    require(parameter(*recalled, "RTA View").getValue() > 0.99f, "RTA view failed to recall");
    require(parameter(*recalled, "Compressor Mode").getValue() > 0.99f, "Compressor mode failed to recall");
    require(parameter(*recalled, "Compressor FET Attack").getValue() < 0.001f, "FET attack failed to recall");
    require(std::abs(parameter(*recalled, "RTA Sensitivity Floor").getValue() - 36.0f / 66.0f) < 0.001f,
            "RTA sensitivity failed to recall");
    require(std::abs(parameter(*recalled, "GEQ 1000 Hz").getValue() - 16.5f / 24.0f) < 0.001f,
            "GEQ gain failed to recall");
    require(std::abs(parameter(*recalled, "GEQ Saturation").getValue() - 0.75f) < 0.001f
            && parameter(*recalled, "GEQ Position").getValue() > 0.99f, "saturation and GEQ position failed to recall");
    require(processConstant(*recalled, 180) > 1.20f, "Restored VST3 state lost order or settings");
    require(std::abs(processConstant(*recalled, 180, 0.01f) - 0.03981f) < 0.001f, "Restored VST3 state lost band lift");
    parameter(*recalled, "GEQ Enabled").setValueNotifyingHost(1.0f);
    const auto rounded = processConstant(*recalled, 180);
    require(rounded > 0.3f && rounded < 0.62f, "VST3 saturation rounds the boosted signal after the EQ");
    parameter(*recalled, "Bypass").setValueNotifyingHost(1.0f);
    require(std::abs(processConstant(*recalled, 80) - 0.62f) < 0.0001f, "VST3 bypass did not restore dry audio");
    recalled->releaseResources();

    std::cout << "Short Bus VST3 scanned, processed, reordered and recalled state\n";
}
