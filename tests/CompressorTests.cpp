// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/Compressor.h"
#include <cstdlib>
#include <iostream>
#include <random>

using namespace shortbus::dsp;

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

void cleanCompatibility()
{
    std::mt19937 random(42);
    std::uniform_real_distribution<float> signal(-0.8f, 0.8f);
    for (const auto rate : { 8000.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f })
    {
        Broadband original;
        Compressor compressor;
        DynamicsSettings settings { true, -24.0f, 3.0f, 25.0f, 180.0f, 6.0f, 12.0f, 3.0f };
        for (int block = 0; block < 50; ++block)
        {
            settings.enabled = block < 30;
            settings.threshold = block < 20 ? -24.0f : -16.0f;
            original.configure(rate, settings, Curve::compress);
            compressor.configure(rate, settings, CompressorMode::clean, optoTimingDefaults, fetTimingDefaults);
            for (int sample = 0; sample < 256; ++sample)
            {
                const Frame input { signal(random), signal(random) };
                require(original.process(input) == compressor.process(input), "Clean matches the original calculation sample for sample");
            }
        }
    }
}

void feedbackCurve()
{
    for (const auto ratio : { 1.0f, 2.0f, 4.0f, 12.0f })
        for (const auto knee : { 0.0f, 6.0f, 18.0f })
            for (const auto range : { 0.0f, 4.0f, 24.0f })
            {
                const DynamicsSettings settings { true, -24.0f, ratio, 10.0f, 60.0f, knee, range, 0.0f };
                for (int db = -70; db <= 6; ++db)
                {
                    const auto input = static_cast<float>(db);
                    const auto change = staticGain(input, settings, Curve::compress);
                    const auto control = feedbackControl(input + change, settings);
                    require(std::abs(control.reduction + change) < 0.0002f, "feedback knee matches the requested static curve");
                    require(control.slope >= 0.0f && std::isfinite(control.slope), "feedback slope stays finite");
                }
            }
}

void characterGain()
{
    for (const auto rate : { 8000.0f, 48000.0f, 96000.0f, 192000.0f })
        for (const auto mode : { CompressorMode::opto, CompressorMode::fet })
            for (const auto ratio : { 1.0f, 4.0f, 12.0f })
                for (const auto inputDb : { -40.0f, -24.0f, -6.0f })
                {
                    DynamicsSettings settings { true, -24.0f, ratio, 25.0f, 180.0f, 6.0f, 12.0f, 0.0f };
                    Compressor compressor;
                    compressor.configure(rate, settings, mode, optoTimingDefaults, { 0.02f, 50.0f });
                    const auto amplitude = gainFromDb(inputDb);
                    const Frame input { amplitude, -0.25f * amplitude };
                    Frame output {};
                    for (int sample = 0; sample < static_cast<int>(rate); ++sample)
                    {
                        output = compressor.process(input);
                        require(std::isfinite(output[0]) && std::isfinite(output[1]), "character modes remain finite");
                        require(std::abs(output[1] + 0.25f * output[0]) < 1.0e-6f, "character compression is stereo linked");
                        require(compressor.meter() >= -settings.range - 0.001f && compressor.meter() <= 0.001f, "character gain respects the range cap");
                    }
                    const auto expected = staticGain(inputDb, settings, Curve::compress);
                    require(std::abs(compressor.meter() - expected) < 0.02f, "character mode settles to its static curve");
                    settings.enabled = false;
                    compressor.configure(rate, settings, mode, optoTimingDefaults, fetTimingDefaults);
                    for (int sample = 0; sample < static_cast<int>(rate * 0.3f); ++sample)
                        output = compressor.process(input);
                    require(std::abs(output[0] - input[0]) < 1.0e-5f, "stage off recovers unity despite optical memory");
                }
}

float optoRecovery(float duration)
{
    constexpr float rate = 48000.0f;
    const DynamicsSettings settings { true, -30.0f, 4.0f, 25.0f, 180.0f, 6.0f, 12.0f, 0.0f };
    OptoEnvelope opto;
    opto.configure(rate, settings, optoTimingDefaults);
    auto before = 0.0f;
    for (int i = 0; i < static_cast<int>(duration * rate); ++i)
        before = opto.tick({ 0.5f, -0.25f });
    auto after = before;
    for (int i = 0; i < 48000; ++i)
        after = opto.tick({});
    return after / before;
}

float fetRecovery(float duration)
{
    constexpr float rate = 48000.0f;
    const DynamicsSettings settings { true, -30.0f, 4.0f, 25.0f, 180.0f, 6.0f, 12.0f, 0.0f };
    FetEnvelope fet;
    fet.configure(rate, settings, fetTimingDefaults);
    auto before = 0.0f;
    for (int i = 0; i < static_cast<int>(duration * rate); ++i)
        before = fet.tick({ 0.5f, -0.25f });
    auto after = before;
    for (int i = 0; i < 9600; ++i)
        after = fet.tick({});
    return after / before;
}

void rmsSensing()
{
    const DynamicsSettings settings { true, -24.0f, 4.0f, 25.0f, 180.0f, 6.0f, 24.0f, 0.0f };
    OptoEnvelope sine, equivalentDc;
    sine.configure(48000.0f, settings, optoTimingDefaults);
    equivalentDc.configure(48000.0f, settings, optoTimingDefaults);
    float toneReduction = 0.0f, dcReduction = 0.0f;
    for (int i = 0; i < 96000; ++i)
    {
        const auto sample = 0.5f * std::sin(6.28318530718f * 1000.0f * static_cast<float>(i) / 48000.0f);
        toneReduction = sine.tick({ sample, -sample });
        dcReduction = equivalentDc.tick({ 0.35355339059f, -0.35355339059f });
    }
    require(std::abs(toneReduction - dcReduction) < 0.1f, "Opto responds to RMS level without stereo phase cancellation");
}

void timingAndSwitching()
{
    const DynamicsSettings settings { true, -30.0f, 4.0f, 25.0f, 180.0f, 6.0f, 12.0f, 0.0f };
    FetEnvelope fast, slow;
    OptoEnvelope opto;
    fast.configure(48000.0f, settings, { 0.02f, 150.0f });
    slow.configure(48000.0f, settings, { 0.8f, 150.0f });
    opto.configure(48000.0f, settings, optoTimingDefaults);
    float fastDb = 0.0f, slowDb = 0.0f, optoDb = 0.0f;
    for (int i = 0; i < 24; ++i)
    {
        fastDb = fast.tick({ 0.5f, -0.25f });
        slowDb = slow.tick({ 0.5f, -0.25f });
        optoDb = opto.tick({ 0.5f, -0.25f });
    }
    require(fastDb < slowDb - 1.0f && fastDb < optoDb - 2.0f, "FET attack catches sub-millisecond transients");
    require(optoRecovery(3.0f) > optoRecovery(0.05f) + 0.06f, "optical recovery depends on prior exposure");
    require(fetRecovery(3.0f) > fetRecovery(0.05f) + 0.03f, "FET release lengthens with sustained compression");

    Compressor compressor;
    auto previous = 0.0f;
    for (int block = 0; block < 250; ++block)
    {
        const auto mode = static_cast<CompressorMode>(block < 100 ? 0 : block % 3);
        compressor.configure(48000.0f, settings, mode, optoTimingDefaults, fetTimingDefaults);
        for (int sample = 0; sample < 128; ++sample)
        {
            const auto output = compressor.process({ 0.25f, -0.125f });
            require(std::isfinite(output[0]), "rapid mode changes stay finite");
            if (block >= 100)
                require(std::abs(output[0] - previous) < 0.001f, "mode changes use a smooth transition");
            previous = output[0];
        }
    }
}
} // namespace

int main()
{
    cleanCompatibility();
    feedbackCurve();
    characterGain();
    rmsSensing();
    timingAndSwitching();
    std::cout << "Clean compatibility, feedback curves, optical memory and FET timing passed\n";
}
