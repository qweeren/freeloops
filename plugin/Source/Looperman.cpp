// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Looperman.h"

namespace
{
    constexpr auto baseUrl = "https://www.looperman.com";
    constexpr auto userAgent = "freeloops/0.1 (open-source FL Studio sample browser)";
    constexpr double requestGapMs = 1500.0;      // never more than one request every 1.5 s
    constexpr juce::int64 auditionCacheBytes = 500LL * 1024 * 1024;

    juce::String decodeEntities (const juce::String& s)
    {
        return s.replace ("&amp;", "&").replace ("&quot;", "\"").replace ("&#039;", "'").replace ("&#39;", "'")
                .replace ("&lt;", "<").replace ("&gt;", ">").replace ("&nbsp;", " ").trim();
    }

    // Text between `start` and `end`, searching from `from`; empty if either marker is missing.
    juce::String between (const juce::String& text, const juce::String& start, const juce::String& end, int from = 0)
    {
        const auto s = text.indexOf (juce::jmax (0, from), start);
        if (s < 0)
            return {};

        const auto b = s + start.length();
        const auto e = text.indexOf (b, end);
        return e < 0 ? juce::String() : text.substring (b, e);
    }

    // The visible text of the <a> whose tooltip starts with `title`, e.g. "Genre : " -> "Trap Loops".
    juce::String taggedLink (const juce::String& tags, const juce::String& title)
    {
        const auto at = tags.indexOf ("data-bs-title=\"" + title);
        return at < 0 ? juce::String() : decodeEntities (between (tags, ">", "</a>", at));
    }

    juce::URL::InputStreamOptions requestOptions (int* status)
    {
        return juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                   .withExtraHeaders (juce::String ("User-Agent: ") + userAgent)
                   .withConnectionTimeoutMs (15000)
                   .withNumRedirectsToFollow (5)
                   .withStatusCode (status);
    }
}

LoopermanClient::LoopermanClient() = default;

LoopermanClient::~LoopermanClient()
{
    pool.removeAllJobs (true, 5000);
}

//==============================================================================
void LoopermanClient::throttle()
{
    const auto wait = lastRequestMs + requestGapMs - juce::Time::getMillisecondCounterHiRes();
    if (wait > 0)
        juce::Thread::sleep ((int) wait);

    lastRequestMs = juce::Time::getMillisecondCounterHiRes();
}

juce::String LoopermanClient::fetchText (const juce::String& url, juce::String& error)
{
    throttle();

    int status = 0;
    auto stream = juce::URL (url).createInputStream (requestOptions (&status));
    if (stream == nullptr || status >= 400)
    {
        error = status > 0 ? "Looperman returned HTTP " + juce::String (status) : "Couldn't reach Looperman";
        return {};
    }

    return stream->readEntireStreamAsString();
}

//==============================================================================
std::vector<LoopermanLoop> LoopermanClient::parseResults (const juce::String& html, bool& hasMore, int page)
{
    std::vector<LoopermanLoop> loops;
    const juce::String marker ("class=\"player-wrapper\"");

    for (auto pos = html.indexOf (marker); pos >= 0;)
    {
        const auto next = html.indexOf (pos + marker.length(), marker);
        const auto block = html.substring (pos, next < 0 ? html.length() : next);
        pos = next;

        const auto titleAt = block.indexOf ("class=\"player-title");
        if (titleAt < 0)
            continue;

        LoopermanLoop loop;
        loop.previewUrl = between (block, "data-mp3=\"", "\"");
        loop.duration = between (block, "data-duration=\"", "\"").getDoubleValue();
        loop.detailUrl = between (block, "href=\"", "\"", block.substring (0, titleAt).lastIndexOf ("href=\""));
        loop.title = decodeEntities (between (block, ">", "</a>", titleAt));
        loop.id = between (loop.detailUrl, "/detail/", "/");
        if (const auto userAt = block.indexOf ("getuser"); userAt >= 0)
            loop.author = decodeEntities (between (block, "\">", "</a>", block.indexOf (userAt, "<a href=")));

        const auto tags = block.substring (juce::jmax (0, block.indexOf ("tag-wrapper")));
        const auto bpmAt = tags.indexOf (" bpm</a>");
        if (bpmAt > 0)
            loop.bpm = tags.substring (0, bpmAt).fromLastOccurrenceOf (">", false, false).getIntValue();

        loop.genre = taggedLink (tags, "Genre : ").upToLastOccurrenceOf (" Loops", false, false);
        loop.category = taggedLink (tags, "Category : ").upToLastOccurrenceOf (" Loops", false, false);
        loop.key = between (tags, "Key : ", "</a>").trim();
        if (loop.key.equalsIgnoreCase ("Unknown"))
            loop.key = {};

        if (loop.id.containsOnly ("0123456789") && loop.id.isNotEmpty() && loop.previewUrl.startsWith (baseUrl))
            loops.push_back (std::move (loop));
    }

    // A pagination link to the next page ("page=3", not "page=30").
    const auto nextPage = "page=" + juce::String (page + 1);
    hasMore = false;
    for (auto at = html.indexOf (nextPage); at >= 0 && ! hasMore; at = html.indexOf (at + 1, nextPage))
        hasMore = ! juce::CharacterFunctions::isDigit (html[at + nextPage.length()]);

    return loops;
}

void LoopermanClient::search (const juce::String& query, int page, std::function<void (juce::var)> callback)
{
    page = juce::jmax (1, page);

    juce::StringArray params;
    if (query.trim().isNotEmpty())
        params.add ("keys=" + juce::URL::addEscapeChars (query.trim(), true));
    if (page > 1)
        params.add ("page=" + juce::String (page));

    const auto url = juce::String (baseUrl) + "/loops" + (params.isEmpty() ? "" : "?" + params.joinIntoString ("&"));

    auto respond = [this, page, callback] (const juce::var& cached)
    {
        // Re-attach download state: it can change after a search was cached.
        juce::Array<juce::var> list;
        for (const auto& id : *cached["ids"].getArray())
            if (auto* loop = find (id.toString()))
                list.add (toVar (*loop, findDownload (loop->id)));

        auto* o = new juce::DynamicObject();
        o->setProperty ("loops", list);
        o->setProperty ("page", page);
        o->setProperty ("hasMore", cached["hasMore"]);
        callback (juce::var (o));
    };

    if (auto it = searchCache.find (url); it != searchCache.end())
    {
        respond (it->second);
        return;
    }

    juce::WeakReference<LoopermanClient> weak (this);
    pool.addJob ([this, weak, url, page, callback, respond]
    {
        juce::String error;
        const auto html = fetchText (url, error);
        bool hasMore = false;
        auto loops = parseResults (html, hasMore, page);

        juce::MessageManager::callAsync ([weak, url, callback, respond, error, hasMore, loops = std::move (loops)]
        {
            auto* self = weak.get();
            if (self == nullptr)
                return;

            if (error.isNotEmpty())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("error", error);
                callback (juce::var (o));
                return;
            }

            juce::Array<juce::var> ids;
            for (const auto& loop : loops)
            {
                self->known[loop.id] = loop;
                ids.add (loop.id);
            }

            auto* entry = new juce::DynamicObject();
            entry->setProperty ("ids", ids);
            entry->setProperty ("hasMore", hasMore);
            self->searchCache[url] = juce::var (entry);
            respond (self->searchCache[url]);
        });
    });
}

//==============================================================================
const LoopermanLoop* LoopermanClient::find (const juce::String& id) const
{
    const auto it = known.find (id);
    return it == known.end() ? nullptr : &it->second;
}

juce::File LoopermanClient::downloadsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("freeloops").getChildFile ("Looperman");
}

juce::File LoopermanClient::auditionCache()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("freeloops").getChildFile ("cache").getChildFile ("looperman-auditions");
}

void LoopermanClient::trimAuditionCache()
{
    auto files = auditionCache().findChildFiles (juce::File::findFiles, false);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });

    juce::int64 total = 0;
    for (const auto& f : files)
    {
        total += f.getSize();
        if (total > auditionCacheBytes)
            f.deleteFile(); // oldest first, once the newest ones fill the cap
    }
}

juce::File LoopermanClient::findIn (const juce::File& folder, const juce::String& id)
{
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*[lm" + id + "]*"))
        if (! f.getFileName().endsWith (".part"))
            return f;

    return {};
}

juce::File LoopermanClient::pathFor (const juce::String& id, bool keep) const
{
    const auto* loop = find (id);
    if (loop == nullptr)
        return {};

    juce::StringArray parts { loop->title };
    if (loop->bpm > 0)
        parts.add (juce::String (loop->bpm) + "bpm");
    if (loop->key.isNotEmpty())
        parts.add (loop->key);
    if (loop->author.isNotEmpty())
        parts.add (loop->author);

    const auto name = juce::File::createLegalFileName (parts.joinIntoString (" - ") + " [lm" + id + "]");
    return (keep ? downloadsFolder() : auditionCache()).getChildFile (name + ".wav");
}

juce::var LoopermanClient::toVar (const LoopermanLoop& l, const juce::File& downloaded)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", "lm:" + l.id);
    o->setProperty ("name", l.title);
    o->setProperty ("author", l.author);
    o->setProperty ("genre", l.genre);
    o->setProperty ("category", l.category);
    o->setProperty ("key", l.key);
    o->setProperty ("bpm", l.bpm);
    o->setProperty ("duration", l.duration);
    o->setProperty ("url", l.detailUrl);
    o->setProperty ("path", downloaded.existsAsFile() ? downloaded.getFullPathName() : juce::String());
    return juce::var (o);
}
