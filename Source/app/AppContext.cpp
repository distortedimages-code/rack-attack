#include "AppContext.h"

namespace
{
    std::unique_ptr<juce::PropertiesFile> makeSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "RackAttack Editor";
        options.folderName = "RackAttack";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.processLock = nullptr;
        return std::make_unique<juce::PropertiesFile> (options);
    }
}

AppContext::AppContext() : settingsFile (makeSettings())
{
    auto& s = settings();
    engine.deviceId = s.getIntValue ("id", 0);
    engine.channel = s.getIntValue ("channel", 1);
    engine.baseNote = s.getIntValue ("baseNote", 36);
    inId = s.getValue ("in");
    outId = s.getValue ("out");
}

void AppContext::start()
{
    if (std::exchange (started, true))
        return;

    engine.userNote ("RackAttack Editor " + Updater::currentVersion() + " ("
                     + juce::PluginHostType().getHostDescription() + ", " + juce::SystemStats::getOperatingSystemName() + ")");

    auto connectLater = [weak = juce::WeakReference<AppContext> (this)]
    {
        juce::MessageManager::callAsync ([weak] { if (auto* c = weak.get()) c->engine.connect(); });
    };

    if (juce::SystemStats::getEnvironmentVariable ("RA_DEMO", {}) == "1")
    {
        demo = std::make_unique<FakeRackAttack>();
        demo->engine = &engine;
        engine.pauseScale = 0.1;
        engine.setTestOutput ([this] (const juce::MidiMessage& m) { demo->receive (m); });
        if (juce::SystemStats::getEnvironmentVariable ("RA_WIZARD", {}) != "1")
            connectLater();

        // a little pattern playing, to see the sequencer move
        sequencer.edit ([] (ra::seq::Pattern& p)
        {
            p.tempo = 124.0;
            const char* names[] = { "Kick", "Kick 2", "Snare", "Clap", "Rim", "Hat fermé", "Hat ouvert", "Crash" };
            for (int i = 0; i < 8; ++i)
                p.tracks[(size_t) i].name = names[i];
            p.tracks[20].name = "Basse";
            for (int i : { 0, 4, 8, 12 })
                p.tracks[0].steps[(size_t) i].on = true;
            for (int i : { 4, 12, 15 })
                p.tracks[2].steps[(size_t) i].on = true;
            p.tracks[2].length = 64;
            p.tracks[2].steps[15].velocity = 40;
            p.tracks[2].steps[12].setLock (0x27, 98);
            p.tracks[2].steps[12].setLock (0x29, 70);
            p.tracks[2].steps[15].condA = 1;
            p.tracks[2].steps[15].condB = 2;
            p.tracks[2].steps[7].on = true;
            p.tracks[2].steps[7].retrig = 3;
            p.tracks[3].steps[12].on = true;
            for (int i = 0; i < 16; i += 2)
            {
                p.tracks[5].steps[(size_t) i].on = true;
                p.tracks[5].steps[(size_t) i].velocity = (std::uint8_t) (i % 4 == 0 ? 110 : 50);
            }
            p.tracks[5].steps[6].setLock (0x40, 96);
            p.tracks[6].steps[6].on = p.tracks[6].steps[14].on = true;
            p.tracks[13].length = 12;
            for (int i : { 2, 5, 9 })
                p.tracks[13].steps[(size_t) i].on = true;
            p.tracks[16].mute = true;
            p.tracks[16].steps[15].on = true;
            p.tracks[20].mode = ra::seq::Mode::notes;
            const int bass[][2] = { { 0, 36 }, { 3, 36 }, { 6, 43 }, { 10, 46 }, { 13, 48 } };
            for (auto& b : bass)
            {
                p.tracks[20].steps[(size_t) b[0]].on = true;
                p.tracks[20].steps[(size_t) b[0]].note = (std::uint8_t) b[1];
            }
        });
        if (juce::SystemStats::getEnvironmentVariable ("RA_DEMO_STILL", {}) != "1")
            sequencer.play();
        return;
    }

    // the ports of last time, then find the unit if the assistant was done once
    bool inOk = false, outOk = false;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (d.identifier == inId) inOk = engine.openInput (inId).isEmpty();
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.identifier == outId) outOk = engine.openOutput (outId).isEmpty();

    if (isSetUp() && inOk && outOk)
        connectLater();
}

AppContext::~AppContext()
{
    engine.stopPlayer();
    engine.cancel();
    engine.setTestOutput (nullptr);
}

void AppContext::chooseInput (const juce::String& identifier)
{
    inId = identifier;
    engine.openInput (identifier);
    saveSettings();
}

void AppContext::chooseOutput (const juce::String& identifier)
{
    outId = identifier;
    engine.openOutput (identifier);
    saveSettings();
}

bool AppContext::isSetUp() const
{
    return settingsFile->getBoolValue ("setupDone", false) || demo != nullptr;
}

void AppContext::setSetUp (bool done)
{
    if (done)
        settings().setValue ("setupDone", true);
    saveSettings();
}

void AppContext::saveSettings()
{
    if (demo != nullptr)
        return;
    auto& s = settings();
    s.setValue ("in", inId);
    s.setValue ("out", outId);
    s.setValue ("id", engine.deviceId.load());
    s.setValue ("channel", engine.channel.load());
    s.setValue ("baseNote", engine.baseNote.load());
    s.saveIfNeeded();
}
