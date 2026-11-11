// SPDX-License-Identifier: AGPL-3.0-only
#include "Processor.h"
#include <cstdlib>
#include <iostream>

using namespace juce;
using namespace shortbus;

namespace
{
void check(bool condition, const char* name)
{
    if (!condition)
    {
        std::cerr << "Failed: " << name << '\n';
        std::exit(1);
    }
}

void set(Processor& processor, const String& id, float value)
{
    auto* parameter = processor.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void historyWindows(int rate)
{
    auto measured = std::make_unique<Processor>();
    auto reference = std::make_unique<Processor>();
    for (auto* processor : { measured.get(), reference.get() })
    {
        set(*processor, "comp_enabled", 1.0f);
        set(*processor, "comp_threshold", -26.0f);
        set(*processor, "comp_ratio", 4.0f);
        processor->setRtaPosition(3);
        processor->prepareToPlay(rate, 2301);
    }
    measured->setMeterHistoryActive(true);
    const auto window = rate / Processor::historyHz;
    const auto length = window * 12 + window / 3;
    const auto level = [](int index) { return 0.04f * static_cast<float>(index + 1); };
    const std::array sizes { 17, 761, 2301, 3, 512 };
    int offset = 0;
    MidiBuffer midi;
    for (std::size_t block = 0; offset < length; ++block)
    {
        const auto samples = std::min(sizes[block % sizes.size()], length - offset);
        AudioBuffer<float> audio(2, samples), dryCheck(2, samples);
        for (int i = 0; i < samples; ++i)
        {
            const auto sample = level((offset + i) / window);
            audio.setSample(0, i, sample);
            audio.setSample(1, i, -0.5f * sample);
        }
        dryCheck.makeCopyOf(audio);
        measured->processBlock(audio, midi);
        reference->processBlock(dryCheck, midi);
        for (int i = 0; i < samples; ++i)
            check(std::abs(audio.getSample(0, i) - dryCheck.getSample(0, i)) < 1.0e-8f,
                  "sampling history preserves the audio output");
        offset += samples;
    }

    std::array<Processor::HistoryFrame, 16> frames;
    const auto count = measured->readMeterHistory(frames.data(), static_cast<int>(frames.size()));
    check(count == 12, "history windows follow samples across uneven audio blocks");
    const auto comp = dsp::index(dsp::Stage::compressor);
    for (int i = 0; i < count; ++i)
    {
        const auto& frame = frames[static_cast<std::size_t>(i)];
        const auto& levels = frame.levels.stages[comp];
        check(frame.sequence == static_cast<std::uint32_t>(i) && std::abs(levels.inputPeak - level(i)) < 1.0e-6f,
              "window peaks arrive in chronological order");
        check(std::abs(frame.tapPeak - levels.outputPeak) < 1.0e-6f, "history follows the chosen RTA tap");
    }
    check(frames[11].levels.stages[comp].gainDb < -3.0f, "history records real compressor gain change");
    set(*measured, param::bypass, 1.0f);
    AudioBuffer<float> bypassed(2, window * 2);
    for (int channel = 0; channel < 2; ++channel)
        std::fill_n(bypassed.getWritePointer(channel), bypassed.getNumSamples(), 0.2f);
    measured->processBlock(bypassed, midi);
    const auto bypassCount = measured->readMeterHistory(frames.data(), static_cast<int>(frames.size()));
    check(bypassCount == 2, "history continues while bypassed");
    const auto& dry = frames[1].levels.stages[comp];
    check(std::abs(dry.inputPeak - dry.outputPeak) < 1.0e-6f && std::abs(dry.gainDb) < 1.0e-6f,
          "bypassed history shows dry level with zero gain change");
}

void geqTap()
{
    auto processor = std::make_unique<Processor>();
    set(*processor, param::geqEnabled, 1.0f);
    set(*processor, param::geqGain(17), 9.0f);
    set(*processor, "limit_enabled", 1.0f);
    set(*processor, "limit_ceiling", -18.0f);
    processor->prepareToPlay(48000.0, 512);
    processor->spectrumRequested.store(Processor::SpectrumView::geq);
    const auto order = processor->currentSettings().order;
    const auto position = dsp::stagePosition(dsp::orders[static_cast<std::size_t>(order)], dsp::Stage::geq) + 1;
    const auto key = processor->spectrumKey(Processor::SpectrumView::geq, order, position, false);
    check(key != processor->spectrumKey(Processor::SpectrumView::tonal, order, position, false),
          "the GEQ and movable RTA have separate capture streams");
    constexpr auto size = Processor::tonalFftSize;
    std::array<float, size> left {}, right {};
    AudioBuffer<float> audio(2, 512);
    MidiBuffer midi;
    int sample = 0;
    const auto feed = [&]
    {
        processor->discardSpectrum();
        for (int block = 0; block < 64; ++block)
        {
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const auto value = 0.22f * static_cast<float>(std::sin(6.283185307179586 * sample++ / 48.0));
                audio.setSample(0, i, value);
                audio.setSample(1, i, -value);
            }
            processor->processBlock(audio, midi);
        }
        check(processor->readSpectrum(left.data(), right.data(), size, key), "GEQ RTA has a continuous window");
        check(audio.getMagnitude(0, audio.getNumSamples()) <= dsp::gainFromDb(-18.0f) + 0.001f,
              "downstream limiter still caps the rack output");
        return *std::max_element(left.begin(), left.end());
    };
    const auto equalized = feed();
    check(std::abs(equalized - 0.22f * dsp::gainFromDb(9.0f)) < 0.003f, "GEQ RTA reads after EQ and before the limiter");
    set(*processor, param::saturation, 100.0f);
    const auto saturated = feed();
    check(saturated > 0.25f && saturated < equalized - 0.10f, "GEQ RTA includes saturation");
}
}

int main()
{
    const ScopedJuceInitialiser_GUI init;
    historyWindows(44100);
    historyWindows(48000);
    geqTap();
    std::cout << "Meter timing, audio transparency and GEQ capture passed\n";
}
