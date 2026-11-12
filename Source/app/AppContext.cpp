#include "AppContext.h"

#include "Measure.h"
#include "Text.h"

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

    const juce::File file (s.getValue ("measures"));
    if (s.getValue ("measures").isNotEmpty() && file.existsAsFile())
    {
        auto c = std::make_unique<ra::synth::Calibration>();
        juce::String error;
        if (measure::load (file, *c, error))
        {
            measured = std::move (c);
            measuresFile = file;
        }
    }
}

bool AppContext::useMeasures (const juce::File& file, juce::String& error)
{
    auto c = std::make_unique<ra::synth::Calibration>();
    if (! measure::load (file, *c, error))
        return false;
    measured = std::move (c);
    measuresFile = file;
    settings().setValue ("measures", file.getFullPathName());
    settings().saveIfNeeded();
    applyMeasures();
    engine.userNote ("Simulateur : " + juce::String (measured->source) + " (" + file.getFileName() + ")");
    return true;
}

void AppContext::forgetMeasures()
{
    measured.reset();
    measuresFile = juce::File();
    settings().removeValue ("measures");
    settings().saveIfNeeded();
    applyMeasures();
}

juce::String AppContext::measuresSource() const
{
    return measured != nullptr ? juce::String (measured->source) : "réglages du manuel"_fr;
}

void AppContext::applyMeasures()
{
    if (simOwner != nullptr)
        simOwner->setCalibration (measured != nullptr ? *measured : ra::synth::Calibration::defaults());
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

    demoFromEnvironment = juce::SystemStats::getEnvironmentVariable ("RA_DEMO", {}) == "1";
    if (demoFromEnvironment || settings().getBoolValue ("simulation", false))
    {
        enterSimulation();
        if (demoFromEnvironment)
        {
            loadDemoPattern();
            if (juce::SystemStats::getEnvironmentVariable ("RA_DEMO_STILL", {}) != "1")
                sequencer.play();
        }
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

void AppContext::loadDemoPattern()
{
    // a little pattern playing, to see the sequencer move
    sequencer.edit ([] (ra::seq::Pattern& p) { ra::seq::fillDemoPattern (p); });
}

void AppContext::enterSimulation()
{
    if (simOwner == nullptr)
    {
        simOwner = std::make_unique<SimDevice> (engine);
        simPtr = simOwner.get();
        applyMeasures();
    }
    engine.openInput ({});
    engine.openOutput ({});
    engine.pauseScale = 0.1;
    engine.setTimedOutput ([this] (const juce::MidiMessage& m, double atMs)
    {
        if (auto* sim = simPtr.load())
            sim->receive (m, atMs);
    });
    simActive = true;
}

void AppContext::leaveSimulation()
{
    simActive = false;
    engine.setTimedOutput (nullptr);
    engine.pauseScale = 1.0;
    // the real unit's settings (the simulation never saves its own)
    auto& s = settings();
    engine.deviceId = s.getIntValue ("id", 0);
    engine.channel = s.getIntValue ("channel", 1);
    engine.baseNote = s.getIntValue ("baseNote", 36);
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (d.identifier == inId) engine.openInput (inId);
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.identifier == outId) engine.openOutput (outId);
}

void AppContext::setSimulation (bool on)
{
    if (on == simActive.load())
        return;
    if (on)
        enterSimulation();
    else
        leaveSimulation();
    if (! demoFromEnvironment)
    {
        settings().setValue ("simulation", on);
        settings().saveIfNeeded();
    }
    if (on || settingsFile->getBoolValue ("setupDone", false))
        engine.connect();
}

AppContext::~AppContext()
{
    engine.stopPlayer();
    engine.cancel();
    engine.setTestOutput (nullptr);
    engine.setTimedOutput (nullptr);
    simPtr = nullptr;
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
    return settingsFile->getBoolValue ("setupDone", false) || simActive.load();
}

void AppContext::setSetUp (bool done)
{
    if (done)
        settings().setValue ("setupDone", true);
    saveSettings();
}

void AppContext::saveSettings()
{
    if (simActive.load())
        return;            // the pretend unit's ID and channel are not the real one's
    auto& s = settings();
    s.setValue ("in", inId);
    s.setValue ("out", outId);
    s.setValue ("id", engine.deviceId.load());
    s.setValue ("channel", engine.channel.load());
    s.setValue ("baseNote", engine.baseNote.load());
    s.saveIfNeeded();
}
