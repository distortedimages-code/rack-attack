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
