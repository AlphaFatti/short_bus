// SPDX-License-Identifier: AGPL-3.0-only
#include "Presets.h"
#include "Processor.h"

namespace shortbus
{
using namespace juce;

const char* presetScopeName(PresetScope scope)
{
    constexpr std::array names { "Expand-Lift", "5-Band", "Compressor", "Limiter", "RTA", "Bus", "GEQ" };
    return names[static_cast<std::size_t>(scope)];
}

bool presetIncludesParameter(PresetScope scope, const String& id)
{
    switch (scope)
    {
        case PresetScope::lowLevel: return id.startsWith("low_");
        case PresetScope::multiband: return id.startsWith("multi_") || id.startsWith("band");
        case PresetScope::compressor: return id.startsWith("comp_");
        case PresetScope::limiter: return id.startsWith("limit_");
        case PresetScope::rta: return id == param::rtaTarget || id == param::rtaView || id == param::rtaFloor;
        case PresetScope::bus: return true;
        case PresetScope::geq: return id.startsWith("geq_");
    }
    return false;
}

PresetStore::PresetStore(File overrideFolder)
{
    if (overrideFolder != File())
    {
        root = std::move(overrideFolder);
        return;
    }
    PropertiesFile::Options options;
    options.applicationName = "Short Bus";
    options.filenameSuffix = "settings";
    options.folderName = "TD Productions";
    options.osxLibrarySubFolder = "Application Support";
    PropertiesFile preferences(options);
    const auto saved = preferences.getValue("presetFolder");
    if (saved.isNotEmpty() && File::isAbsolutePath(saved))
    {
        root = File(saved);
        fallback = preferences.getBoolValue("userPresetFolder");
        return;
    }

    auto module = File::getSpecialLocation(File::currentApplicationFile);
    for (int level = 0; level < 6 && module != module.getParentDirectory(); ++level)
    {
        if (module.hasFileExtension("vst3"))
        {
            root = module.getParentDirectory().getChildFile("Short Bus Presets");
            break;
        }
        module = module.getParentDirectory();
    }
    if (root == File() || root.createDirectory().failed() || !root.hasWriteAccess())
    {
        root = preferences.getFile().getParentDirectory().getChildFile("Short Bus Presets");
        fallback = true;
    }
    preferences.setValue("presetFolder", root.getFullPathName());
    preferences.setValue("userPresetFolder", fallback);
    preferences.saveIfNeeded();
}

File PresetStore::scopeFolder(PresetScope scope) const
{
    return root.getChildFile(presetScopeName(scope));
}

std::vector<PresetEntry> PresetStore::list(PresetScope scope) const
{
    std::vector<PresetEntry> entries;
    for (const auto& file : scopeFolder(scope).findChildFiles(File::findFiles, false, "*.shortbus"))
        entries.push_back({ file, file.getFileNameWithoutExtension() });
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
    return entries;
}

Result PresetStore::save(PresetScope scope, const String& requestedName, const ValueTree& settings, File& saved)
{
    const auto name = File::createLegalFileName(requestedName.trim()).substring(0, 80);
    if (name.isEmpty() || name == "." || name == "..")
        return Result::fail("Enter a preset name.");
    const auto directory = scopeFolder(scope);
    if (const auto result = directory.createDirectory(); result.failed())
        return Result::fail("Cannot create the preset folder. " + result.getErrorMessage());

    InterProcessLock writer("TDProductionsShortBusPresets");
    if (!writer.enter(500))
        return Result::fail("Another Short Bus instance is saving. Try again.");
    const ScopeGuard unlock { [&writer] { writer.exit(); } };
    saved = directory.getNonexistentChildFile(name, ".shortbus", true);
    ValueTree preset { "ShortBusPreset" };
    preset.setProperty("formatVersion", 1, nullptr);
    preset.setProperty("scope", static_cast<int>(scope), nullptr);
    preset.addChild(settings.createCopy(), -1, nullptr);
    const auto xml = preset.createXml();
    TemporaryFile temporary(saved);
    if (xml == nullptr || !xml->writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary())
        return Result::fail("Could not save the preset. Check that the folder is writable.");
    return Result::ok();
}

Result PresetStore::load(const PresetEntry& entry, PresetScope scope, ValueTree& settings) const
{
    if (entry.file.getSize() > 1024 * 1024)
        return Result::fail("The preset file is too large.");
    const auto xml = XmlDocument::parse(entry.file);
    if (xml == nullptr || !xml->hasTagName("ShortBusPreset")
        || xml->getIntAttribute("formatVersion") != 1 || xml->getIntAttribute("scope", -1) != static_cast<int>(scope))
        return Result::fail("This file is not a compatible " + String(presetScopeName(scope)) + " preset.");
    const auto tree = ValueTree::fromXml(*xml);
    settings = tree.getChildWithName("ShortBusState");
    return settings.isValid() ? Result::ok() : Result::fail("The preset does not contain settings.");
}
} // namespace shortbus
