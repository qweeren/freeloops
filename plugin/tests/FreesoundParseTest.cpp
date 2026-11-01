// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Checks FreesoundClient's parsing of API replies and the search filters it builds.
//   FreesoundParseTest tests/freesound-search.json
// A real reply can be checked too (fixture-specific checks are skipped):
//   curl -s -H "Authorization: Token YOUR_KEY" "https://freesound.org/apiv2/search/?query=piano+loop&fields=id,name,username,license,duration,previews,bpm,tonality,type,url" -o reply.json
//   FreesoundParseTest reply.json --live

#include "../Source/Freesound.h"

#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: FreesoundParseTest <search reply.json> [--live]\n";
        return 2;
    }

    const auto live = argc > 2 && juce::String (argv[2]) == "--live";
    const auto json = juce::JSON::parse (juce::File (juce::CharPointer_UTF8 (argv[1])).loadFileAsString());
    bool hasMore = false;
    const auto sounds = FreesoundClient::parseResults (json, hasMore);

    int failures = 0;
    const auto check = [&] (bool ok, const juce::String& what)
    {
        if (! ok)
        {
            ++failures;
            std::cout << "FAIL: " << what << "\n";
        }
    };

    check (! sounds.empty(), "found no sounds");
    for (const auto& s : sounds)
    {
        std::cout << s.id << " | " << s.name << " | " << s.author << " | " << s.bpm << " bpm | key " << s.key
                  << " | " << s.license << " | " << juce::String (s.duration, 2) << "s | " << s.type << "\n";

        check (s.name.isNotEmpty(), s.id + ": name");
        check (s.author.isNotEmpty(), s.id + ": author");
        check (s.previewUrl.endsWith (".ogg"), s.id + ": preview url");
        check (s.pageUrl.contains ("/sounds/" + s.id + "/"), s.id + ": page url");
        check (s.license.isNotEmpty() && ! s.license.startsWith ("http"), s.id + ": licence label");
        check (s.bpm == 0 || (s.bpm >= 30 && s.bpm <= 300), s.id + ": bpm");
    }

    if (! live)
    {
        check (hasMore, "hasMore");
        check (sounds.size() == 3, "sound without a preview is skipped");
        if (sounds.size() == 3)
        {
            check (sounds[0].name == "Piano Loop 120bpm", "file extension stripped from name");
            check (sounds[0].bpm == 120 && sounds[0].key == "Am", "bpm rounded, key label");
            check (sounds[0].license == "CC BY 4.0", "CC BY label");
            check (sounds[1].license == "CC0" && sounds[1].bpm == 0 && sounds[1].key.isEmpty(), "CC0, no analysis");
            check (sounds[2].license == "CC BY-NC 3.0" && sounds[2].key == "Bb", "CC BY-NC label, major key");
            check (sounds[0].attribution().contains ("by example_user") && sounds[0].attribution().contains ("CC BY 4.0"), "attribution");
        }

        // Search filters: BPM ranges (with half/double time) and a key in both spellings.
        auto* filters = new juce::DynamicObject();
        filters->setProperty ("bpm", juce::JSON::parse ("[[137, 143], [68.5, 71.5]]"));
        filters->setProperty ("key", "A#m");
        const auto query = FreesoundClient::filterQuery (juce::var (filters));
        std::cout << "filter: " << query << "\n";
        check (query == "(bpm:[137 TO 143] OR bpm:[68 TO 72]) (tonality:\"A# minor\" OR tonality:\"Bb minor\")", "filter query");

        auto* open = new juce::DynamicObject();
        open->setProperty ("bpm", juce::JSON::parse ("[[120, null]]"));
        open->setProperty ("key", "C");
        check (FreesoundClient::filterQuery (juce::var (open)) == "bpm:[120 TO *] tonality:\"C major\"", "open range, natural key");
    }

    std::cout << sounds.size() << " sounds, hasMore=" << (hasMore ? "yes" : "no") << ", " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
