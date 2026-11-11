// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Dynamics.h"

namespace shortbus::dsp
{
struct FeedbackControl
{
    float reduction;
    float slope;
};

// Invert the soft-knee input/output curve for a detector after the gain element.
// See docs/compressor-models.md for the derivation and public references.
inline FeedbackControl feedbackControl(float outputDb, const DynamicsSettings& settings)
{
    const auto ratio = settings.ratio;
    const auto over = outputDb - settings.threshold;
    const auto halfKnee = settings.knee * 0.5f;
    auto reduction = std::max(0.0f, (ratio - 1.0f) * over);
    auto slope = ratio - 1.0f;
    if (settings.knee > 0.0f && over > -halfKnee && over < halfKnee / ratio)
    {
        const auto strength = 1.0f - 1.0f / ratio;
        const auto distance = over + halfKnee;
        const auto root = std::sqrt(std::max(0.0f, 1.0f - 2.0f * strength * distance / settings.knee));
        const auto position = 2.0f * distance / (1.0f + root);
        reduction = strength * position * position / (2.0f * settings.knee);
        slope = 1.0f / std::max(1.0f / ratio, root) - 1.0f;
    }
    if (over <= -halfKnee)
        return {};
    if (reduction >= settings.range)
        return { settings.range, 0.0f };
    return { reduction, slope };
}

class OptoEnvelope
{
public:
    void configure(float rate, DynamicsSettings next, CompressorTiming timing)
    {
        settings = next;
        rmsCoefficient = timeCoefficient(rate, 5.0f);
        attack = timeCoefficient(rate, timing.attack);
        fastRelease = -std::expm1(-693.14718056f / (rate * timing.release));
        slowRelease = timeCoefficient(rate, timing.release * 8.0f);
        memoryRelease = timeCoefficient(rate, timing.release * 40.0f);
        memoryCharge = timeCoefficient(rate, 400.0f);
        memoryDischarge = timeCoefficient(rate, 1500.0f);
        bypassSlew = timeCoefficient(rate, 10.0f);
    }

    float tick(Frame input)
    {
        const auto gain = gainFromDb(-0.5f * (fast + slow));
        for (std::size_t channel = 0; channel < input.size(); ++channel)
        {
            const auto amplitude = std::clamp(input[channel] * gain, -1.0e12f, 1.0e12f);
            rms[channel] += rmsCoefficient * (amplitude * amplitude - rms[channel]);
        }
        const auto levelDb = 10.0f * std::log10(std::max(1.0e-18f, std::max(rms[0], rms[1])));
        const auto control = feedbackControl(levelDb, settings);
        const auto target = settings.enabled ? control.reduction : 0.0f;
        const auto correction = 1.0f / (1.0f + control.slope);
        const auto tail = slowRelease + memory * memory * (memoryRelease - slowRelease);
        const auto coefficient = [&](float current, float release)
        {
            return settings.enabled ? (target > current ? attack : release) * correction : bypassSlew;
        };
        fast = smoothMix(fast, target, coefficient(fast, fastRelease));
        slow = smoothMix(slow, target, coefficient(slow, tail));
        const auto reduction = 0.5f * (fast + slow);
        const auto exposure = settings.enabled ? std::clamp(reduction / std::max(1.0f, settings.range), 0.0f, 1.0f) : 0.0f;
        memory += (exposure > memory ? memoryCharge : memoryDischarge) * (exposure - memory);
        return -reduction;
    }

private:
    DynamicsSettings settings;
    std::array<float, 2> rms {};
    float fast = 0.0f, slow = 0.0f, memory = 0.0f;
    float rmsCoefficient = 1.0f, attack = 1.0f, fastRelease = 1.0f, slowRelease = 1.0f;
    float memoryRelease = 1.0f, memoryCharge = 1.0f, memoryDischarge = 1.0f, bypassSlew = 1.0f;
};

class FetEnvelope
{
public:
    void configure(float rate, DynamicsSettings next, CompressorTiming timing)
    {
        settings = next;
        attack = timeCoefficient(rate, timing.attack);
        release = timeCoefficient(rate, timing.release);
        slowRelease = timeCoefficient(rate, timing.release * 2.5f);
        detectorFall = 1.0f - timeCoefficient(rate, 1.0f);
        memoryCharge = timeCoefficient(rate, 150.0f);
        memoryDischarge = timeCoefficient(rate, 700.0f);
        bypassSlew = timeCoefficient(rate, 10.0f);
    }

    float tick(Frame input)
    {
        // A variable shunt conductance gives gain = 1 / (1 + conductance).
        const auto gain = 1.0f / (1.0f + conductance);
        detected = std::max(std::min(1.0e12f, peak(input) * gain), detected * detectorFall);
        const auto control = feedbackControl(dbFromGain(detected), settings);
        const auto target = settings.enabled ? gainFromDb(control.reduction) - 1.0f : 0.0f;
        const auto loopSlope = control.slope * (1.0f + target) / (1.0f + conductance);
        const auto recovery = release + memory * (slowRelease - release);
        const auto coefficient = settings.enabled
            ? (target > conductance ? attack : recovery) / (1.0f + loopSlope) : bypassSlew;
        conductance = smoothMix(conductance, target, coefficient);
        const auto reduction = dbFromGain(1.0f + conductance);
        const auto exposure = settings.enabled ? std::clamp(reduction / std::max(1.0f, settings.range), 0.0f, 1.0f) : 0.0f;
        memory += (exposure > memory ? memoryCharge : memoryDischarge) * (exposure - memory);
        return -reduction;
    }

private:
    DynamicsSettings settings;
    float conductance = 0.0f, detected = 0.0f, memory = 0.0f;
    float attack = 1.0f, release = 1.0f, slowRelease = 1.0f, detectorFall = 0.0f;
    float memoryCharge = 1.0f, memoryDischarge = 1.0f, bypassSlew = 1.0f;
};

class Compressor
{
public:
    void configure(float rate, DynamicsSettings settings, CompressorMode next,
                   CompressorTiming optoTiming, CompressorTiming fetTiming)
    {
        clean.configure(rate, settings, Curve::compress);
        opto.configure(rate, settings, optoTiming);
        fet.configure(rate, settings, fetTiming);
        mode = static_cast<std::size_t>(next);
        slew = timeCoefficient(rate, 8.0f);
        if (!configured)
        {
            weights.fill(0.0f);
            weights[mode] = 1.0f;
            configured = true;
        }
    }

    Frame process(Frame input)
    {
        const auto cleanOutput = clean.process(input);
        changes = { clean.meter(), opto.tick(input), fet.tick(input) };
        for (std::size_t i = 0; i < weights.size(); ++i)
            weights[i] = smoothMix(weights[i], i == mode ? 1.0f : 0.0f, slew);
        if (weights[0] >= 1.0f)
            return cleanOutput;
        auto output = scale(cleanOutput, weights[0]);
        for (std::size_t i = 1; i < weights.size(); ++i)
        {
            const auto gain = weights[i] * gainFromDb(changes[i] + clean.makeupDb());
            output[0] += input[0] * gain;
            output[1] += input[1] * gain;
        }
        const auto sum = weights[0] + weights[1] + weights[2];
        return scale(output, 1.0f / sum);
    }

    float meter() const
    {
        const auto sum = weights[0] + weights[1] + weights[2];
        return (weights[0] * changes[0] + weights[1] * changes[1] + weights[2] * changes[2]) / sum;
    }

private:
    Broadband clean;
    OptoEnvelope opto;
    FetEnvelope fet;
    std::array<float, 3> weights { 1.0f, 0.0f, 0.0f }, changes {};
    std::size_t mode = 0;
    float slew = 1.0f;
    bool configured = false;
};
} // namespace shortbus::dsp
