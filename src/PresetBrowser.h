// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Processor.h"

namespace shortbus
{
class PresetSection final : public juce::Component, private juce::ListBoxModel
{
public:
    PresetSection(Processor&, PresetStore&, PresetScope);
    void setScope(PresetScope);
    void refresh();
    void resized() override;
    void paint(juce::Graphics&) override;
    std::function<void(const juce::String&)> onStatus;

private:
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
    void recall();
    void saveCurrent();
    void showSafes();
    Processor& processor;
    PresetStore& store;
    PresetScope scope;
    std::vector<PresetEntry> entries;
    juce::Label heading, empty;
    juce::ListBox list;
    juce::TextButton load { "Load" }, save { "Save" }, safes { "Safes" };
};

class PresetBrowser final : public juce::Component, private juce::Timer
{
public:
    explicit PresetBrowser(Processor&);
    void setScope(PresetScope scope) { unit.setScope(scope); }
    void resized() override;
    void paint(juce::Graphics&) override;
    void visibilityChanged() override;
    const PresetStore& presetStore() const { return store; }

private:
    void timerCallback() override;
    PresetStore store;
    PresetSection unit, bus;
    juce::TextButton folder { "Folder" };
    juce::Label status;
};
} // namespace shortbus
