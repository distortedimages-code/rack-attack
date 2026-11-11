#include "Updater.h"
#include "../app/Text.h"

#ifndef RA_UPDATE_REPO
 #define RA_UPDATE_REPO ""
#endif
#ifndef RA_VERSION
 #define RA_VERSION "0.0.0"
#endif

namespace
{
    juce::String normalise (juce::String v)
    {
        v = v.trim();
        if (v.startsWithIgnoreCase ("v"))
            v = v.substring (1);
        return v.upToFirstOccurrenceOf ("-", false, false).upToFirstOccurrenceOf ("+", false, false);
    }

    std::array<int, 3> parts (const juce::String& v)
    {
        std::array<int, 3> p { 0, 0, 0 };
        auto tokens = juce::StringArray::fromTokens (normalise (v), ".", "");
        for (int i = 0; i < 3 && i < tokens.size(); ++i)
            p[(size_t) i] = tokens[i].getIntValue();
        return p;
    }

    // GET into memory. Returns an error text, empty on success.
    juce::String httpGet (const juce::String& url, juce::MemoryBlock& out, const std::atomic<bool>& cancel, int timeoutMs)
    {
        int statusCode = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: RackAttack-Editor/" + Updater::currentVersion())
                           .withConnectionTimeoutMs (timeoutMs)
                           .withNumRedirectsToFollow (8)
                           .withStatusCode (&statusCode);
        auto in = juce::URL (url).createInputStream (options);
        if (in == nullptr)
            return "GitHub ne répond pas (pas de connexion ?)."_fr;
        if (statusCode == 404)
            return "Aucune version publiée."_fr;
        if (statusCode != 0 && (statusCode < 200 || statusCode >= 300))
            return "GitHub a répondu par l'erreur "_fr + juce::String (statusCode) + ".";

        juce::HeapBlock<char> buffer (65536);
        for (;;)
        {
            if (cancel)
                return "Annulé."_fr;
            const auto n = in->read (buffer.get(), 65536);
            if (n <= 0)
                break;
            out.append (buffer.get(), (size_t) n);
            if (out.getSize() > (size_t) 300 * 1024 * 1024)
                return "Le téléchargement est anormalement gros : arrêté."_fr;
        }
        return {};
    }

   #if JUCE_MAC
    bool dittoExtract (const juce::File& zip, const juce::File& destination)
    {
        juce::ChildProcess p;
        if (! p.start (juce::StringArray { "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), destination.getFullPathName() }))
            return false;
        return p.waitForProcessToFinish (120000) && p.getExitCode() == 0;
    }

    void removeQuarantine (const juce::File& bundle)
    {
        juce::ChildProcess p;
        if (p.start (juce::StringArray { "/usr/bin/xattr", "-dr", "com.apple.quarantine", bundle.getFullPathName() }))
            p.waitForProcessToFinish (20000);
    }

    // Runs a shell script as administrator: macOS shows its own password dialog (osascript, the
    // way installers without a helper do it). False if the password wasn't given.
    bool runAsAdministrator (const juce::String& shellScript)
    {
        const auto literal = [] (const juce::String& t) { return "\"" + t.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\""; };
        const auto apple = "do shell script " + literal (shellScript) + " with prompt "
                           + literal ("RackAttack Editor a besoin de ton mot de passe pour se mettre à jour dans le dossier des plug-ins."_fr)
                           + " with administrator privileges";
        juce::ChildProcess p;
        if (! p.start (juce::StringArray { "/usr/bin/osascript", "-e", apple }))
            return false;
        return p.waitForProcessToFinish (300000) && p.getExitCode() == 0;   // time to type the password
    }
   #endif
}

//==============================================================================
Updater::Updater (std::function<juce::PropertiesFile&()> s) : settings (std::move (s))
{
    if (repository().isEmpty())
        setState (State::Disabled, "Mises à jour non prévues dans cette version."_fr);
    else
        setState (State::Idle, "Version "_fr + currentVersion() + ".");
}

Updater::~Updater()
{
    cancel = true;
    pool.removeAllJobs (true, 8000);
}

juce::String Updater::currentVersion()  { return RA_VERSION; }
juce::String Updater::repository()      { return juce::String (RA_UPDATE_REPO).trim(); }

bool Updater::getAutoCheck() const      { return settings().getBoolValue ("updateAuto", true); }

void Updater::setAutoCheck (bool b)
{
    settings().setValue ("updateAuto", b);
    settings().saveIfNeeded();
    changes.sendChangeMessage();
}

void Updater::setState (State s, const juce::String& text)
{
    state = s;
    status = text;
    changes.sendChangeMessage();
}

void Updater::openReleasePage() const
{
    const auto page = release.pageUrl.isNotEmpty() ? release.pageUrl
                                                   : "https://github.com/" + repository() + "/releases/latest";
    juce::URL (page).launchInDefaultBrowser();
}

void Updater::skipThisVersion()
{
    settings().setValue ("updateSkip", release.version);
    settings().saveIfNeeded();
    setState (State::UpToDate, "Version "_fr + release.version + " ignorée. Tu as la "_fr + currentVersion() + ".");
}

//==============================================================================
void Updater::checkIfDue()
{
    if (state != State::Idle || ! getAutoCheck())
        return;
    const auto last = settings().getValue ("updateLastCheck", "0").getLargeIntValue();
    if (juce::Time::currentTimeMillis() - last < (juce::int64) 24 * 3600 * 1000)
        return;
    automatic = true;
    checkNow();
}

void Updater::checkNow()
{
    if (repository().isEmpty() || state == State::Checking || state == State::Installing)
        return;

    setState (State::Checking, "Recherche de mise à jour…"_fr);
    settings().setValue ("updateLastCheck", juce::String (juce::Time::currentTimeMillis()));
    settings().saveIfNeeded();

    const auto url = "https://api.github.com/repos/" + repository() + "/releases/latest";
    pool.addJob ([this, weak = juce::WeakReference<Updater> (this), url]
    {
        juce::MemoryBlock mb;
        const auto err = httpGet (url, mb, cancel, 10000);
        std::optional<Release> rel;
        if (err.isEmpty())
            rel = parseLatestRelease (mb.toString(), assetName);

        juce::MessageManager::callAsync ([weak, err, rel]
        {
            auto* u = weak.get();
            if (u == nullptr)
                return;
            const bool wasAutomatic = std::exchange (u->automatic, false);

            if (err.isNotEmpty() || ! rel)
            {
                u->setState (wasAutomatic ? State::Idle : State::Failed,
                             "Impossible de vérifier les mises à jour : "_fr + (err.isNotEmpty() ? err : "réponse inattendue de GitHub."_fr));
                return;
            }

            u->release = *rel;
            const bool skipped = wasAutomatic && u->settings().getValue ("updateSkip") == rel->version;
            if (isNewer (rel->version, currentVersion()) && ! skipped)
                u->setState (State::Available, "La version "_fr + rel->version + " est disponible (tu as la "_fr + currentVersion() + ").");
            else
                u->setState (State::UpToDate, "Tu as la dernière version ("_fr + currentVersion() + ").");
        });
    });
}

void Updater::install()
{
    if (state != State::Available)
        return;

   #if ! JUCE_MAC
    openReleasePage();
   #else
    setState (State::Installing, "Téléchargement de la version "_fr + release.version + "…"_fr);
    pool.addJob ([this, weak = juce::WeakReference<Updater> (this), rel = release]
    {
        juce::MemoryBlock zip, sha;
        juce::String err = httpGet (rel.zipUrl, zip, cancel, 30000);
        if (err.isEmpty())
        {
            if (rel.shaUrl.isEmpty())
                err = "Cette version n'a pas de fichier de contrôle : rien n'a été installé."_fr;
            else
                err = httpGet (rel.shaUrl, sha, cancel, 15000);
        }
        if (err.isEmpty())
        {
            const auto want = expectedSha (sha.toString());
            const auto got = juce::SHA256 (zip).toHexString();
            if (want.isEmpty() || got != want)
                err = "Le téléchargement est abîmé (contrôle faux). Rien n'a été installé."_fr;
        }

        juce::StringArray installed;
        if (err.isEmpty())
        {
            const auto tmp = juce::File::createTempFile (".zip");
            if (! tmp.replaceWithData (zip.getData(), zip.getSize()))
                err = "Impossible d'écrire le téléchargement sur le disque."_fr;
            else
            {
                const auto targets = installedBundles();
                if (targets.isEmpty())
                    err = "RackAttack Editor n'est pas dans les dossiers habituels : installe cette version à la main."_fr;
                else
                {
                    const auto r = installBundles (tmp, targets, rel.version, dittoExtract, installed, runAsAdministrator);
                    if (r.failed())
                        err = r.getErrorMessage();
                    for (const auto& path : installed)
                        removeQuarantine (juce::File (path));
                }
            }
            tmp.deleteFile();
        }

        juce::MessageManager::callAsync ([weak, err, installed, rel]
        {
            auto* u = weak.get();
            if (u == nullptr)
                return;
            if (! installed.isEmpty())
                u->setState (State::Installed, "Version "_fr + rel.version + " installée ("_fr + juce::String (installed.size())
                                               + " éléments). Quitte et rouvre ton logiciel (ou l'appli) pour l'utiliser."_fr
                                               + (err.isNotEmpty() ? "\n" + err : juce::String()));
            else
                u->setState (State::Failed, err.isNotEmpty() ? err : "Rien n'a été installé."_fr);
        });
    });
   #endif
}

//==============================================================================
bool Updater::isNewer (const juce::String& candidate, const juce::String& current)
{
    return parts (candidate) > parts (current);
}

std::optional<Updater::Release> Updater::parseLatestRelease (const juce::String& json, const juce::String& asset)
{
    const auto v = juce::JSON::parse (json);
    if (! v.isObject())
        return std::nullopt;
    if ((bool) v.getProperty ("draft", false) || (bool) v.getProperty ("prerelease", false))
        return std::nullopt;

    Release r;
    r.version = normalise (v.getProperty ("tag_name", {}).toString());
    r.title   = v.getProperty ("name", {}).toString();
    r.notes   = v.getProperty ("body", {}).toString().replace ("\r\n", "\n");
    r.pageUrl = v.getProperty ("html_url", {}).toString();
    if (r.version.isEmpty() || parts (r.version) == std::array<int, 3> { 0, 0, 0 })
        return std::nullopt;

    if (const auto* assets = v.getProperty ("assets", {}).getArray())
        for (const auto& a : *assets)
        {
            const auto name = a.getProperty ("name", {}).toString();
            const auto url = a.getProperty ("browser_download_url", {}).toString();
            if (name == asset)                 r.zipUrl = url;
            if (name == asset + ".sha256")     r.shaUrl = url;
        }

    if (! r.pageUrl.startsWith ("https://github.com/"))
        r.pageUrl = {};
    for (auto* u : { &r.zipUrl, &r.shaUrl })
        if (! u->startsWith ("https://"))
            u->clear();
    if (r.zipUrl.isEmpty())
        return std::nullopt;
    return r;
}

juce::String Updater::expectedSha (const juce::String& text)
{
    // "shasum -a 256" format: "<64 hex>  <file name>"
    const auto token = text.trim().upToFirstOccurrenceOf (" ", false, false).toLowerCase();
    return token.length() == 64 && token.containsOnly ("0123456789abcdef") ? token : juce::String();
}

juce::Array<juce::File> Updater::installedBundles()
{
    juce::Array<juce::File> r;
    auto add = [&r] (const juce::File& f) { if (f.isDirectory() && ! r.contains (f)) r.add (f); };

    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const juce::String n (productName);
    for (const auto& base : { home, juce::File ("/") })
    {
        add (base.getChildFile ("Library/Audio/Plug-Ins/Components/" + n + ".component"));
        add (base.getChildFile ("Library/Audio/Plug-Ins/VST3/" + n + ".vst3"));
    }
    add (home.getChildFile ("Applications/" + n + ".app"));
    add (juce::File ("/Applications/" + n + ".app"));

    // the running copy, wherever it is
    const auto self = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    if (self.hasFileExtension ("component;vst3;app") && self.getFileNameWithoutExtension() == productName)
        add (self);
    return r;
}

juce::String Updater::shellQuote (const juce::String& s)
{
    return "'" + s.replace ("'", "'\\''") + "'";
}

juce::Result Updater::installBundles (const juce::File& zip, const juce::Array<juce::File>& targets,
                                      const juce::String& expectedVersion, const Extractor& extract,
                                      juce::StringArray& installed, const Elevator& elevate,
                                      const std::function<bool (const juce::File&)>& writable)
{
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    juce::StringArray problems;

    // bundles waiting for the administrator step: the checked new bundle, where it goes, and the
    // temporary folder to clear afterwards
    struct Pending { juce::File fresh, dest, staging; };
    std::vector<Pending> pending;

    for (const auto& dest : targets)
    {
        const auto parent = dest.getParentDirectory();
        const auto name = dest.getFileName();

        // unpacked next to the bundle when the folder can be written in, otherwise in a
        // temporary folder for the administrator step
        bool direct = writable ? writable (parent) : true;
        auto staging = parent.getChildFile (".rackattack-update-" + stamp);
        if (direct)
        {
            staging.deleteRecursively();
            direct = staging.createDirectory().wasOk();
        }
        if (! direct)
        {
            if (! elevate)
            {
                problems.add (name + " : pas le droit d'écrire dans "_fr + parent.getFullPathName() + " (à installer à la main)."_fr);
                continue;
            }
            staging = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("rackattack-update-" + stamp).getChildFile (dest.getFileExtension().substring (1));
            staging.deleteRecursively();
            if (! staging.createDirectory())
            {
                problems.add (name + " : le téléchargement n'a pas pu être décompressé."_fr);
                continue;
            }
        }

        auto cleanup = [&staging] { staging.deleteRecursively(); };

        if (! extract (zip, staging))
        {
            problems.add (name + " : le téléchargement n'a pas pu être décompressé."_fr);
            cleanup();
            continue;
        }

        const auto fresh = staging.getChildFile (name);
        const auto plist = fresh.getChildFile ("Contents/Info.plist").loadFileAsString();
        if (! fresh.isDirectory() || ! plist.contains (bundleId) || ! plist.contains (normalise (expectedVersion)))
        {
            problems.add (name + " : absent de la version téléchargée, ou pas la bonne version. Laissé tel quel."_fr);
            cleanup();
            continue;
        }

        if (! direct)
        {
            pending.push_back ({ fresh, dest, staging });
            continue;
        }

        const bool hadDest = dest.exists();
        const auto old = parent.getChildFile ("." + name + ".old-" + stamp);
        if (hadDest && ! dest.moveFileTo (old))
        {
            problems.add (name + " : impossible de le remplacer (en cours d'utilisation ou protégé ?). Laissé tel quel."_fr);
            cleanup();
            continue;
        }
        if (! fresh.moveFileTo (dest))
        {
            if (hadDest)
                old.moveFileTo (dest);   // put the previous version back
            problems.add (name + " : impossible de le remplacer. L'ancienne version est gardée."_fr);
            cleanup();
            continue;
        }

        old.deleteRecursively();
        cleanup();
        installed.add (dest.getFullPathName());
    }

    // The administrator step: one script for every protected bundle, so the password is asked
    // once. Per bundle: the old one aside, the new one moved in, then the old one deleted (or put
    // back if the move failed), the quarantine flag cleared.
    if (! pending.empty())
    {
        juce::StringArray script;
        for (const auto& p : pending)
        {
            const auto q = [] (const juce::File& f) { return shellQuote (f.getFullPathName()); };
            const auto old = p.dest.getParentDirectory().getChildFile ("." + p.dest.getFileName() + ".old-" + stamp);
            script.add ("if [ -e " + q (p.dest) + " ]; then /bin/mv " + q (p.dest) + " " + q (old) + "; fi; "
                        "if [ ! -e " + q (p.dest) + " ] && /bin/mv " + q (p.fresh) + " " + q (p.dest) + "; then "
                        "/bin/rm -rf " + q (old) + "; /usr/bin/xattr -dr com.apple.quarantine " + q (p.dest) + " 2>/dev/null; "
                        "else [ ! -e " + q (old) + " ] || { /bin/rm -rf " + q (p.dest) + "; /bin/mv " + q (old) + " " + q (p.dest) + "; }; fi");
        }
        script.add ("true");
        const bool ran = elevate (script.joinIntoString ("; "));

        for (const auto& p : pending)
        {
            const auto plist = p.dest.getChildFile ("Contents/Info.plist").loadFileAsString();
            if (ran && plist.contains (bundleId) && plist.contains (normalise (expectedVersion)))
                installed.add (p.dest.getFullPathName());
            else if (! ran)
                problems.add (p.dest.getFileName() + " : mot de passe administrateur non donné. Laissé tel quel."_fr);
            else
                problems.add (p.dest.getFileName() + " : impossible à installer dans "_fr + p.dest.getParentDirectory().getFullPathName()
                              + ", même en administrateur. Laissé tel quel."_fr);
            p.staging.getParentDirectory().deleteRecursively();
        }
    }

    return problems.isEmpty() ? juce::Result::ok() : juce::Result::fail (problems.joinIntoString ("\n"));
}
