// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <map>

struct FreesoundSound
{
    juce::String id;          // Freesound's sound number, e.g. "415987"
    juce::String name, author, key, type;
    juce::String license;     // short label, e.g. "CC BY 4.0"
    juce::String licenseUrl, pageUrl, previewUrl;
    int bpm = 0;
    double duration = 0.0;

    // "Name" by author (page) - licence, for crediting the sound in a release.
    juce::String attribution() const;
};

// The official Freesound API (https://freesound.org/docs/api/). Search and previews need only an API key;
// downloading the original file needs OAuth2, i.e. the user's own Freesound account. API requests run on
// their own thread, at most one every requestGapMs (Freesound allows 60 a minute), and name freeloops in
// the User-Agent. Previews and downloads run on a second thread. Message thread only, except where noted.
class FreesoundClient
{
public:
    using FileCallback = std::function<void (juce::File, juce::String error)>;

    FreesoundClient();
    ~FreesoundClient();

    // Credentials (client id + API key) and the saved sign-in live in the shared settings file. Built-in
    // credentials can be compiled in (FREELOOPS_FREESOUND_CLIENT_ID / _API_KEY); the user's own win.
    void attachSettings (juce::PropertiesFile&);
    void setCredentials (const juce::String& clientId, const juce::String& apiKey);
    bool hasApiKey() const { return apiKey.isNotEmpty(); }
    bool hasClientId() const { return clientId.isNotEmpty(); }
    bool usesBuiltInCredentials() const;
    juce::String getClientId() const { return clientId; }

    // Callback (message thread): { sounds: [...], page, hasMore } or { error }.
    // filters: { bpm: [[min, max], ...], key: "Am" }, applied by Freesound.
    void search (const juce::String& query, int page, const juce::var& filters, std::function<void (juce::var)> callback);

    const FreesoundSound* find (const juce::String& id) const;

    // The HQ Ogg preview, fetched into a size-capped cache. No account needed.
    void fetchPreview (const juce::String& id, FileCallback);

    // ---- OAuth2 ----
    juce::String authorizeUrl (const juce::String& state) const;
    void signIn (const juce::String& code, std::function<void (juce::String error)>); // exchanges the code
    void signOut();
    bool isSignedIn() const;
    juce::String getUsername() const { return username; }

    // The original file, with the user's account, into downloadsFolder(). Also adds a line to CREDITS.txt there.
    void download (const juce::String& id, FileCallback);

    // Sounds the user kept: Music\freeloops\Freesound. FL projects point at these.
    static juce::File downloadsFolder();
    static juce::File previewCache();

    juce::File findDownload (const juce::String& id) const { return findIn (downloadsFolder(), id); }
    juce::File findPreview (const juce::String& id) const { return findIn (previewCache(), id); }

    static juce::var toVar (const FreesoundSound&, const juce::File& downloaded);

    // Any thread: parses one page of search results. Public for testing.
    static std::vector<FreesoundSound> parseResults (const juce::var& json, bool& hasMore);
    static juce::String licenseLabel (const juce::String& url);
    static juce::String keyLabel (const juce::String& tonality); // "C# minor" -> "C#m"
    static juce::String filterQuery (const juce::var& filters);

    std::function<void()> onAccountChange; // message thread

private:
    struct Response
    {
        int status = 0;
        juce::String text, error;
    };

    static juce::File findIn (const juce::File& folder, const juce::String& id);
    juce::File pathFor (const FreesoundSound&, const juce::String& extension) const;

    // Request threads.
    void throttle();
    Response request (const juce::URL&, const juce::String& authHeader, bool post = false);
    juce::String bearerToken (juce::String& error); // refreshes the access token when it has run out
    bool storeTokens (const juce::var& json);
    void saveAccount();

    juce::PropertiesFile* settings = nullptr;
    juce::String clientId, apiKey, username;

    juce::CriticalSection tokenLock;  // tokens are used on the request threads
    juce::String accessToken, refreshToken;
    double accessExpiresMs = 0.0;

    std::map<juce::String, FreesoundSound> known;
    std::map<juce::String, juce::var> searchCache;
    std::map<juce::String, std::vector<FileCallback>> fetching; // previews and downloads in flight, by "p:"/"d:" + id

    juce::CriticalSection throttleLock;
    double lastRequestMs = 0.0;

    juce::ThreadPool api   { juce::ThreadPoolOptions{}.withThreadName ("freeloops-freesound").withNumberOfThreads (1) };
    juce::ThreadPool files { juce::ThreadPoolOptions{}.withThreadName ("freeloops-freesound-files").withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (FreesoundClient)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreesoundClient)
};
