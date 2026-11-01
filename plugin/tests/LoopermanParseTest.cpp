// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Runs LoopermanClient::parseResults on a saved Looperman search page and checks the result.
// Looperman's HTML isn't stored in this repo (it's their content); save a page yourself:
//   curl -sL "https://www.looperman.com/loops?keys=trap" -o loops.html
//   LoopermanParseTest loops.html

#include "../Source/Looperman.h"

#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: LoopermanParseTest <saved search page.html>\n";
        return 2;
    }

    const auto html = juce::File (juce::CharPointer_UTF8 (argv[1])).loadFileAsString();
    bool hasMore = false;
    const auto loops = LoopermanClient::parseResults (html, hasMore, 1);

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
    for (auto at = html.indexOf ("data-mp3="); at >= 0; at = html.indexOf (at + 1, "data-mp3="))
        ++players;

    check (! loops.empty(), "found no loops");
    check ((int) loops.size() == players, "parsed " + juce::String ((int) loops.size()) + " of " + juce::String (players) + " players");
    for (const auto& l : loops)
    {
        std::cout << l.id << " | " << l.title << " | " << l.author << " | " << l.bpm << " bpm | key " << l.key
                  << " | " << l.genre << " / " << l.category << " | " << juce::String (l.duration, 1) << "s\n";

        check (l.title.isNotEmpty() && ! l.title.contains ("<"), l.id + ": title");
        check (l.author.isNotEmpty() && ! l.author.contains ("<"), l.id + ": author");
        check (l.previewUrl.upToFirstOccurrenceOf ("?", false, false).endsWith (".mp3"), l.id + ": preview url");
        check (l.key != "Unknown", l.id + ": key");
        check (l.detailUrl.contains ("/loops/detail/" + l.id + "/"), l.id + ": detail url");
        check (l.bpm == 0 || (l.bpm >= 40 && l.bpm <= 300), l.id + ": bpm");
        check (l.duration > 0, l.id + ": duration");
    }

    std::cout << loops.size() << " loops, hasMore=" << (hasMore ? "yes" : "no") << ", " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
