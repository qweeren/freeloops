// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// A popup on Freesound's own OAuth2 page. The user signs in and grants access there; freeloops never
// sees the password. When Freesound redirects back with the authorization code, the redirect is caught
// before it loads (whatever callback URL the credentials name) and the code is handed to onFinished.
class FreesoundSignIn final : public juce::DocumentWindow
{
public:
    // code is empty if the user closed the window or refused access.
    using Finished = std::function<void (juce::String code)>;

    FreesoundSignIn (const juce::String& authorizeUrl, const juce::String& state, juce::Component& centreOn, Finished);
    ~FreesoundSignIn() override;

    void closeButtonPressed() override { finish ({}); }

private:
    class Browser;
    struct Content;

    void finish (const juce::String& code);

    Finished onFinished;
    bool finished = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreesoundSignIn)
};
