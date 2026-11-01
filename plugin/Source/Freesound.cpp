// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Freesound.h"

// Optional credentials compiled in (CMake: -DFREELOOPS_FREESOUND_CLIENT_ID=... -DFREELOOPS_FREESOUND_API_KEY=...).
#define FREELOOPS_STRINGIFY_(x) #x
#define FREELOOPS_STRINGIFY(x) FREELOOPS_STRINGIFY_(x)
#ifdef FREELOOPS_FREESOUND_CLIENT_ID
 static const juce::String builtInClientId { FREELOOPS_STRINGIFY (FREELOOPS_FREESOUND_CLIENT_ID) };
#else
 static const juce::String builtInClientId;
#endif
#ifdef FREELOOPS_FREESOUND_API_KEY
 static const juce::String builtInApiKey { FREELOOPS_STRINGIFY (FREELOOPS_FREESOUND_API_KEY) };
#else
 static const juce::String builtInApiKey;
#endif

namespace
{
    constexpr auto apiBase = "https://freesound.org/apiv2";
    constexpr auto userAgent = "freeloops/0.1 (open-source FL Studio sample browser)";
    constexpr double requestGapMs = 1100.0;      // Freesound allows 60 requests a minute per key
    constexpr int pageSize = 30;
    constexpr juce::int64 previewCacheBytes = 300LL * 1024 * 1024;
    constexpr auto searchFields = "id,name,username,license,duration,previews,bpm,tonality,type,url";

    const juce::StringArray audioExtensions { "wav", "wave", "aif", "aiff", "flac", "mp3", "ogg", "m4a", "wv" };

    // Freesound names often end in the uploaded file's extension ("Piano Loop 120.wav").
    juce::String stripAudioExtension (const juce::String& name)
    {
        const auto ext = name.fromLastOccurrenceOf (".", false, false);
        return name.containsChar ('.') && audioExtensions.contains (ext, true) ? name.upToLastOccurrenceOf (".", false, false).trim()
                                                                                : name.trim();
    }

    juce::String extensionForType (const juce::String& type)
    {
        const auto t = type.toLowerCase();
        if (t == "aiff" || t == "aif") return ".aif";
        if (t == "wave")               return ".wav";
        return audioExtensions.contains (t) ? "." + t : juce::String();
    }

    juce::String sniffExtension (const juce::File& file)
    {
        char b[4] {};
        if (juce::FileInputStream in (file); in.openedOk())
            in.read (b, 4);

        const auto starts = [&] (const char* magic) { return std::memcmp (b, magic, std::strlen (magic)) == 0; };
        if (starts ("RIFF")) return ".wav";
        if (starts ("FORM")) return ".aif";
        if (starts ("fLaC")) return ".flac";
        if (starts ("OggS")) return ".ogg";
        if (starts ("ID3") || ((unsigned char) b[0] == 0xff && ((unsigned char) b[1] & 0xe0) == 0xe0)) return ".mp3";
        return {};
    }

    juce::var errorResult (const juce::String& message)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("error", message);
        return juce::var (o);
    }

    juce::String httpError (int status)
    {
        if (status == 401) return "Freesound rejected the API key";
        if (status == 429) return "Freesound's request limit was reached. Try again in a minute";
        return status > 0 ? "Freesound returned HTTP " + juce::String (status) : "Couldn't reach Freesound";
    }

    // Both spellings of a key: Freesound's analysis may name it either way.
    juce::StringArray tonalityNames (const juce::String& label)
    {
        static const juce::StringArray sharps { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const juce::StringArray flats  { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

        const auto minor = label.endsWith ("m");
        const auto note = minor ? label.dropLastCharacters (1) : label;
        auto index = sharps.indexOf (note);
        if (index < 0)
            index = flats.indexOf (note);
        if (index < 0)
            return {};

        const juce::String quality = minor ? " minor" : " major";
        juce::StringArray names { sharps[index] + quality };
        names.addIfNotAlreadyThere (flats[index] + quality);
        return names;
    }
}

//==============================================================================
juce::String FreesoundSound::attribution() const
{
    return "\"" + name + "\" by " + author + " (" + pageUrl + "), licensed under " + license
           + (licenseUrl.startsWith ("http") ? " (" + licenseUrl + ")" : juce::String());
}

//==============================================================================
FreesoundClient::FreesoundClient() = default;

FreesoundClient::~FreesoundClient()
{
    api.removeAllJobs (true, 5000);
    files.removeAllJobs (true, 5000);
}

void FreesoundClient::attachSettings (juce::PropertiesFile& s)
{
    settings = &s;
    clientId = s.getValue ("freesound.clientId", builtInClientId);
    apiKey = s.getValue ("freesound.apiKey", builtInApiKey);
    username = s.getValue ("freesound.username");

    const juce::ScopedLock sl (tokenLock);
    refreshToken = s.getValue ("freesound.refreshToken");
}

void FreesoundClient::setCredentials (const juce::String& newClientId, const juce::String& newApiKey)
{
    clientId = newClientId.trim().isEmpty() ? builtInClientId : newClientId.trim();
    apiKey = newApiKey.trim().isEmpty() ? builtInApiKey : newApiKey.trim();
    searchCache.clear();

    if (settings != nullptr)
    {
        settings->setValue ("freesound.clientId", newClientId.trim());
        settings->setValue ("freesound.apiKey", newApiKey.trim());
        settings->saveIfNeeded();
    }

    signOut(); // a sign-in belongs to the credentials it was made with
}

bool FreesoundClient::usesBuiltInCredentials() const
{
    return apiKey.isNotEmpty() && apiKey == builtInApiKey;
}

//==============================================================================
void FreesoundClient::throttle()
{
    const juce::ScopedLock sl (throttleLock);
    const auto wait = lastRequestMs + requestGapMs - juce::Time::getMillisecondCounterHiRes();
    if (wait > 0)
        juce::Thread::sleep ((int) wait);

    lastRequestMs = juce::Time::getMillisecondCounterHiRes();
}

FreesoundClient::Response FreesoundClient::request (const juce::URL& url, const juce::String& authHeader, bool post)
{
    throttle();

    Response r;
    const auto options = juce::URL::InputStreamOptions (post ? juce::URL::ParameterHandling::inPostData
                                                             : juce::URL::ParameterHandling::inAddress)
                             .withExtraHeaders (juce::String ("User-Agent: ") + userAgent
                                                + (authHeader.isNotEmpty() ? "\r\nAuthorization: " + authHeader : juce::String()))
                             .withConnectionTimeoutMs (15000)
                             .withNumRedirectsToFollow (5)
                             .withStatusCode (&r.status);

    auto stream = url.createInputStream (options);
    if (stream != nullptr)
        r.text = stream->readEntireStreamAsString();

    if (stream == nullptr || r.status >= 400)
        r.error = httpError (r.status);

    return r;
}

bool FreesoundClient::storeTokens (const juce::var& json)
{
    const auto access = json["access_token"].toString();
    const auto refresh = json["refresh_token"].toString();
    if (access.isEmpty() || refresh.isEmpty())
        return false;

    const juce::ScopedLock sl (tokenLock);
    accessToken = access;
    refreshToken = refresh;
    accessExpiresMs = juce::Time::getMillisecondCounterHiRes() + 1000.0 * juce::jmax (60, (int) json["expires_in"]);
    return true;
}

juce::String FreesoundClient::bearerToken (juce::String& error)
{
    juce::String refresh;
    {
        const juce::ScopedLock sl (tokenLock);
        if (accessToken.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() < accessExpiresMs - 60000.0)
            return "Bearer " + accessToken;

        refresh = refreshToken;
    }

    if (refresh.isEmpty())
    {
        error = "signed-out";
        return {};
    }

    // Access tokens last 24 hours; the saved refresh token gets a new one (and a new refresh token).
    const auto r = request (juce::URL (juce::String (apiBase) + "/oauth2/access_token/")
                                .withParameter ("client_id", clientId)
                                .withParameter ("client_secret", apiKey)
                                .withParameter ("grant_type", "refresh_token")
                                .withParameter ("refresh_token", refresh),
                            {}, true);

    if (r.status == 400 || r.status == 401)
    {
        // Revoked, or the credentials changed: the user has to sign in again.
        juce::WeakReference<FreesoundClient> weak (this);
        juce::MessageManager::callAsync ([weak] { if (auto* self = weak.get()) self->signOut(); });
        error = "signed-out";
        return {};
    }

    if (r.error.isNotEmpty() || ! storeTokens (juce::JSON::parse (r.text)))
    {
        error = r.error.isNotEmpty() ? r.error : "Freesound sent an unexpected reply";
        return {};
    }

    juce::WeakReference<FreesoundClient> weak (this);
    juce::MessageManager::callAsync ([weak] { if (auto* self = weak.get()) self->saveAccount(); });

    const juce::ScopedLock sl (tokenLock);
    return "Bearer " + accessToken;
}

//==============================================================================
juce::String FreesoundClient::licenseLabel (const juce::String& license)
{
    const auto l = license.toLowerCase();

    if (l.contains ("publicdomain/zero") || l.contains ("creative commons 0") || l == "cc0")
        return "CC0";
    if (l.contains ("sampling+"))
        return "Sampling+";

    // http://creativecommons.org/licenses/by-nc/3.0/ -> "CC BY-NC 3.0"
    if (l.contains ("/licenses/"))
    {
        const auto parts = juce::StringArray::fromTokens (l.fromFirstOccurrenceOf ("/licenses/", false, false), "/", "");
        return ("CC " + parts[0].toUpperCase() + " " + parts[1]).trim();
    }

    if (l == "attribution")               return "CC BY";
    if (l == "attribution noncommercial") return "CC BY-NC";
    return license;
}

juce::String FreesoundClient::keyLabel (const juce::String& tonality)
{
    const auto parts = juce::StringArray::fromTokens (tonality.trim(), " ", "");
    if (parts.size() != 2 || parts[0].isEmpty() || ! juce::String ("ABCDEFG").containsChar (parts[0][0]))
        return {};

    return parts[0] + (parts[1].equalsIgnoreCase ("minor") ? "m" : "");
}

juce::String FreesoundClient::filterQuery (const juce::var& filters)
{
    juce::StringArray clauses;

    if (const auto* ranges = filters["bpm"].getArray(); ranges != nullptr && ! ranges->isEmpty())
    {
        juce::StringArray bpm;
        for (const auto& range : *ranges)
        {
            const auto lo = range[0].isVoid() ? juce::String ("*") : juce::String ((int) std::floor ((double) range[0]));
            const auto hi = range[1].isVoid() ? juce::String ("*") : juce::String ((int) std::ceil ((double) range[1]));
            bpm.add ("bpm:[" + lo + " TO " + hi + "]");
        }
        clauses.add (bpm.size() == 1 ? bpm[0] : "(" + bpm.joinIntoString (" OR ") + ")");
    }

    if (const auto names = tonalityNames (filters["key"].toString()); ! names.isEmpty())
    {
        juce::StringArray keys;
        for (const auto& n : names)
            keys.add ("tonality:\"" + n + "\"");
        clauses.add (keys.size() == 1 ? keys[0] : "(" + keys.joinIntoString (" OR ") + ")");
    }

    return clauses.joinIntoString (" ");
}

std::vector<FreesoundSound> FreesoundClient::parseResults (const juce::var& json, bool& hasMore)
{
    std::vector<FreesoundSound> sounds;
    hasMore = json["next"].isString() && json["next"].toString().isNotEmpty();

    if (const auto* results = json["results"].getArray())
    {
        for (const auto& r : *results)
        {
            FreesoundSound s;
            s.id = r["id"].toString();
            s.name = stripAudioExtension (r["name"].toString());
            s.author = r["username"].toString();
            s.licenseUrl = r["license"].toString();
            s.license = licenseLabel (s.licenseUrl);
            s.duration = (double) r["duration"];
            s.pageUrl = r["url"].toString();
            s.type = r["type"].toString();
            s.previewUrl = r["previews"]["preview-hq-ogg"].toString();
            s.key = keyLabel (r["tonality"].toString());
            if (const auto bpm = (double) r["bpm"]; bpm >= 30.0 && bpm <= 300.0)
                s.bpm = juce::roundToInt (bpm);

            if (s.id.containsOnly ("0123456789") && s.id.isNotEmpty() && s.previewUrl.startsWith ("https://"))
                sounds.push_back (std::move (s));
        }
    }

    return sounds;
}

void FreesoundClient::search (const juce::String& query, int page, const juce::var& filters, std::function<void (juce::var)> callback)
{
    if (apiKey.isEmpty())
    {
        callback (errorResult ("no-key"));
        return;
    }

    page = juce::jmax (1, page);
    auto url = juce::URL (juce::String (apiBase) + "/search/")
                   .withParameter ("query", query.trim())
                   .withParameter ("page", juce::String (page))
                   .withParameter ("page_size", juce::String (pageSize))
                   .withParameter ("fields", searchFields)
                   .withParameter ("sort", query.trim().isEmpty() ? "downloads_desc" : "score");
    if (const auto filter = filterQuery (filters); filter.isNotEmpty())
        url = url.withParameter ("filter", filter);

    const auto cacheKey = url.toString (true);

    auto respond = [this, page, callback] (const juce::var& cached)
    {
        // Re-attach download state: it can change after a search was cached.
        juce::Array<juce::var> list;
        for (const auto& id : *cached["ids"].getArray())
            if (auto* sound = find (id.toString()))
                list.add (toVar (*sound, findDownload (sound->id)));

        auto* o = new juce::DynamicObject();
        o->setProperty ("sounds", list);
        o->setProperty ("page", page);
        o->setProperty ("hasMore", cached["hasMore"]);
        callback (juce::var (o));
    };

    if (auto it = searchCache.find (cacheKey); it != searchCache.end())
    {
        respond (it->second);
        return;
    }

    juce::WeakReference<FreesoundClient> weak (this);
    api.addJob ([this, weak, url, cacheKey, callback, respond, auth = "Token " + apiKey]
    {
        const auto r = request (url, auth);
        bool hasMore = false;
        auto sounds = r.error.isEmpty() ? parseResults (juce::JSON::parse (r.text), hasMore) : std::vector<FreesoundSound>();

        juce::MessageManager::callAsync ([weak, cacheKey, callback, respond, error = r.error, hasMore, sounds = std::move (sounds)]
        {
            auto* self = weak.get();
            if (self == nullptr)
                return;

            if (error.isNotEmpty())
            {
                callback (errorResult (error));
                return;
            }

            juce::Array<juce::var> ids;
            for (const auto& s : sounds)
            {
                self->known[s.id] = s;
                ids.add (s.id);
            }

            auto* entry = new juce::DynamicObject();
            entry->setProperty ("ids", ids);
            entry->setProperty ("hasMore", hasMore);
            self->searchCache[cacheKey] = juce::var (entry);
            respond (self->searchCache[cacheKey]);
        });
    });
}

const FreesoundSound* FreesoundClient::find (const juce::String& id) const
{
    const auto it = known.find (id);
    return it == known.end() ? nullptr : &it->second;
}

//==============================================================================
void FreesoundClient::fetchPreview (const juce::String& id, FileCallback callback)
{
    const auto* sound = find (id);
    if (sound == nullptr)
    {
        callback ({}, "unknown sound");
        return;
    }

    if (const auto cached = findPreview (id); cached.existsAsFile())
    {
        cached.setLastModificationTime (juce::Time::getCurrentTime()); // keeps it out of the next trim
        callback (cached, {});
        return;
    }

    auto& waiting = fetching["p:" + id];
    waiting.push_back (std::move (callback));
    if (waiting.size() > 1)
        return; // already on its way

    const auto target = previewCache().getChildFile (juce::File::createLegalFileName (sound->name.substring (0, 80)) + " [fs" + id + "].ogg");
    juce::WeakReference<FreesoundClient> weak (this);

    // Previews come from Freesound's CDN, not the API, so they don't count against the request limit.
    files.addJob ([weak, id, target, url = sound->previewUrl]
    {
        juce::String error;
        int status = 0;
        const auto part = target.getSiblingFile (target.getFileName() + ".part");
        target.getParentDirectory().createDirectory();
        part.deleteFile(); // FileOutputStream appends to an existing file

        auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                             .withExtraHeaders (juce::String ("User-Agent: ") + userAgent)
                                                             .withConnectionTimeoutMs (15000)
                                                             .withNumRedirectsToFollow (5)
                                                             .withStatusCode (&status));
        if (stream == nullptr || status >= 400)
        {
            error = httpError (status);
        }
        else
        {
            bool written = false;
            {
                juce::FileOutputStream out (part);
                written = out.openedOk() && out.writeFromInputStream (*stream, -1) > 0;
            }
            if (! (written && part.moveFileTo (target)))
                error = "couldn't save the preview";
        }
        part.deleteFile();

        juce::MessageManager::callAsync ([weak, id, target, error]
        {
            auto* self = weak.get();
            if (self == nullptr)
                return;

            if (error.isEmpty())
            {
                // Trim the cache, oldest first, once the newest previews fill the cap.
                auto cached = previewCache().findChildFiles (juce::File::findFiles, false);
                std::sort (cached.begin(), cached.end(), [] (const juce::File& a, const juce::File& b)
                           { return a.getLastModificationTime() > b.getLastModificationTime(); });
                juce::int64 total = 0;
                for (const auto& f : cached)
                    if ((total += f.getSize()) > previewCacheBytes && f != target)
                        f.deleteFile();
            }

            auto callbacks = std::move (self->fetching["p:" + id]);
            self->fetching.erase ("p:" + id);
            for (auto& cb : callbacks)
                cb (error.isEmpty() ? target : juce::File(), error);
        });
    });
}

//==============================================================================
juce::String FreesoundClient::authorizeUrl (const juce::String& state) const
{
    return juce::URL (juce::String (apiBase) + "/oauth2/authorize/")
        .withParameter ("client_id", clientId)
        .withParameter ("response_type", "code")
        .withParameter ("state", state)
        .toString (true);
}

void FreesoundClient::signIn (const juce::String& code, std::function<void (juce::String)> callback)
{
    juce::WeakReference<FreesoundClient> weak (this);
    api.addJob ([this, weak, code, callback]
    {
        juce::String error, name;
        const auto r = request (juce::URL (juce::String (apiBase) + "/oauth2/access_token/")
                                    .withParameter ("client_id", clientId)
                                    .withParameter ("client_secret", apiKey)
                                    .withParameter ("grant_type", "authorization_code")
                                    .withParameter ("code", code),
                                {}, true);

        if (r.status == 400 || r.status == 401)
            error = "Freesound didn't accept the sign-in. Check the client ID and API key";
        else if (r.error.isNotEmpty())
            error = r.error;
        else if (! storeTokens (juce::JSON::parse (r.text)))
            error = "Freesound sent an unexpected reply";

        if (error.isEmpty())
        {
            juce::String tokenError;
            const auto me = request (juce::URL (juce::String (apiBase) + "/me/"), bearerToken (tokenError));
            name = juce::JSON::parse (me.text)["username"].toString();
        }

        juce::MessageManager::callAsync ([weak, callback, error, name]
        {
            auto* self = weak.get();
            if (self == nullptr)
                return;

            if (error.isEmpty())
            {
                self->username = name;
                self->saveAccount();
            }
            callback (error);
        });
    });
}

void FreesoundClient::signOut()
{
    {
        const juce::ScopedLock sl (tokenLock);
        accessToken = {};
        refreshToken = {};
    }
    username = {};
    saveAccount();
}

bool FreesoundClient::isSignedIn() const
{
    const juce::ScopedLock sl (tokenLock);
    return refreshToken.isNotEmpty();
}

void FreesoundClient::saveAccount()
{
    if (settings != nullptr)
    {
        const juce::ScopedLock sl (tokenLock);
        settings->setValue ("freesound.refreshToken", refreshToken);
        settings->setValue ("freesound.username", username);
        settings->saveIfNeeded();
    }

    if (onAccountChange != nullptr)
        onAccountChange();
}

//==============================================================================
void FreesoundClient::download (const juce::String& id, FileCallback callback)
{
    const auto* sound = find (id);
    if (sound == nullptr)
    {
        callback ({}, "unknown sound");
        return;
    }

    if (const auto kept = findDownload (id); kept.existsAsFile())
    {
        callback (kept, {});
        return;
    }

    if (! isSignedIn())
    {
        callback ({}, "signed-out");
        return;
    }

    auto& waiting = fetching["d:" + id];
    waiting.push_back (std::move (callback));
    if (waiting.size() > 1)
        return;

    const auto base = pathFor (*sound, ".part");
    juce::WeakReference<FreesoundClient> weak (this);

    files.addJob ([this, weak, id, base, sound = *sound]
    {
        juce::String error;
        juce::File saved;
        const juce::URL url (juce::String (apiBase) + "/sounds/" + id + "/download/");

        for (int attempt = 0; attempt < 2 && saved == juce::File() && error.isEmpty(); ++attempt)
        {
            const auto auth = bearerToken (error);
            if (auth.isEmpty())
                break;

            throttle();
            int status = 0;
            juce::StringPairArray headers;
            auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                     .withExtraHeaders (juce::String ("User-Agent: ") + userAgent + "\r\nAuthorization: " + auth)
                                                     .withConnectionTimeoutMs (15000)
                                                     .withNumRedirectsToFollow (5)
                                                     .withResponseHeaders (&headers)
                                                     .withStatusCode (&status));

            if (status == 401 && attempt == 0)
            {
                const juce::ScopedLock sl (tokenLock);
                accessToken = {}; // expired early: refresh and try once more
                continue;
            }

            if (stream == nullptr || status >= 400)
            {
                error = httpError (status);
                break;
            }

            const auto contentType = headers.getValue ("Content-Type", headers.getValue ("content-type", {}));
            if (contentType.containsIgnoreCase ("text/html") || contentType.containsIgnoreCase ("json"))
            {
                error = "not-a-file";
                break;
            }

            base.getParentDirectory().createDirectory();
            base.deleteFile();
            bool written = false;
            {
                juce::FileOutputStream out (base);
                written = out.openedOk() && out.writeFromInputStream (*stream, -1) > 0;
            }

            auto extension = extensionForType (sound.type);
            if (extension.isEmpty())
                extension = sniffExtension (base);

            const auto target = base.withFileExtension (extension.isNotEmpty() ? extension : ".wav");
            if (written && base.moveFileTo (target))
                saved = target;
            else
                error = "couldn't save the file";
        }
        base.deleteFile();

        juce::MessageManager::callAsync ([weak, id, saved, error, credit = sound.attribution()]
        {
            auto* self = weak.get();
            if (self == nullptr)
                return;

            if (saved.existsAsFile())
            {
                // Most Freesound licences need credit; keep it next to the files.
                const auto credits = downloadsFolder().getChildFile ("CREDITS.txt");
                if (! credits.loadFileAsString().contains (credit))
                    credits.appendText (credit + juce::newLine);
            }

            auto callbacks = std::move (self->fetching["d:" + id]);
            self->fetching.erase ("d:" + id);
            for (auto& cb : callbacks)
                cb (saved, error);
        });
    });
}

//==============================================================================
juce::File FreesoundClient::downloadsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("freeloops").getChildFile ("Freesound");
}

juce::File FreesoundClient::previewCache()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("freeloops").getChildFile ("cache").getChildFile ("freesound-previews");
}

juce::File FreesoundClient::findIn (const juce::File& folder, const juce::String& id)
{
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*[fs" + id + "]*"))
        if (! f.getFileName().endsWith (".part"))
            return f;

    return {};
}

juce::File FreesoundClient::pathFor (const FreesoundSound& s, const juce::String& extension) const
{
    juce::StringArray parts { s.name.substring (0, 100) };
    if (s.bpm > 0)
        parts.add (juce::String (s.bpm) + "bpm");
    if (s.key.isNotEmpty())
        parts.add (s.key);
    if (s.author.isNotEmpty())
        parts.add (s.author);

    const auto name = juce::File::createLegalFileName (parts.joinIntoString (" - ") + " [fs" + s.id + "]");
    return downloadsFolder().getChildFile (name + extension);
}

juce::var FreesoundClient::toVar (const FreesoundSound& s, const juce::File& downloaded)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", "fs:" + s.id);
    o->setProperty ("name", s.name);
    o->setProperty ("author", s.author);
    o->setProperty ("license", s.license);
    o->setProperty ("key", s.key);
    o->setProperty ("bpm", s.bpm);
    o->setProperty ("duration", s.duration);
    o->setProperty ("url", s.pageUrl);
    o->setProperty ("path", downloaded.existsAsFile() ? downloaded.getFullPathName() : juce::String());
    return juce::var (o);
}
