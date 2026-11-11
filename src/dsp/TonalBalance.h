// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Types.h"
#include <numeric>

namespace shortbus::dsp
{
constexpr int tonalBandCount = 30;
using TonalCurve = std::array<float, tonalBandCount>;
constexpr float defaultRtaFloorDb = -65.0f;

inline bool tonalAboveFloor(const float* left, const float* right, int samples, float floorDb, bool active)
{
    double power = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        const auto l = static_cast<double>(left[i]);
        const auto r = static_cast<double>(right[i]);
        power += l * l + r * r;
    }
    // Require a little more level to open than to keep displaying the current signal.
    const auto minimum = gainFromDb(floorDb + (active ? 0.0f : 3.0f));
    return samples > 0 && std::isfinite(power) && power >= 2.0 * samples * minimum * minimum;
}

struct TonalReference
{
    TonalCurve power;
    int bands = tonalBandCount;
};

inline const auto tonalEdges = []
{
    std::array<float, tonalBandCount + 1> edges {};
    for (std::size_t i = 0; i < edges.size(); ++i)
        edges[i] = minHz * std::pow(maxHz / minHz, static_cast<float>(i) / tonalBandCount);
    edges.back() = maxHz;
    return edges;
}();

inline int tonalBandsAtRate(float rate)
{
    const auto boundary = std::upper_bound(tonalEdges.begin() + 1, tonalEdges.end(), rate * 0.5f);
    return static_cast<int>(boundary - tonalEdges.begin()) - 1;
}

inline TonalCurve tonalBandPower(const float* left, const float* right, int fftSize, float rate)
{
    TonalCurve power {};
    const auto binWidth = rate / static_cast<float>(fftSize);
    const auto fftScale = 1.0f / static_cast<float>(fftSize);
    const auto count = tonalBandsAtRate(rate);
    for (int band = 0; band < count; ++band)
    {
        const auto i = static_cast<std::size_t>(band);
        const auto low = tonalEdges[i] / binWidth;
        const auto high = tonalEdges[i + 1] / binWidth;
        const auto first = std::max(1, static_cast<int>(std::ceil(low - 0.5f)));
        const auto last = std::min(fftSize / 2, static_cast<int>(std::floor(high + 0.5f)));
        for (auto bin = first; bin <= last; ++bin)
        {
            const auto center = static_cast<float>(bin);
            const auto overlap = std::min(high, center + 0.5f) - std::max(low, center - 0.5f);
            const auto l = left[bin] * fftScale;
            const auto r = right[bin] * fftScale;
            power[i] += 0.5f * overlap * (l * l + r * r);
        }
    }
    return power;
}

inline float tonalEnergy(const TonalCurve& power, int first = 0, int end = tonalBandCount)
{
    return std::accumulate(power.begin() + first, power.begin() + end, 0.0f);
}

inline TonalCurve normalizedTone(TonalCurve power, int bands = tonalBandCount)
{
    std::fill(power.begin() + bands, power.end(), 0.0f);
    const auto sum = tonalEnergy(power);
    if (!std::isfinite(sum) || sum <= 0.0f)
        return {};
    for (auto& value : power)
        value /= sum;
    return power;
}

inline float tonalDb(float power) { return 10.0f * std::log10(std::max(1.0e-9f, power)); }

inline const auto tonalTargets = []
{
    // Original starting curves, with anchors spaced evenly on the log-frequency axis.
    constexpr std::array<std::array<float, 8>, 3> anchors {{
        {{ -6.0f, 1.0f, 1.5f, 0.5f, 0.0f, -1.0f, -3.0f, -7.0f }},
        {{ -1.0f, 4.5f, 3.5f, 1.0f, -0.5f, -2.0f, -4.0f, -8.0f }},
        {{ -28.0f, -14.0f, -3.0f, 1.5f, 3.0f, 2.0f, -2.0f, -12.0f }}
    }};
    std::array<TonalCurve, anchors.size()> targets {};
    for (std::size_t target = 0; target < targets.size(); ++target)
    {
        auto& curve = targets[target];
        for (std::size_t band = 0; band < curve.size(); ++band)
        {
            const auto point = 7.0f * static_cast<float>(band) / static_cast<float>(curve.size() - 1);
            const auto left = std::min(static_cast<std::size_t>(point), anchors[target].size() - 2);
            const auto a = anchors[target][left];
            const auto b = anchors[target][left + 1];
            curve[band] = gainFromDb(2.0f * (a + (point - static_cast<float>(left)) * (b - a)));
        }
        curve = normalizedTone(curve);
    }
    return targets;
}();
} // namespace shortbus::dsp
