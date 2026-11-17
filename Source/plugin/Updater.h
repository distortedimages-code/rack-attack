// Checks the project's GitHub releases and, on macOS, installs a newer build in place:
// the AU, VST3 and app found on this Mac are replaced by the ones in the release zip,
// after the zip's SHA-256 has been checked against the checksum published with it.
// Because the plug-in downloads the file itself (not a browser), macOS doesn't
// quarantine it, so there is no Gatekeeper step. The DAW must be restarted to load it.
// (The same mechanism as OXIde.)
//
// The repository is set at build time: cmake -DRA_UPDATE_REPO=owner/repo
// Only published, non-prerelease releases are seen. Only api.github.com and GitHub's
// download host are contacted.

#pragma once

#include <JuceHeader.h>

class Updater
{
public:
    struct Release
    {
        juce::String version, title, notes, pageUrl, zipUrl, shaUrl;
    };

    // what a version brings, in short: its headline and the titles of its points (from its release notes)
    struct News
    {
        juce::String version, headline;
        juce::StringArray points;
    };

    enum class State { Disabled, Idle, Checking, UpToDate, Available, Installing, Installed, Failed };

    explicit Updater (std::function<juce::PropertiesFile&()> settings);
    ~Updater();

    void checkIfDue();                 // automatic check, when the window opens: what the last check found at
                                       // once (kept in the settings), and GitHub asked again after an hour
    void checkNow();
    void install();                    // download, verify, replace (macOS); elsewhere opens the page
    void skipThisVersion();
    void openReleasePage() const;

    bool getAutoCheck() const;
    void setAutoCheck (bool);

    State getState() const             { return state; }
    juce::String getStatusText() const { return status; }
    const Release& getRelease() const  { return release; }
    const std::vector<Release>& getNewer() const   { return newer; }   // every version newer than this one, newest first
    std::vector<News> getNews (int maxVersions = 3) const;              // the same, in short

    juce::ChangeBroadcaster changes;

    static juce::String currentVersion();
    static juce::String repository();

    //==============================================================================
    // Pieces kept free of networking so they can be tested.
    static bool isNewer (const juce::String& candidate, const juce::String& current);
    static std::optional<Release> parseLatestRelease (const juce::String& json, const juce::String& assetName);
    static std::vector<Release> parseReleases (const juce::String& json, const juce::String& assetName);   // newest first
    static News summarise (const juce::String& version, const juce::String& notes);
    static juce::String pageOf (const juce::String& version);   // its release page
    static juce::String expectedSha (const juce::String& shaFileText);

    // Replaces each existing bundle in 'targets' with the bundle of the same name found in the
    // zip. 'extract' unpacks the zip into a folder (ditto on macOS). Each replacement is atomic
    // per bundle: the old one is put back if the new one can't be moved in.
    //
    // A folder the plug-in can't write in (/Library/Audio/Plug-Ins/... when installed from the
    // disk image): the new bundle is unpacked and checked in a temporary folder, then 'elevate'
    // runs one shell script that swaps all such bundles at once (macOS asks the password once).
    // Without 'elevate' those bundles are reported and left as they are. 'writable' tells
    // whether a folder can be written in (tests); by default, trying is enough.
    using Extractor = std::function<bool (const juce::File& zip, const juce::File& destination)>;
    using Elevator = std::function<bool (const juce::String& shellScript)>;
    static juce::Result installBundles (const juce::File& zip, const juce::Array<juce::File>& targets,
                                        const juce::String& expectedVersion, const Extractor& extract,
                                        juce::StringArray& installed, const Elevator& elevate = {},
                                        const std::function<bool (const juce::File&)>& writable = {});
    static juce::String shellQuote (const juce::String& s);   // 'like this', for /bin/sh
    static juce::Array<juce::File> installedBundles();         // where it is installed on this Mac

    static constexpr const char* productName = "RackAttack Editor";
    static constexpr const char* bundleId = "com.distortedimages.rackattackeditor";
    static constexpr const char* assetName = "RackAttack-Editor-mac.zip";   // what the updater downloads

private:
    void setState (State s, const juce::String& text);
    void found (const std::vector<Release>& all, bool automatic);   // a list of releases, from GitHub or the cache
    void keep (const std::vector<Release>& all);                    // the cache, in the settings
    static std::optional<Release> parseRelease (const juce::var&, const juce::String& assetName);

    std::function<juce::PropertiesFile&()> settings;
    State state = State::Idle;
    juce::String status;
    Release release;
    std::vector<Release> newer;
    bool automatic = false;

    std::atomic<bool> cancel { false };
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Updater)
    JUCE_DECLARE_NON_COPYABLE (Updater)
};
