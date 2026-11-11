// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Types.h"

namespace shortbus::dsp
{
class GraphicEq
{
public:
    void prepare(float rate)
    {
        slew = timeCoefficient(rate, 10.0f);
        rampLength = std::max(1, static_cast<int>(std::lround(rate * 0.015f)));
        for (std::size_t i = 0; i < bands.size(); ++i)
            bands[i].prepare(rate, geqFrequencies[i]);
        configured = false;
    }

    void configure(const GraphicEqSettings& settings)
    {
        for (std::size_t i = 0; i < bands.size(); ++i)
            bands[i].setGain(settings.gains[i], configured ? rampLength : 0);
        wetTarget = settings.enabled ? 1.0f : 0.0f;
        saturationTarget = settings.saturation * 0.01f;
        if (!configured)
        {
            wet = wetTarget;
            saturation = saturationTarget;
            configured = true;
        }
    }

    Frame process(Frame input)
    {
        const auto wasWet = wet > 0.0f;
        wet = smoothMix(wet, wetTarget, slew);
        saturation = smoothMix(saturation, saturationTarget, slew);
        if (wet <= 0.0f)
        {
            if (wasWet)
                for (auto& band : bands)
                    band.reset();
            return input;
        }

        std::array<double, 2> signal { input[0], input[1] };
        for (auto& band : bands)
            band.process(signal);

        if (saturation > 0.0f)
        {
            // This soft clipper keeps unity slope around zero and rounds larger peaks.
            const auto strength = 6.0 * saturation * saturation;
            for (auto& sample : signal)
                sample /= std::sqrt(1.0 + strength * sample * sample);
        }
        return blend(input, { static_cast<float>(signal[0]), static_cast<float>(signal[1]) }, wet);
    }

private:
    struct PeakingBand
    {
        void prepare(float rate, float frequency)
        {
            available = frequency < rate * 0.5f;
            if (available)
            {
                constexpr double twoPi = 6.2831853071795864769;
                const auto omega = twoPi * frequency / rate;
                const auto alphaAtOneThirdOctave = std::sinh(std::log(2.0) / 6.0);
                alpha = std::sin(omega) * alphaAtOneThirdOctave;
                cosine = std::cos(omega);
                current = target = coefficients(0.0f);
            }
            else
            {
                current = target = {};
            }
            targetDb = 0.0f;
            remaining = 0;
            reset();
        }

        void setGain(float gain, int samples)
        {
            if (!available || std::abs(gain - targetDb) < 0.001f)
                return;
            targetDb = gain;
            target = coefficients(gain);
            remaining = samples;
            if (samples == 0)
            {
                current = target;
                return;
            }
            step = { (target.delta - current.delta) / samples,
                     (target.a1 - current.a1) / samples, (target.a2 - current.a2) / samples };
        }

        void process(std::array<double, 2>& signal)
        {
            if (remaining > 0)
            {
                current.delta += step.delta;
                current.a1 += step.a1;
                current.a2 += step.a2;
                if (--remaining == 0)
                {
                    current = target;
                    if (std::abs(targetDb) < 0.001f)
                        reset();
                }
            }
            if (std::abs(current.delta) < 1.0e-15)
                return;

            // Peaking biquad as unity plus a scaled bandpass, so flat bands pass samples exactly.
            for (std::size_t i = 0; i < signal.size(); ++i)
            {
                const auto input = signal[i];
                const auto band = input + z1[i];
                z1[i] = z2[i] - current.a1 * band;
                z2[i] = -input - current.a2 * band;
                signal[i] += current.delta * band;
            }
        }

        void reset()
        {
            z1.fill(0.0);
            z2.fill(0.0);
        }

        struct Coefficients { double delta = 0.0, a1 = 0.0, a2 = 0.0; };

        Coefficients coefficients(float gain) const
        {
            const auto amplitude = std::pow(10.0, gain / 40.0);
            const auto inverseA0 = 1.0 / (1.0 + alpha / amplitude);
            return { alpha * (amplitude - 1.0 / amplitude) * inverseA0,
                     -2.0 * cosine * inverseA0, (1.0 - alpha / amplitude) * inverseA0 };
        }

        Coefficients current, target, step;
        std::array<double, 2> z1 {}, z2 {};
        double cosine = 0.0, alpha = 0.0;
        float targetDb = 0.0f;
        int remaining = 0;
        bool available = false;
    };

    std::array<PeakingBand, geqBandCount> bands;
    float wet = 0.0f, wetTarget = 0.0f, slew = 1.0f;
    float saturation = 0.0f, saturationTarget = 0.0f;
    int rampLength = 1;
    bool configured = false;
};
} // namespace shortbus::dsp
