#include "Session.h"

Session::Session()
{
    auto root = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("RackAttack").getChildFile ("Journal");
    auto name = "session-" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
    folder = root.getNonexistentChildFile (name, {}, false);
    dumps = folder.getChildFile ("recus");
    dumps.createDirectory();

    journal = std::make_unique<juce::FileOutputStream> (folder.getChildFile ("journal.txt"));
    if (journal->failedToOpen())
        journal.reset();
}

juce::String Session::timeStamp()
{
    auto t = juce::Time::getCurrentTime();
    return t.formatted ("%H:%M:%S") + "." + juce::String (t.getMilliseconds()).paddedLeft ('0', 3);
}

void Session::line (const juce::String& text)
{
    if (journal == nullptr)
        return;
    journal->writeText (timeStamp() + "  " + text + "\n", false, false, "\n");
    journal->flush();
}

void Session::saveDump (const ra::Bytes& sysex, const juce::String& name)
{
    ++dumpCount;
    auto file = dumps.getChildFile (juce::String (dumpCount).paddedLeft ('0', 4) + "-" + name + ".syx");
    file.replaceWithData (sysex.data(), sysex.size());
    folder.getChildFile ("tout-recu.syx").appendData (sysex.data(), sysex.size());
}

void Session::saveText (const juce::String& fileName, const juce::String& text)
{
    folder.getChildFile (fileName).replaceWithText (text);
}

juce::File Session::makeZip()
{
    if (journal != nullptr)
        journal->flush();

    juce::ZipFile::Builder builder;
    for (auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
    {
        auto file = entry.getFile();
        builder.addFile (file, 9, folder.getFileName() + "/" + file.getRelativePathFrom (folder).replaceCharacter ('\\', '/'));
    }

    auto zip = folder.getSiblingFile (folder.getFileName() + ".zip");
    zip.deleteFile();
    juce::FileOutputStream out (zip);
    if (out.failedToOpen() || ! builder.writeToStream (out, nullptr))
        return {};
    return zip;
}
