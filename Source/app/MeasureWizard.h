// "Mesurer le RackAttack": the guided measurement, over the window.
// 1. what it is (or a try on the simulator), 2. the audio input with a level meter
// and a test sound, 3. speakers off and hands off, 4. the measurement itself, with
// the ghost measuring everyday objects on a conveyor belt and saying silly things,
// 5. the file, to send to whoever wants their simulator to sound like this unit.

#pragma once

#include <JuceHeader.h>

#include <set>

#include "Engine.h"
#include "Measure.h"
#include "Widgets.h"

// The ghost at work, in black and white pixels.
class GhostShow : public juce::Component,
                  private juce::Timer
{
public:
    GhostShow();
    void setRunning (bool on);
    void setPaused (const juce::String& why);   // empty: not paused
    void setFinished (bool ok);
    void setIdle (const juce::String& says);    // floats about, says this (the first screen)
    void reset();
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void nextPhrase();
    juce::Rectangle<int> objectBox (int index, int p, int ghostX) const;

    double t = 0.0, belt = 0.0, inspectT = 0.0, phraseT = 0.0;
    int inspecting = -1;
    std::set<int> measured;
    bool running = false, finished = false, finishedOk = false;
    juce::String pausedWhy, idleSays;
    juce::StringArray phrases;
    std::vector<int> order;
    int phrase = 0;
    juce::Random random;
};

class MeasureWizard : public juce::Component,
                      private Engine::Listener,
                      private juce::Timer
{
public:
    explicit MeasureWizard (Engine&);
    ~MeasureWizard() override;

    std::function<void()> onClose;
    std::function<bool (const juce::File&, juce::String&)> useMeasures;   // into this app's simulator
    std::function<bool()> isSimulation;
    std::function<void()> stopSequencer;
    std::function<juce::PropertiesFile*()> settings;

    void start();                 // back to the first screen
    void demo (const juce::String& what);   // screenshots: "audio", "ready", "run", "done"
    bool isBusy() const;          // measuring or playing the test sound

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}   // the window behind stays untouchable

private:
    enum class Screen { intro, audio, ready, running, done };

    struct Worker : public juce::Thread
    {
        Worker() : juce::Thread ("Mesure du RackAttack") {}
        std::function<void()> job;
        void run() override   { if (job) job(); }
    };

    void taskFinished (const juce::String& task, bool ok, const juce::String& message) override;
    void connectionChanged (Engine::Connection) override   { if (screen == Screen::intro) show (Screen::intro); }
    void timerCallback() override;

    void show (Screen);
    void openAudio();
    void closeAudio();
    void playTone();
    void toneDone();
    void startMeasure();
    void finished (const measure::Result&, const juce::File&);
    void close();
    void withKit (std::function<void()> then);
    void restoreSound();
    measure::Rig* rig();
    int freeChannel() const;
    void updateButtons();
    float level (int channel);
    void drawMeters (juce::Graphics&, juce::Rectangle<int>, bool big);

    Engine& engine;
    Screen screen = Screen::intro;
    bool trial = false;
    juce::Rectangle<int> card, meterArea, bannerArea, progressArea, smallMeterArea;

    std::unique_ptr<juce::AudioDeviceManager> devices;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    measure::Recorder recorder;
    std::unique_ptr<measure::LiveRig> live;
    std::unique_ptr<measure::SynthRig> sim;
    Worker worker;

    std::atomic<bool> toneRunning { false };
    std::array<float, 2> toneMax {}, shown {}, hold {};
    std::array<double, 2> holdUntil {};
    enum class Signal { none, listening, ok, tooLoud, nothing } signal = Signal::none;
    std::function<void()> afterKit;

    std::atomic<int> stepsDone { 0 };
    juce::CriticalSection whatLock;
    juce::String whatNow;
    juce::File resultFile;
    bool resultOk = false, resultStopped = false;
    int lastPaused = -2;

    juce::Label stepLabel, title, body, details, status;
    juce::TextButton later { "Plus tard" }, back { "Retour" }, next { "Suivant" };
    juce::TextButton measureReal { "Mesurer mon RackAttack" }, tryIt { "Essai sans machine (en accéléré)"_fr };
    juce::TextButton testTone { "Jouer un son test" };
    juce::ToggleButton speakersOff { "J'ai coupé le son de mes enceintes et de mon casque"_fr };
    juce::ToggleButton handsOff { "Je ne touche plus au RackAttack pendant la mesure" };
    juce::TextButton go { "Démarrer la mesure"_fr };
    juce::TextButton pauseButton { "Pause" }, stopButton { "Arrêter"_fr };
    juce::TextButton showFile { "Montrer le fichier" }, useHere { "Utiliser ces mesures dans mon simulateur" };
    juce::TextButton again { "Recommencer" };
    GhostShow ghost;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeasureWizard)
};
