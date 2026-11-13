#include "Library.h"
#include "Text.h"

#include "../core/RaFactory.h"

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
      morphsFolder (root.getChildFile ("Morphs")),
      usineFolder (root.getChildFile ("Usine"))
{
}

juce::Array<Library::Item> Library::kits() const
{
    juce::Array<Item> items;
    for (auto& f : syxFiles (kitsFolder))
        items.add ({ f, f.getFileNameWithoutExtension(), "Mes kits", false });

    // Waldorf's: a group per set, the patterns last
    auto sets = usineFolder.findChildFiles (juce::File::findDirectories, false);
    sets.sort();
    for (auto& set : sets)
        if (set.getFileName() != "Patterns")
            for (auto& f : syxFiles (set))
                items.add ({ f, f.getFileNameWithoutExtension(), "Usine · "_fr + set.getFileName(), false });
    for (auto& f : syxFiles (usineFolder.getChildFile ("Patterns")))
    {
        Item item { f, f.getFileNameWithoutExtension(), "Usine · patterns seuls"_fr, false };
        item.isPatterns = true;
        items.add (item);
    }

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

Library::Imported Library::importWaldorf (const juce::File& file) const
{
    Imported result;
    if (file.hasFileExtension ("zip"))
    {
        juce::ZipFile zip (file);
        importZip (zip, file.getFileNameWithoutExtension(), result, 0);
    }
    else
        importData ({}, file.getFileNameWithoutExtension(), read (file), result);
    return result;
}

void Library::importZip (juce::ZipFile& zip, const juce::String& set, Imported& result, int depth) const
{
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        const auto name = e->filename.replaceCharacter ('\\', '/');
        const auto leaf = name.fromLastOccurrenceOf ("/", false, false);
        if (name.contains ("__MACOSX") || leaf.startsWith ("._") || e->isSymbolicLink || leaf.isEmpty())
            continue;
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        if (in == nullptr)
            continue;
        juce::MemoryBlock block;
        in->readIntoMemoryBlock (block, 64 * 1024 * 1024);
        const auto stem = leaf.upToLastOccurrenceOf (".", false, false);
        const auto ext = leaf.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (ext == "zip" && depth < 4)
        {
            // a zip inside: its own set (its name)
            juce::ZipFile inner (new juce::MemoryInputStream (block, true), true);
            importZip (inner, stem, result, depth + 1);
        }
        else if (ext == "mid" || ext == "midi" || ext == "syx")
        {
            auto* p = static_cast<const std::uint8_t*> (block.getData());
            importData (set, stem, ra::Bytes (p, p + block.getSize()), result);
        }
    }
}

void Library::importData (const juce::String& set, const juce::String& stem, const ra::Bytes& data, Imported& result) const
{
    const auto bank = ra::readBank (ra::isSmf (data) ? ra::smfSysex (data) : ra::splitSysex (data));
    if (bank.empty())
    {
        ++result.skipped;                // the system updates, and anything that is no kit
        return;
    }
    auto tidy = [] (juce::String n) { return safeName (n.replaceCharacter ('_', ' ').trim()); };
    for (size_t i = 0; i < bank.size(); ++i)
    {
        const auto& p = bank[i];
        if (! p.hasKit())
        {
            // internal patterns alone
            auto folder = usineFolder.getChildFile ("Patterns");
            folder.createDirectory();
            ra::Bytes out;
            for (int s = 0; s < ra::Kit::numSounds; ++s)
                if (p.kit.hasPattern (s))
                {
                    const auto m = p.kit.patternDump (0, s);
                    out.insert (out.end(), m.begin(), m.end());
                }
            const auto name = tidy (stem) + (bank.size() > 1 ? " " + juce::String ((int) i + 1) : juce::String());
            if (folder.getChildFile (name + ".syx").replaceWithData (out.data(), out.size()))
                ++result.patternSets;
            continue;
        }
        // a set of programs: its own folder; single kits: the folder of their zip, or "Kits"
        const auto setName = tidy (bank.size() > 1 ? stem : set.isNotEmpty() ? set : juce::String ("Kits"));
        auto folder = usineFolder.getChildFile (setName);
        folder.createDirectory();
        int number = p.number >= 0 ? p.number + 1 : (bank.size() > 1 ? (int) i + 1 : 0);
        if (number == 0 && stem.startsWithIgnoreCase ("Prog"))
            number = stem.substring (4).getIntValue();      // Waldorf's single programs: "Prog33_New_Style_Kit"

        auto title = juce::String (p.kit.name()).trim();
        if (title.isEmpty())
            title = tidy (stem);
        const auto name = (number > 0 ? "P" + juce::String (number).paddedLeft ('0', 2) + " " : juce::String()) + title;
        const auto bytes = p.kit.toSyx();
        if (folder.getChildFile (safeName (name) + ".syx").replaceWithData (bytes.data(), bytes.size()))
        {
            ++result.kits;
            result.sets.addIfNotAlreadyThere (setName);
        }
    }
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
