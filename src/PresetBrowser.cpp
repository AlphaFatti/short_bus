// SPDX-License-Identifier: AGPL-3.0-only
#include "PresetBrowser.h"

namespace shortbus
{
using namespace juce;
namespace
{
const auto glass = Colour(0xff262e34);
const auto white = Colour(0xfffff5e3);
const auto muted = Colour(0xffb3c0cb);
const auto yellow = Colour(0xffffd475);

void stylePresetButton(TextButton& button)
{
    button.setColour(TextButton::buttonColourId, Colour(0xff343c42));
    button.setColour(TextButton::textColourOffId, white);
}
}

PresetSection::PresetSection(Processor& owner, PresetStore& files, PresetScope initial)
    : processor(owner), store(files), scope(initial)
{
    heading.setColour(Label::textColourId, white);
    heading.setFont(Font(FontOptions(14.0f, Font::bold)));
    addAndMakeVisible(heading);
    list.setModel(this);
    list.setRowHeight(24);
    list.setColour(ListBox::backgroundColourId, glass);
    list.setColour(ListBox::outlineColourId, Colour(0xff667178));
    list.setOutlineThickness(1);
    list.setMultipleSelectionEnabled(false);
    list.setTitle("Saved presets");
    addAndMakeVisible(list);
    empty.setText("Save the current setup", dontSendNotification);
    empty.setColour(Label::textColourId, muted);
    empty.setFont(Font(FontOptions(11.0f)));
    empty.setJustificationType(Justification::centred);
    empty.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(empty);
    for (auto* button : { &load, &save, &safes })
    {
        stylePresetButton(*button);
        addAndMakeVisible(*button);
    }
    load.onClick = [this] { recall(); };
    save.onClick = [this] { saveCurrent(); };
    safes.onClick = [this] { showSafes(); };
    safes.setTooltip("Checked sections keep their current settings when a Bus preset is loaded.");
    setScope(initial);
}

void PresetSection::setScope(PresetScope next)
{
    scope = next;
    heading.setText(String(presetScopeName(scope)) + " Presets", dontSendNotification);
    safes.setVisible(scope == PresetScope::bus);
    list.deselectAllRows();
    entries.clear();
    load.setEnabled(false);
    empty.setVisible(true);
    refresh();
    resized();
}

void PresetSection::refresh()
{
    const auto fresh = store.list(scope);
    const auto selected = list.getSelectedRow();
    const auto chosen = isPositiveAndBelow(selected, static_cast<int>(entries.size())) ? entries[static_cast<std::size_t>(selected)].file : File();
    if (fresh.size() == entries.size() && std::equal(fresh.begin(), fresh.end(), entries.begin(),
                                                  [](const auto& a, const auto& b) { return a.file == b.file; }))
        return;
    entries = fresh;
    empty.setVisible(entries.empty());
    list.updateContent();
    list.deselectAllRows();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].file == chosen)
            list.selectRow(static_cast<int>(i));
    selectedRowsChanged(list.getSelectedRow());
    repaint();
}

void PresetSection::resized()
{
    auto bounds = getLocalBounds().reduced(7, 5);
    heading.setBounds(bounds.removeFromTop(23));
    auto buttons = bounds.removeFromBottom(23);
    if (scope == PresetScope::bus)
        safes.setBounds(buttons.removeFromRight(52).reduced(1, 0));
    save.setBounds(buttons.removeFromRight(buttons.getWidth() / 2).reduced(1, 0));
    load.setBounds(buttons.reduced(1, 0));
    list.setBounds(bounds.withTrimmedBottom(5));
    empty.setBounds(list.getBounds().reduced(6));
}

void PresetSection::paint(Graphics& g)
{
    g.setColour(glass);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
    g.setColour(Colour(0xff667178));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
}

int PresetSection::getNumRows() { return static_cast<int>(entries.size()); }

void PresetSection::paintListBoxItem(int row, Graphics& g, int width, int height, bool selected)
{
    if (!isPositiveAndBelow(row, static_cast<int>(entries.size())))
        return;
    if (selected)
    {
        g.setColour(yellow);
        g.fillRoundedRectangle(Rectangle<float>(static_cast<float>(width), static_cast<float>(height)).reduced(2.0f), 2.0f);
    }
    g.setColour(selected ? Colour(0xff171715) : white);
    g.setFont(getLookAndFeel().getLabelFont(heading).withHeight(12.0f));
    g.drawFittedText(entries[static_cast<std::size_t>(row)].name, 7, 0, width - 14, height,
                    Justification::centredLeft, 1, 0.85f);
}

void PresetSection::selectedRowsChanged(int row)
{
    load.setEnabled(isPositiveAndBelow(row, static_cast<int>(entries.size())));
}

void PresetSection::listBoxItemDoubleClicked(int row, const MouseEvent&)
{
    list.selectRow(row);
    recall();
}

void PresetSection::recall()
{
    const auto row = list.getSelectedRow();
    if (!isPositiveAndBelow(row, static_cast<int>(entries.size())))
        return;
    const auto& entry = entries[static_cast<std::size_t>(row)];
    ValueTree settings;
    auto result = store.load(entry, scope, settings);
    if (result.wasOk())
        result = processor.applyPreset(settings, scope);
    if (onStatus)
        onStatus(result.wasOk() ? "Loaded " + entry.name : result.getErrorMessage());
}

void PresetSection::saveCurrent()
{
    const auto settings = processor.presetState(scope);
    const auto savedScope = scope;
    auto* dialog = new AlertWindow("Save " + String(presetScopeName(scope)) + " preset", "Enter a name.",
                                    MessageBoxIconType::NoIcon, getTopLevelComponent());
    dialog->addTextEditor("name", "", "Preset name");
    dialog->addButton("Save", 1, KeyPress(KeyPress::returnKey));
    dialog->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));
    const Component::SafePointer<PresetSection> safe(this);
    dialog->enterModalState(true, ModalCallbackFunction::create([safe, dialog, settings, savedScope](int result)
    {
        if (result != 1 || safe == nullptr)
            return;
        File saved;
        const auto status = safe->store.save(savedScope, dialog->getTextEditorContents("name"), settings, saved);
        if (safe->onStatus)
            safe->onStatus(status.wasOk() ? "Saved " + saved.getFileNameWithoutExtension() : status.getErrorMessage());
        safe->refresh();
    }), true);
}

void PresetSection::showSafes()
{
    constexpr std::array names { "Expand / Lift", "5-Band", "Compressor", "Limiter",
                                 "Order + RTA position", "RTA target + reference", "Bypass" };
    PopupMenu menu;
    menu.addSectionHeader("Keep current settings on Bus recall");
    const auto mask = processor.presetRecallSafes();
    for (std::size_t i = 0; i < names.size(); ++i)
        menu.addItem(static_cast<int>(i + 1), names[i], true, (mask & (1u << i)) != 0);
    const Component::SafePointer<PresetSection> safe(this);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&safes), [safe](int result)
    {
        if (safe != nullptr && result > 0 && result <= presetSafeCount)
            safe->processor.setPresetRecallSafes(safe->processor.presetRecallSafes() ^ (1u << (result - 1)));
    });
}

PresetBrowser::PresetBrowser(Processor& owner)
    : unit(owner, store, PresetScope::lowLevel), bus(owner, store, PresetScope::bus)
{
    addAndMakeVisible(unit);
    addAndMakeVisible(bus);
    addAndMakeVisible(folder);
    addAndMakeVisible(status);
    stylePresetButton(folder);
    const auto path = store.folder().getFullPathName();
    folder.setTooltip((store.usesUserFolder() ? "User preset folder. " : "Preset folder beside the VST3. ") + path);
    folder.onClick = [this] { store.folder().createDirectory(); store.folder().revealToUser(); };
    status.setColour(Label::textColourId, muted);
    status.setFont(Font(FontOptions(10.0f)));
    status.setText("Double-click a preset to load", dontSendNotification);
    status.setTooltip(path);
    const auto report = [this](const String& message)
    {
        status.setText(message, dontSendNotification);
        status.setTooltip(message + "\n" + store.folder().getFullPathName());
    };
    unit.onStatus = report;
    bus.onStatus = report;
}

void PresetBrowser::resized()
{
    auto bounds = getLocalBounds().reduced(12, 7);
    auto footer = bounds.removeFromBottom(24);
    folder.setBounds(footer.removeFromLeft(51));
    status.setBounds(footer.withTrimmedLeft(3));
    bounds.removeFromBottom(5);
    unit.setBounds(bounds.removeFromTop((bounds.getHeight() - 8) / 2));
    bounds.removeFromTop(8);
    bus.setBounds(bounds);
}

void PresetBrowser::paint(Graphics& g)
{
    g.setColour(Colour(0xff171715));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f);
    g.setColour(Colour(0xffe8a317));
    g.fillRect(0, 0, 5, getHeight());
    for (int i = 1; i <= 4; ++i)
    {
        g.setColour(Colour(0xff414649));
        g.fillRoundedRectangle(0.0f, static_cast<float>(i * getHeight() / 5 - 9), 9.0f, 18.0f, 2.0f);
    }
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        timerCallback();
        startTimer(1500);
    }
    else
        stopTimer();
}

void PresetBrowser::timerCallback()
{
    unit.refresh();
    bus.refresh();
}
} // namespace shortbus
