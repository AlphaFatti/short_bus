// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Dynamics.h"
#include <numeric>

namespace shortbus::dsp
{
using BandLevels = std::array<float, bandCount>;

class BandAssist
{
public:
    void prepare(float rate)
    {
        clear();
        energySlew = timeCoefficient(rate, 40.0f);
        attack = timeCoefficient(rate, 42.0f);
        release = timeCoefficient(rate, 180.0f);
        offSlew = timeCoefficient(rate, 10.0f);
        interval = std::max(1, static_cast<int>(std::lround(rate / 100.0f)));
        const auto updateRate = rate / static_cast<float>(interval);
        learnFast = timeCoefficient(updateRate, 150.0f);
        learnSlow = timeCoefficient(updateRate, 1800.0f);
    }

    void clear()
    {
        resetDetector();
        targets = cuts = {};
    }

    void configure(AssistSettings settings, bool stageEnabled)
    {
        profile = settings.profile;
        focus = settings.focus * 0.01f;
        if (enabled && !stageEnabled)
            resetDetector();
        enabled = stageEnabled;
        if (!enabled)
            targets = {};
    }

    const BandLevels& process(Frame input, const BandLevels& levels)
    {
        if (enabled)
        {
            const auto power = 0.5f * (input[0] * input[0] + input[1] * input[1]);
            inputPower += energySlew * (power - inputPower);
            for (std::size_t i = 0; i < energy.size(); ++i)
                energy[i] += energySlew * (levels[i] * levels[i] - energy[i]);
            if (untilUpdate == 0)
            {
                updateTargets();
                untilUpdate = interval;
            }
            --untilUpdate;
        }
        for (std::size_t i = 0; i < cuts.size(); ++i)
        {
            const auto coefficient = !enabled ? offSlew : targets[i] < cuts[i] ? attack : release;
            cuts[i] = smoothMix(cuts[i], targets[i], coefficient);
        }
        return cuts;
    }

private:
    void resetDetector()
    {
        energy = reference = {};
        inputPower = 0.0f;
        untilUpdate = audibleWindows = quietWindows = 0;
    }

    void updateTargets()
    {
        // Only bass and low mids can trigger on steady energy. Higher bands
        // need a rise over the ongoing mix, with smaller maximum cuts.
        constexpr BandLevels speechSteady { 0.29f, 0.40f, 1.0f, 1.0f, 1.0f };
        constexpr BandLevels musicSteady  { 0.39f, 0.46f, 1.0f, 1.0f, 1.0f };
        constexpr BandLevels minimumShare { 0.20f, 0.30f, 0.42f, 0.28f, 0.24f };
        constexpr BandLevels riseFactor { 1.7f, 1.7f, 2.2f, 2.5f, 3.0f };
        constexpr BandLevels speechLimits { assistMaxCutDb, 2.0f, 0.65f, 0.50f, 0.35f };
        constexpr BandLevels musicLimits { musicAssistMaxCutDb, 1.5f, 0.55f, 0.40f, 0.30f };
        const auto speech = profile == AssistProfile::speech;
        const auto& steady = speech ? speechSteady : musicSteady;
        const auto& limits = speech ? speechLimits : musicLimits;
        // Taper the effect across -54 to -46 dBFS RMS to stay out of quiet gaps.
        constexpr float closed = 3.981072e-6f;
        constexpr float open = 2.511886e-5f;
        const auto total = std::accumulate(energy.begin(), energy.end(), 0.0f);
        targets = {};
        if (inputPower <= closed || total <= 0.0f)
        {
            constexpr int quietReset = 180;
            quietWindows = std::min(quietReset, quietWindows + 1);
            if (quietWindows == quietReset)
                audibleWindows = 0;
            return;
        }
        quietWindows = 0;
        constexpr int warmup = 30;
        const auto learning = audibleWindows < warmup;
        const auto amount = focus * std::min(1.0f, (inputPower - closed) / (open - closed));
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            const auto share = energy[i] / total;
            auto& baseline = reference[i];
            const auto transient = std::max(minimumShare[i], baseline * riseFactor[i]);
            const auto threshold = learning ? steady[i] : std::min(steady[i], transient);
            if (share > threshold && amount > 0.0f)
                targets[i] = -amount * std::min(limits[i], 5.5f * std::log10(share / threshold));
            baseline = audibleWindows == 0 ? share : baseline + (learning ? learnFast : learnSlow) * (share - baseline);
        }
        audibleWindows = std::min(warmup, audibleWindows + 1);
    }

    BandLevels energy {}, reference {}, targets {}, cuts {};
    AssistProfile profile = AssistProfile::speech;
    bool enabled = false;
    float focus = 0.0f, inputPower = 0.0f;
    float energySlew = 1.0f, attack = 1.0f, release = 1.0f, offSlew = 1.0f;
    float learnFast = 1.0f, learnSlow = 1.0f;
    int interval = 1, untilUpdate = 0, audibleWindows = 0, quietWindows = 0;
};

class Multiband
{
public:
    void prepare(float rate)
    {
        manualBands = assistBands = {};
        assist.prepare(rate);
        sampleRate = rate;
        transition = timeCoefficient(rate, 12.0f);
        configured = false;
    }

    void configure(const Settings& settings)
    {
        assistTarget = settings.assist.enabled ? 1.0f : 0.0f;
        manualTarget = !settings.assist.enabled || settings.assist.keepManual ? 1.0f : 0.0f;
        if (assistTarget > 0.0f && assistMix <= 0.0f)
        {
            assist.clear();
            assistBands = {};
        }
        assist.configure(settings.assist, settings.multiEnabled && settings.assist.enabled);
        if (!configured)
        {
            assistMix = assistTarget;
            manualMix = manualTarget;
            configured = true;
        }
        if (manualMix > 0.0f || manualTarget > 0.0f)
            configureBands(manualBands, settings.bands, settings.multiEnabled);
        if (assistMix > 0.0f || assistTarget > 0.0f)
            configureBands(assistBands, bandDefaults, false);
    }

    Frame process(Frame input)
    {
        auto output = input;
        manualMix = smoothMix(manualMix, manualTarget, transition);
        if (manualMix > 0.0f)
        {
            for (auto& band : manualBands)
                output = band.process(input, output);
            output = blend(input, output, manualMix);
        }
        assistMix = smoothMix(assistMix, assistTarget, transition);
        if (assistMix <= 0.0f)
            return output;

        BandLevels detected {};
        for (std::size_t i = 0; i < assistBands.size(); ++i)
            detected[i] = assistBands[i].detect(output);
        const auto& cuts = assist.process(output, detected);
        for (std::size_t i = 0; i < assistBands.size(); ++i)
            output = assistBands[i].applyGain(output, assistMix * cuts[i]);
        return output;
    }

    float meter(std::size_t band) const
    {
        return assistTarget > 0.0f ? assistBands[band].meter() : manualMix * manualBands[band].meter();
    }

private:
    void configureBands(std::array<Band, bandCount>& bands, const std::array<BandSettings, bandCount>& requested, bool enabled)
    {
        const auto spans = orderedBandSpans(requested, sampleRate);
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            auto band = requested[i];
            band.lowHz = spans[i].low;
            band.highHz = spans[i].high;
            bands[i].configure(sampleRate, band, enabled);
        }
    }

    std::array<Band, bandCount> manualBands, assistBands;
    BandAssist assist;
    float sampleRate = 48000.0f, transition = 1.0f;
    float assistMix = 0.0f, assistTarget = 0.0f;
    float manualMix = 1.0f, manualTarget = 1.0f;
    bool configured = false;
};
} // namespace shortbus::dsp
