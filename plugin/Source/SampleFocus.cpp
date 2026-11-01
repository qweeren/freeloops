// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SampleFocus.h"

namespace
{
    constexpr auto baseUrl = "https://samplefocus.com";
    constexpr juce::int64 previewCacheBytes = 300LL * 1024 * 1024;

    // The search page hands its results to a React component as JSON in this script tag.
    constexpr auto resultsMarker = "data-component-name=\"SamplesWithTagFilter\"";

    juce::String fileTag (const juce::String& id)
    {
        return " [sf" + id + "]";
    }

    // Search results only carry a display name cut to 10 characters ("Carter Sch..."). The user's slug
    // ("carter-schaaf") usually spells the rest: "Carter Schaaf". Slugs that don't match (ids) are left out.
    juce::String authorName (const juce::String& truncated, const juce::String& slug)
    {
        if (! truncated.endsWith ("..."))
            return truncated.trim();

        const auto start = truncated.dropLastCharacters (3);
        const auto full = slug.replaceCharacter ('-', ' ');
        return full.startsWithIgnoreCase (start) ? start + full.substring (start.length()) : start.trim();
    }
}

//==============================================================================
bool SampleFocusClient::isResultsPage (const juce::String& html)
{
    return html.contains (resultsMarker);
}

std::vector<SampleFocusSample> SampleFocusClient::parseResults (const juce::String& html, bool& hasMore)
{
    std::vector<SampleFocusSample> samples;
    hasMore = false;

    const auto at = html.indexOf (resultsMarker);
    const auto start = at < 0 ? -1 : html.indexOf (at, ">");
    const auto end = start < 0 ? -1 : html.indexOf (start, "</script>");
    if (end < 0)
        return samples;

    const auto json = juce::JSON::parse (html.substring (start + 1, end));
    hasMore = (int) json["pagyMetadata"]["next"] > 0;

    if (const auto* list = json["samples"].getArray())
    {
        for (const auto& r : *list)
        {
            SampleFocusSample s;
            s.id = r["id"].toString();
            s.slug = r["slug"].toString();
            s.name = r["name"].toString().trim();
            s.author = authorName (r["user_display_name_truncated"].toString(), r["user_slug"].toString());
            s.pageUrl = r["sample_url"].toString();
            s.previewUrl = r["sample_mp3_url"].toString();
            s.duration = r["duration"].toString().getDoubleValue();
            if (s.duration <= 0.0)
                s.duration = (double) r["approximate_duration"];

            if (const auto tempo = (int) r["tempo"]; tempo >= 30 && tempo <= 300)
                s.bpm = tempo;

            // "A#" + "minor" -> "A#m". Sample Focus's keys are all sharps.
            if (const auto note = r["key"].toString(); note.isNotEmpty() && juce::String ("ABCDEFG").containsChar (note[0]))
                s.key = note + (r["mode"].toString() == "minor" ? "m" : "");

            if (const auto* tags = r["tag_list"].getArray())
                for (const auto& t : *tags)
                    s.tags.add (t.toString());
            s.isLoop = s.tags.contains ("loop");

            if (s.id.containsOnly ("0123456789") && s.id.isNotEmpty() && s.slug.isNotEmpty()
                && s.pageUrl.startsWith (juce::String (baseUrl) + "/samples/") && s.previewUrl.startsWith ("https://"))
                samples.push_back (std::move (s));
        }
    }

    return samples;
}

juce::String SampleFocusClient::searchUrl (const juce::String& query, int page, const juce::var& filters)
{
    juce::StringArray params;
    if (query.trim().isNotEmpty())
        params.add ("search=" + juce::URL::addEscapeChars (query.trim(), true));

    // One tempo range, both ends given (the site's own filter always sends both).
    if (const auto* ranges = filters["bpm"].getArray(); ranges != nullptr && ! ranges->isEmpty())
    {
        const auto& range = ranges->getReference (0);
        params.add ("min_tempo=" + juce::String (range[0].isVoid() ? 1 : (int) std::floor ((double) range[0])));
        params.add ("max_tempo=" + juce::String (range[1].isVoid() ? 300 : (int) std::ceil ((double) range[1])));
    }

    // "C#m" -> key=C%23&mode=minor
    if (const auto key = filters["key"].toString(); key.isNotEmpty())
    {
        const auto minor = key.endsWith ("m");
        params.add ("key=" + juce::URL::addEscapeChars (minor ? key.dropLastCharacters (1) : key, true));
        params.add (juce::String ("mode=") + (minor ? "minor" : "major"));
    }

    if (page > 1)
        params.add ("page=" + juce::String (page));

    return juce::String (baseUrl) + "/samples" + (params.isEmpty() ? "" : "?" + params.joinIntoString ("&"));
}

juce::var SampleFocusClient::cachedResults (const juce::String& url, int page) const
{
    const auto it = searchCache.find (url);
    if (it == searchCache.end())
        return {};

    // Re-attach download state: it can change after a search was cached.
    juce::Array<juce::var> list;
    for (const auto& id : *it->second["ids"].getArray())
        if (auto* sample = find (id.toString()))
            list.add (toVar (*sample, findDownload (sample->id)));

    auto* o = new juce::DynamicObject();
    o->setProperty ("samples", list);
    o->setProperty ("page", page);
    o->setProperty ("hasMore", it->second["hasMore"]);
    return juce::var (o);
}

juce::var SampleFocusClient::addResults (const juce::String& url, int page, const juce::String& html)
{
    // A page without the results component is an error page, not "no results".
    if (! isResultsPage (html))
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("error", "Sample Focus sent an unexpected page");
        return juce::var (o);
    }

    bool hasMore = false;
    juce::Array<juce::var> ids;
    for (auto& s : parseResults (html, hasMore))
    {
        ids.add (s.id);
        known[s.id] = std::move (s);
    }

    auto* entry = new juce::DynamicObject();
    entry->setProperty ("ids", ids);
    entry->setProperty ("hasMore", hasMore);
    searchCache[url] = juce::var (entry);
    return cachedResults (url, page);
}

//==============================================================================
const SampleFocusSample* SampleFocusClient::find (const juce::String& id) const
{
    const auto it = known.find (id);
    return it == known.end() ? nullptr : &it->second;
}

juce::File SampleFocusClient::downloadsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("freeloops").getChildFile ("Sample Focus");
}

juce::File SampleFocusClient::previewCache()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("freeloops").getChildFile ("cache").getChildFile ("samplefocus-previews");
}

void SampleFocusClient::trimPreviewCache()
{
    auto files = previewCache().findChildFiles (juce::File::findFiles, false);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });

    juce::int64 total = 0;
    for (const auto& f : files)
    {
        total += f.getSize();
        if (total > previewCacheBytes)
            f.deleteFile(); // oldest first, once the newest ones fill the cap
    }
}

juce::File SampleFocusClient::findIn (const juce::File& folder, const juce::String& id)
{
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*[sf" + id + "]*"))
        if (! f.getFileName().endsWith (".part"))
            return f;

    return {};
}

juce::File SampleFocusClient::downloadPathFor (const juce::String& id) const
{
    const auto* s = find (id);
    if (s == nullptr)
        return {};

    juce::StringArray parts { s->name.substring (0, 100) };
    if (s->bpm > 0)
        parts.add (juce::String (s->bpm) + "bpm");
    if (s->key.isNotEmpty())
        parts.add (s->key);
    if (s->author.isNotEmpty())
        parts.add (s->author);

    return downloadsFolder().getChildFile (juce::File::createLegalFileName (parts.joinIntoString (" - ") + fileTag (id)));
}

juce::File SampleFocusClient::previewPathFor (const juce::String& id) const
{
    const auto* s = find (id);
    if (s == nullptr)
        return {};

    return previewCache().getChildFile (juce::File::createLegalFileName (s->name.substring (0, 80) + fileTag (id)) + ".mp3");
}

juce::var SampleFocusClient::toVar (const SampleFocusSample& s, const juce::File& downloaded)
{
    juce::Array<juce::var> tags;
    for (const auto& t : s.tags)
        tags.add (t);

    auto* o = new juce::DynamicObject();
    o->setProperty ("id", "sf:" + s.id);
    o->setProperty ("name", s.name);
    o->setProperty ("author", s.author);
    o->setProperty ("key", s.key);
    o->setProperty ("bpm", s.bpm);
    o->setProperty ("duration", s.duration);
    o->setProperty ("tags", tags);
    o->setProperty ("loop", s.isLoop);
    o->setProperty ("url", s.pageUrl);
    o->setProperty ("path", downloaded.existsAsFile() ? downloaded.getFullPathName() : juce::String());
    return juce::var (o);
}
