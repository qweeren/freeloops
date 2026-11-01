// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Runs SampleFocusClient::parseResults on a saved Sample Focus search page and checks the result, plus the
// search URLs it builds. Sample Focus's pages aren't stored in this repo (it's their content); save one yourself:
//   curl -sL "https://samplefocus.com/samples?search=trap" -o samples.html
//   SampleFocusParseTest samples.html

#include "../Source/SampleFocus.h"

#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: SampleFocusParseTest <saved search page.html>\n";
        return 2;
    }

    const auto html = juce::File (juce::CharPointer_UTF8 (argv[1])).loadFileAsString();
    bool hasMore = false;
    const auto samples = SampleFocusClient::parseResults (html, hasMore);

    int failures = 0;
    const auto check = [&] (bool ok, const juce::String& what)
    {
        if (! ok)
        {
            ++failures;
            std::cout << "FAIL: " << what << "\n";
        }
    };

    int players = 0;
    for (auto at = html.indexOf ("\"sample_url\":"); at >= 0; at = html.indexOf (at + 1, "\"sample_url\":"))
        ++players;

    check (! samples.empty(), "found no samples");
    check ((int) samples.size() == players, "parsed " + juce::String ((int) samples.size()) + " of " + juce::String (players) + " samples");
    for (const auto& s : samples)
    {
        std::cout << s.id << " | " << s.name << " | " << s.author << " | " << s.bpm << " bpm | key " << s.key
                  << " | " << (s.isLoop ? "loop" : "one-shot") << " | " << s.tags.joinIntoString (", ").substring (0, 40)
                  << " | " << juce::String (s.duration, 1) << "s\n";

        check (s.name.isNotEmpty(), s.id + ": name");
        check (s.author.isNotEmpty() && ! s.author.endsWith ("..."), s.id + ": author");
        check (s.previewUrl.upToFirstOccurrenceOf ("?", false, false).endsWith (".mp3"), s.id + ": preview url");
        check (s.pageUrl == "https://samplefocus.com/samples/" + s.slug, s.id + ": page url");
        check (s.key.isEmpty() || juce::String ("ABCDEFG").containsChar (s.key[0]), s.id + ": key");
        check (s.bpm == 0 || (s.bpm >= 30 && s.bpm <= 300), s.id + ": bpm");
        check (s.duration > 0, s.id + ": duration");
    }

    // Search URLs: one tempo range (open ends filled in) and a key split into note and mode.
    auto* filters = new juce::DynamicObject();
    filters->setProperty ("bpm", juce::JSON::parse ("[[137, 143], [68.5, 71.5]]"));
    filters->setProperty ("key", "A#m");
    const auto url = SampleFocusClient::searchUrl ("dark trap", 2, juce::var (filters));
    std::cout << "url: " << url << "\n";
    check (url == "https://samplefocus.com/samples?search=dark%20trap&min_tempo=137&max_tempo=143&key=A%23&mode=minor&page=2", "search url");

    auto* open = new juce::DynamicObject();
    open->setProperty ("bpm", juce::JSON::parse ("[[120, null]]"));
    open->setProperty ("key", "C");
    check (SampleFocusClient::searchUrl ({}, 1, juce::var (open)) == "https://samplefocus.com/samples?min_tempo=120&max_tempo=300&key=C&mode=major",
           "open tempo range and major key");

    std::cout << samples.size() << " samples, hasMore=" << (hasMore ? "yes" : "no") << ", " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
