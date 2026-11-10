// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace shortbus
{
enum class PresetScope { lowLevel, multiband, compressor, limiter, rta, bus };
constexpr int presetSafeCount = 7;
constexpr unsigned defaultPresetSafes = 1u << 6;

const char* presetScopeName(PresetScope);
bool presetIncludesParameter(PresetScope, const juce::String& id);

struct PresetEntry
{
    juce::File file;
    juce::String name;
};

class PresetStore
{
public:
    explicit PresetStore(juce::File folder = {});
    const juce::File& folder() const { return root; }
    bool usesUserFolder() const { return fallback; }
    std::vector<PresetEntry> list(PresetScope) const;
    juce::Result save(PresetScope, const juce::String& name, const juce::ValueTree&, juce::File& saved);
    juce::Result load(const PresetEntry&, PresetScope, juce::ValueTree&) const;

private:
    juce::File scopeFolder(PresetScope) const;
    juce::File root;
    bool fallback = false;
};
} // namespace shortbus
