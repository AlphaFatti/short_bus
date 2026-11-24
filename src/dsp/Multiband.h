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
        attack = timeCoefficient(rate, 55.0f);
        release = timeCoefficient(rate, 300.0f);
        offSlew = timeCoefficient(rate, 10.0f);
        interval = std::max(1, static_cast<int>(std::lround(rate / 100.0f)));
    }

    void clear()
    {
        energy = targets = cuts = {};
        inputPower = 0.0f;
        untilUpdate = 0;
    }

    void configure(AssistSettings settings, bool stageEnabled)
    {
        profile = settings.profile;
        focus = settings.focus * 0.01f;
        enabled = stageEnabled;
        if (!enabled)
            targets = {};
    }

    const BandLevels& process(Frame input, const BandLevels& levels)
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
        for (std::size_t i = 0; i < cuts.size(); ++i)
        {
            const auto coefficient = !enabled ? offSlew : targets[i] < cuts[i] ? attack : release;
            cuts[i] = smoothMix(cuts[i], targets[i], coefficient);
        }
        return cuts;
    }

private:
    void updateTargets()
    {
        // Share thresholds are tuned for the overlapping detector filters, so a
        // band's energy must dominate before it gets a cut. There is no gain boost.
        constexpr BandLevels speechAllowance { 0.22f, 0.36f, 0.43f, 0.30f, 0.22f };
        constexpr BandLevels musicAllowance  { 0.32f, 0.36f, 0.38f, 0.31f, 0.27f };
        constexpr BandLevels speechLimits { 3.0f, 3.0f, 1.5f, 2.0f, 2.5f };
        const auto speech = profile == AssistProfile::speech;
        const auto& allowances = speech ? speechAllowance : musicAllowance;
        // Taper the effect across -54 to -46 dBFS RMS to stay out of quiet gaps.
        constexpr float closed = 3.981072e-6f;
        constexpr float open = 2.511886e-5f;
        const auto total = std::accumulate(energy.begin(), energy.end(), 0.0f);
        targets = {};
        if (!enabled || inputPower <= closed || total <= 0.0f || focus <= 0.0f)
            return;
        const auto amount = focus * std::min(1.0f, (inputPower - closed) / (open - closed));
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            const auto excess = std::max(1.0f, energy[i] / (total * allowances[i]));
            const auto limit = speech ? speechLimits[i] : assistMaxCutDb;
            targets[i] = -amount * std::min(limit, 10.0f * std::log10(excess));
        }
    }

    BandLevels energy {}, targets {}, cuts {};
    AssistProfile profile = AssistProfile::speech;
    bool enabled = false;
    float focus = 0.0f, inputPower = 0.0f;
    float energySlew = 1.0f, attack = 1.0f, release = 1.0f, offSlew = 1.0f;
    int interval = 1, untilUpdate = 0;
};

class Multiband
{
public:
    void prepare(float rate)
    {
        bands = {};
        assist.prepare(rate);
        sampleRate = rate;
        transition = timeCoefficient(rate, 12.0f);
        configured = false;
    }

    void configure(const Settings& settings)
    {
        const auto& selectedBands = settings.assist.enabled ? bandDefaults : settings.bands;
        const auto spans = orderedBandSpans(selectedBands, sampleRate);
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            auto band = selectedBands[i];
            band.lowHz = spans[i].low;
            band.highHz = spans[i].high;
            bands[i].configure(sampleRate, band, settings.multiEnabled && !settings.assist.enabled);
        }
        assistTarget = settings.assist.enabled ? 1.0f : 0.0f;
        if (assistTarget > 0.0f && assistMix <= 0.0f)
            assist.clear();
        assist.configure(settings.assist, settings.multiEnabled && settings.assist.enabled);
        if (!configured)
        {
            assistMix = assistTarget;
            configured = true;
        }
    }

    Frame process(Frame input)
    {
        auto output = input;
        assistMix = smoothMix(assistMix, assistTarget, transition);
        if (assistMix <= 0.0f)
        {
            for (auto& band : bands)
                output = band.process(input, output);
            return output;
        }

        BandLevels detected {};
        for (std::size_t i = 0; i < bands.size(); ++i)
            detected[i] = bands[i].detect(input);
        const auto& cuts = assist.process(input, detected);
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            const auto manual = bands[i].manualGain(detected[i]);
            const auto db = (1.0f - assistMix) * manual + assistMix * cuts[i];
            output = bands[i].applyGain(output, db);
        }
        return output;
    }

    float meter(std::size_t band) const { return bands[band].meter(); }

private:
    std::array<Band, bandCount> bands;
    BandAssist assist;
    float sampleRate = 48000.0f, transition = 1.0f;
    float assistMix = 0.0f, assistTarget = 0.0f;
    bool configured = false;
};
} // namespace shortbus::dsp
