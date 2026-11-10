// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Types.h"

namespace shortbus::dsp
{
enum class Curve { expand, lift, compress };

// Signed gain change in dB, excluding makeup.
inline float staticGain(float inputDb, const DynamicsSettings& settings, Curve curve)
{
    const auto over = curve == Curve::compress ? inputDb - settings.threshold : settings.threshold - inputDb;
    const auto halfKnee = 0.5f * settings.knee;
    auto excursion = std::max(0.0f, over);
    if (settings.knee > 0.0f && over > -halfKnee && over < halfKnee)
    {
        const auto position = over + halfKnee;
        excursion = position * position / (2.0f * settings.knee);
    }
    const auto slope = curve == Curve::expand ? settings.ratio - 1.0f : 1.0f - 1.0f / settings.ratio;
    const auto change = std::min(settings.range, excursion * slope);
    return curve == Curve::lift ? change : -change;
}

class GainEnvelope
{
public:
    void configure(float rate, DynamicsSettings next, Curve shape)
    {
        settings = next;
        curve = shape;
        attack = timeCoefficient(rate, next.attack);
        release = timeCoefficient(rate, next.release);
        detectorFall = 1.0f - timeCoefficient(rate, 12.0f);
        slew = timeCoefficient(rate, 10.0f);
    }

    float tick(float amplitude)
    {
        detected = std::max(amplitude, detected * detectorFall);
        if (!settings.enabled && std::abs(gainDb) < 1.0e-5f)
            return gainDb;

        const auto target = settings.enabled ? staticGain(dbFromGain(detected), settings, curve) : 0.0f;
        // An expander opens with attack and closes with release.
        const auto attacking = curve == Curve::expand ? target > gainDb : std::abs(target) > std::abs(gainDb);
        gainDb += (settings.enabled ? (attacking ? attack : release) : slew) * (target - gainDb);
        if (std::abs(gainDb) < 1.0e-5f)
            gainDb = 0.0f;
        return gainDb;
    }

    float current() const { return gainDb; }

private:
    DynamicsSettings settings;
    Curve curve = Curve::compress;
    float gainDb = 0.0f, detected = 0.0f;
    float attack = 1.0f, release = 1.0f, detectorFall = 0.0f, slew = 1.0f;
};

class Broadband
{
public:
    void configure(float rate, DynamicsSettings settings, Curve curve)
    {
        envelope.configure(rate, settings, curve);
        makeupTarget = settings.enabled ? settings.makeup : 0.0f;
        makeupSlew = timeCoefficient(rate, 10.0f);
    }

    Frame process(Frame input)
    {
        makeup += makeupSlew * (makeupTarget - makeup);
        if (std::abs(makeup - makeupTarget) < 1.0e-5f)
            makeup = makeupTarget;
        const auto gainDb = envelope.tick(peak(input)) + makeup;
        return std::abs(gainDb) < 1.0e-5f ? input : scale(input, gainFromDb(gainDb));
    }

    float meter() const { return envelope.current(); }

private:
    GainEnvelope envelope;
    float makeup = 0.0f, makeupTarget = 0.0f, makeupSlew = 1.0f;
};

struct OnePole
{
    float state = 0.0f;

    float lowpass(float input, float coefficient)
    {
        const auto step = (input - state) * (coefficient / (1.0f + coefficient));
        const auto output = state + step;
        state = output + step;
        return output;
    }
};

class Band
{
public:
    void configure(float rate, BandSettings settings, bool stageEnabled)
    {
        const auto edges = effectiveEdges(settings.lowHz, settings.highHz, rate);
        const auto upper = static_cast<float>(bandCeilingHz(rate));
        constexpr float pi = 3.14159265358979323846f;
        low.configure(std::tan(pi * edges.low / rate), edges.low > minHz + 0.5f);
        high.configure(std::tan(pi * edges.high / rate), edges.high < upper - 1.0f);
        edgeSlew = timeCoefficient(rate, 12.0f);
        settings.dynamics.enabled = stageEnabled && settings.dynamics.enabled;
        envelope.configure(rate, settings.dynamics, settings.lift ? Curve::lift : Curve::compress);
    }

    Frame process(Frame detectorInput, Frame signal)
    {
        low.tick(edgeSlew);
        high.tick(edgeSlew);
        Frame isolated {};
        for (std::size_t channel = 0; channel < signal.size(); ++channel)
        {
            const auto input = detectorInput[channel];
            const auto highPassed = input - low.mix * detectorLow[channel].lowpass(input, low.coefficient);
            const auto lowPassed = detectorHigh[channel].lowpass(highPassed, high.coefficient);
            isolated[channel] = highPassed + high.mix * (lowPassed - highPassed);
        }

        const auto db = envelope.tick(peak(isolated));
        // The matched shelves restore unity away from the band and at zero gain change.
        const auto rootGain = std::abs(db) > 0.0f ? gainFromDb(0.5f * db) : 1.0f;
        const auto inverse = 1.0f / rootGain;
        const auto delta = rootGain * rootGain - 1.0f;
        const auto inverseDelta = inverse * inverse - 1.0f;
        for (std::size_t channel = 0; channel < signal.size(); ++channel)
        {
            const auto input = signal[channel];
            const auto bottom = shelfLow[channel].lowpass(input, low.coefficient * rootGain);
            const auto shelved = input + delta * (input - bottom);
            const auto top = shelfHigh[channel].lowpass(shelved, high.coefficient * inverse);
            const auto bounded = shelved + high.mix * inverseDelta * (shelved - top);
            const auto lowPass = lowOnly[channel].lowpass(input, high.coefficient * inverse);
            const auto shelf = input + high.mix * delta * lowPass;
            signal[channel] = shelf + low.mix * (bounded - shelf);
        }
        return signal;
    }

    float meter() const { return envelope.current(); }

private:
    struct Edge
    {
        void configure(float next, bool active)
        {
            target = next;
            mixTarget = active ? 1.0f : 0.0f;
            if (coefficient <= 0.0f)
            {
                coefficient = target;
                mix = mixTarget;
            }
        }

        void tick(float slew)
        {
            coefficient += slew * (target - coefficient);
            mix = smoothMix(mix, mixTarget, slew);
        }

        float coefficient = 0.0f, target = 0.0f, mix = 0.0f, mixTarget = 0.0f;
    };

    GainEnvelope envelope;
    Edge low, high;
    std::array<OnePole, 2> detectorLow {}, detectorHigh {}, shelfLow {}, shelfHigh {}, lowOnly {};
    float edgeSlew = 1.0f;
};

} // namespace shortbus::dsp
