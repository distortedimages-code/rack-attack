#include "Library.h"
#include "Text.h"

namespace
{
    juce::String safeName (const juce::String& s)
    {
        auto t = s.trim().replaceCharacters ("/\\:*?\"<>|", "---------");
        return t.isEmpty() ? "sans nom"_fr : t;
    }

    ra::Bytes read (const juce::File& f)
    {
        juce::MemoryBlock data;
        if (! f.loadFileAsData (data))
            return {};
        auto* p = static_cast<const std::uint8_t*> (data.getData());
        return ra::Bytes (p, p + data.getSize());
    }

    juce::Array<juce::File> syxFiles (const juce::File& folder)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*.syx");
        files.sort();
        return files;
    }
}

Library::Library (juce::File rootFolder)
    : root (rootFolder == juce::File() ? juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RackAttack")
                                       : rootFolder),
      kitsFolder (root.getChildFile ("Kits")),
      soundsFolder (root.getChildFile ("Sons")),
      backupsFolder (root.getChildFile ("Sauvegardes")),
      morphsFolder (root.getChildFile ("Morphs"))
{
}

juce::Array<Library::Item> Library::kits() const
{
    juce::Array<Item> items;
    for (auto& f : syxFiles (kitsFolder))
        items.add ({ f, f.getFileNameWithoutExtension(), "Mes kits", false });

    auto backups = backupsFolder.findChildFiles (juce::File::findDirectories, false);
    backups.sort();
    for (int i = backups.size(); --i >= 0;)
        for (auto& f : syxFiles (backups[i]))
            items.add ({ f, f.getFileNameWithoutExtension(), "Sauvegarde "_fr + backups[i].getFileName(), false });
    return items;
}

juce::Array<Library::Item> Library::sounds() const
{
    juce::Array<Item> items;
    for (auto& f : syxFiles (soundsFolder))
        items.add ({ f, f.getFileNameWithoutExtension(), "Mes sons", true });
    return items;
}

juce::Array<Library::Item> Library::morphs() const
{
    juce::Array<Item> items;
    auto files = morphsFolder.findChildFiles (juce::File::findFiles, false, "*.ramorph");
    files.sort();
    for (auto& f : files)
        items.add ({ f, f.getFileNameWithoutExtension(), "Mes morphs", false, true });
    return items;
}

juce::File Library::saveMorph (const std::vector<std::uint8_t>& preset, const juce::String& name) const
{
    morphsFolder.createDirectory();
    auto file = morphsFolder.getNonexistentChildFile (safeName (name), ".ramorph", true);
    return file.replaceWithData (preset.data(), preset.size()) ? file : juce::File();
}

std::optional<std::vector<std::uint8_t>> Library::loadMorph (const juce::File& file)
{
    auto data = read (file);
    if (data.size() < 10)
        return std::nullopt;
    return std::vector<std::uint8_t> (data.begin(), data.end());
}

juce::File Library::saveKit (const ra::Kit& kit, const juce::String& name) const
{
    kitsFolder.createDirectory();
    auto file = kitsFolder.getNonexistentChildFile (safeName (name), ".syx", true);
    auto data = kit.toSyx();
    return file.replaceWithData (data.data(), data.size()) ? file : juce::File();
}

juce::File Library::saveSound (const ra::Bytes& soundData, const juce::String& name) const
{
    soundsFolder.createDirectory();
    auto file = soundsFolder.getNonexistentChildFile (safeName (name), ".syx", true);
    auto data = ra::Kit::soundToSyx (soundData);
    return file.replaceWithData (data.data(), data.size()) ? file : juce::File();
}

juce::File Library::newBackupFolder() const
{
    backupsFolder.createDirectory();
    return backupsFolder.getNonexistentChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %Hh%M"), {}, false);
}

std::optional<ra::Kit> Library::loadKit (const juce::File& file)
{
    return ra::Kit::fromSyx (read (file));
}

std::optional<ra::Bytes> Library::loadSound (const juce::File& file)
{
    return ra::Kit::soundFromSyx (read (file));
}
