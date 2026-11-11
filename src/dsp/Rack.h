// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Compressor.h"
#include "GraphicEq.h"

namespace shortbus::dsp
{
constexpr int maxDelay = 1536;

class Delay
{
public:
    void prepare(int delaySamples);
    Frame push(Frame next);

private:
    std::array<Frame, maxDelay> frames {};
    int size = 1, cursor = 0;
};

class Limiter
{
public:
    void prepare(float rate, int samples);
    void configure(LimiterSettings settings);
    Frame process(Frame input);
    float meter() const { return dbFromGain(gain) * mix; }

private:
    struct Peak { std::uint64_t frame; float value; };
    static constexpr std::size_t queueCapacity = maxDelay + 2;

    static std::size_t next(std::size_t position) { return (position + 1) % queueCapacity; }
    static std::size_t previous(std::size_t position) { return (position + queueCapacity - 1) % queueCapacity; }
    float trackPeak(float amplitude);

    Delay delay;
    std::array<Peak, queueCapacity> peaks {};
    std::size_t head = 0, tail = 0;
    std::uint64_t frame = 0;
    std::uint64_t delaySamples = 1;
    float sampleRate = 48000.0f;
    float ceiling = 1.0f, drive = 1.0f, driveTarget = 1.0f;
    float gain = 1.0f, mix = 0.0f, mixTarget = 0.0f;
    float attack = 1.0f, release = 1.0f, slew = 1.0f;
    bool configured = false;
};

class Rack
{
public:
    void prepare(double rate);
    void configure(const Settings&);
    Frame process(Frame input, int tap = 0, Frame* tapped = nullptr);
    Metering meters() const;
    int latency() const { return delaySamples; }
    bool isReordering() const { return fadeLeft > 0; }

private:
    struct Chain
    {
        void prepare(float rate, int delay);
        void configure(const Settings&);
        Frame process(Frame input, int tap = 0, Frame* tapped = nullptr);
        Metering meters() const;

        Order order = defaultOrder;
        float sampleRate = 48000.0f;
        Broadband lowLevel;
        Compressor compressor;
        std::array<Band, bandCount> bands;
        Limiter limiter;
        GraphicEq geq;
        std::array<StageMetering, stageCount> stageMeters {};
    };

    std::array<Chain, 2> chains;
    Delay dryDelay;
    int active = 0;
    int activeOrder = 0, pendingOrder = 0;
    int delaySamples = 1, warmup = 0, fadeLeft = 0, fadeLength = 1;
    float dryMix = 0.0f, dryTarget = 0.0f, slew = 1.0f;
    bool configured = false;
};

} // namespace shortbus::dsp
