// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace shortbus::dsp
{
using Frame = std::array<float, 2>;

enum class Stage : std::uint8_t { lowLevel, multiband, compressor, limiter, geq };
enum class LowMode : std::uint8_t { expand, lift };
enum class CompressorMode : std::uint8_t { clean, opto, fet };

struct CompressorTiming { float attack; float release; };
constexpr CompressorTiming optoTimingDefaults { 10.0f, 60.0f };
constexpr CompressorTiming fetTimingDefaults { 0.2f, 150.0f };

constexpr int dynamicsStageCount = 4;
constexpr int stageCount = dynamicsStageCount + 1;
constexpr int bandCount = 5;
constexpr int dynamicsOrderCount = 24;
constexpr int orderCount = dynamicsOrderCount * stageCount;
constexpr float minHz = 20.0f;
constexpr float maxHz = 20000.0f;
constexpr int edgeWidthNumerator = 28;
constexpr int edgeWidthDenominator = 25;
constexpr float edgeRatio = static_cast<float>(edgeWidthNumerator) / edgeWidthDenominator;
constexpr float lookaheadMs = 3.0f;
constexpr float maxRangeDb = 24.0f;

using Order = std::array<Stage, stageCount>;
using DynamicsOrder = std::array<Stage, dynamicsStageCount>;
constexpr DynamicsOrder defaultDynamicsOrder { Stage::lowLevel, Stage::multiband, Stage::compressor, Stage::limiter };

constexpr std::size_t index(Stage stage) { return static_cast<std::size_t>(stage); }

inline constexpr auto dynamicsOrders = []
{
    std::array<DynamicsOrder, dynamicsOrderCount> result {};
    std::size_t i = 0;
    for (auto a : defaultDynamicsOrder)
        for (auto b : defaultDynamicsOrder)
            for (auto c : defaultDynamicsOrder)
                for (auto d : defaultDynamicsOrder)
                    if (a != b && a != c && a != d && b != c && b != d && c != d)
                        result[i++] = { a, b, c, d };
    return result;
}();

template <std::size_t N>
constexpr int stagePosition(const std::array<Stage, N>& route, Stage stage)
{
    for (std::size_t i = 0; i < route.size(); ++i)
        if (route[i] == stage)
            return static_cast<int>(i);
    return static_cast<int>(N);
}

constexpr int rackOrderIndex(int dynamicsOrder, int geqPosition)
{
    const auto limiter = stagePosition(dynamicsOrders[static_cast<std::size_t>(dynamicsOrder)], Stage::limiter);
    if (geqPosition < 0 || geqPosition == limiter)
        return dynamicsOrder;
    const auto offset = geqPosition < limiter ? geqPosition + 1 : geqPosition;
    return dynamicsOrder + offset * dynamicsOrderCount;
}

inline constexpr auto orders = []
{
    std::array<Order, orderCount> result {};
    for (int i = 0; i < dynamicsOrderCount; ++i)
    {
        const auto& dynamics = dynamicsOrders[static_cast<std::size_t>(i)];
        for (int geq = 0; geq < stageCount; ++geq)
        {
            auto& route = result[static_cast<std::size_t>(rackOrderIndex(i, geq))];
            for (int slot = 0; slot < stageCount; ++slot)
                route[static_cast<std::size_t>(slot)] = slot == geq ? Stage::geq
                    : dynamics[static_cast<std::size_t>(slot - (slot > geq ? 1 : 0))];
        }
    }
    return result;
}();

constexpr Order defaultOrder = orders.front();

constexpr std::array geqFrequencies {
    20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f,
    200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f,
    2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f
};
constexpr auto geqBandCount = geqFrequencies.size();
constexpr float geqRangeDb = 12.0f;

inline float gainFromDb(float db) { return std::exp(db * 0.11512925465f); }
inline float dbFromGain(float gain) { return 20.0f * std::log10(std::max(gain, 1.0e-9f)); }
inline float peak(Frame frame) { return std::max(std::abs(frame[0]), std::abs(frame[1])); }

inline Frame scale(Frame frame, float gain)
{
    return { frame[0] * gain, frame[1] * gain };
}

inline Frame blend(Frame from, Frame to, float mix)
{
    if (mix <= 0.0f)
        return from;
    if (mix >= 1.0f)
        return to;
    return { from[0] + mix * (to[0] - from[0]), from[1] + mix * (to[1] - from[1]) };
}

inline float smoothMix(float current, float target, float coefficient)
{
    const auto difference = target - current;
    return std::abs(difference) < 1.0e-4f ? target : current + coefficient * difference;
}

inline float timeCoefficient(float rate, float ms)
{
    return -std::expm1(-1000.0f / (rate * ms));
}

struct DynamicsSettings
{
    bool enabled = false;
    float threshold = -20.0f;
    float ratio = 2.0f;
    float attack = 25.0f;
    float release = 180.0f;
    float knee = 6.0f;
    float range = 12.0f;
    float makeup = 0.0f;
};

constexpr DynamicsSettings lowDefaults { false, -40.0f, 1.5f, 12.0f, 200.0f, 6.0f, 6.0f, 0.0f };
constexpr DynamicsSettings compDefaults {};

struct BandSettings
{
    float lowHz;
    float highHz;
    DynamicsSettings dynamics { true, -24.0f, 1.8f, 25.0f, 180.0f, 6.0f, 4.0f, 0.0f };
    bool lift = false;
};

constexpr std::array<BandSettings, bandCount> bandDefaults {{
    {20.0f, 100.0f}, {100.0f, 350.0f}, {350.0f, 1500.0f},
    {1500.0f, 5500.0f}, {5500.0f, 20000.0f}
}};

struct LimiterSettings
{
    bool enabled = false;
    float ceiling = -1.0f;
    float drive = 0.0f;
    float release = 100.0f;
};

struct GraphicEqSettings
{
    bool enabled = false;
    std::array<float, geqBandCount> gains {};
    float saturation = 0.0f;
};

struct Settings
{
    DynamicsSettings lowLevel = lowDefaults;
    LowMode lowMode = LowMode::expand;
    bool multiEnabled = false;
    std::array<BandSettings, bandCount> bands = bandDefaults;
    DynamicsSettings compressor = compDefaults;
    CompressorMode compressorMode = CompressorMode::clean;
    CompressorTiming optoTiming = optoTimingDefaults;
    CompressorTiming fetTiming = fetTimingDefaults;
    LimiterSettings limiter;
    GraphicEqSettings geq;
    bool bypass = false;
    int order = 0;
};

struct Edges { float low; float high; };

using BandSpans = std::array<Edges, bandCount>;
enum class BandEdge { low, high };

constexpr int minimumHighHz(int low)
{
    return (low * edgeWidthNumerator + edgeWidthDenominator - 1) / edgeWidthDenominator;
}

constexpr int maximumLowHz(int high)
{
    return high * edgeWidthDenominator / edgeWidthNumerator;
}

inline int bandCeilingHz(float rate)
{
    constexpr auto minimumCeiling = []
    {
        int hz = static_cast<int>(minHz);
        for (int i = 0; i < bandCount; ++i)
            hz = minimumHighHz(hz);
        return hz;
    }();
    const auto nyquistLimit = std::isfinite(rate) ? rate * 0.45f : maxHz;
    return static_cast<int>(std::clamp(nyquistLimit, static_cast<float>(minimumCeiling), maxHz));
}

inline Edges effectiveEdges(float low, float high, float rate)
{
    const auto upper = static_cast<float>(bandCeilingHz(rate));
    low = std::clamp(low, minHz, upper / edgeRatio);
    high = std::clamp(high, std::min(upper, low * edgeRatio), upper);
    return { low, high };
}

inline int wholeHz(float requested, float fallback)
{
    const auto hz = std::isfinite(requested) ? requested : fallback;
    return static_cast<int>(std::round(std::clamp(hz, minHz, maxHz)));
}

inline BandSpans orderedBandSpans(const std::array<BandSettings, bandCount>& requested, float rate)
{
    const auto ceiling = bandCeilingHz(rate);
    std::array<int, bandCount> highestLow {};
    auto available = ceiling;
    for (auto i = highestLow.rbegin(); i != highestLow.rend(); ++i)
    {
        available = maximumLowHz(available);
        *i = available;
    }

    // Keep the requested starts where possible, then trim ends at the following start.
    BandSpans spans {};
    auto lowestLow = static_cast<int>(minHz);
    for (std::size_t i = 0; i < spans.size(); ++i)
    {
        const auto low = std::clamp(wholeHz(requested[i].lowHz, bandDefaults[i].lowHz), lowestLow, highestLow[i]);
        spans[i].low = static_cast<float>(low);
        lowestLow = minimumHighHz(low);
    }
    for (std::size_t i = 0; i < spans.size(); ++i)
    {
        const auto highLimit = i + 1 < spans.size() ? static_cast<int>(spans[i + 1].low) : ceiling;
        const auto minHigh = minimumHighHz(static_cast<int>(spans[i].low));
        spans[i].high = static_cast<float>(std::clamp(wholeHz(requested[i].highHz, bandDefaults[i].highHz), minHigh, highLimit));
    }
    return spans;
}

inline float constrainBandFrequency(const BandSpans& spans, std::size_t band, BandEdge edge, float requested, float rate)
{
    const auto span = spans[band];
    if (edge == BandEdge::low)
    {
        const auto left = band > 0 ? static_cast<int>(spans[band - 1].high) : static_cast<int>(minHz);
        const auto right = maximumLowHz(static_cast<int>(span.high));
        return static_cast<float>(std::clamp(wholeHz(requested, span.low), left, right));
    }
    const auto left = minimumHighHz(static_cast<int>(span.low));
    const auto right = band + 1 < spans.size() ? static_cast<int>(spans[band + 1].low) : bandCeilingHz(rate);
    return static_cast<float>(std::clamp(wholeHz(requested, span.high), left, right));
}

struct StageMetering
{
    float inputPeak = 0.0f;
    float gainDb = 0.0f;
    float outputPeak = 0.0f;
};

struct Metering
{
    std::array<StageMetering, stageCount> stages {};
    std::array<float, bandCount> bands {};
};

} // namespace shortbus::dsp
