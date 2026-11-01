// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "FreesoundSignIn.h"

namespace
{
    const juce::Colour background { 0xff1d2024 };
}

//==============================================================================
class FreesoundSignIn::Browser final : public juce::WebBrowserComponent
{
public:
    Browser (const juce::String& expectedState, std::function<void (const juce::String&)> onRedirect)
        : WebBrowserComponent (options()), state (expectedState), redirected (std::move (onRedirect)) {}

    // The redirect back from Freesound carries ?code=...&state=... (or ?error=access_denied&state=...).
    // Only top-level parameters count, so the login page's encoded "next=" link doesn't match.
    bool pageAboutToLoad (const juce::String& newURL) override
    {
        const juce::URL url (newURL);
        const auto names = url.getParameterNames();
        const auto values = url.getParameterValues();
        const auto value = [&] (const char* name) { const auto i = names.indexOf (name); return i < 0 ? juce::String() : values[i]; };

        if (value ("state") != state || (names.indexOf ("code") < 0 && names.indexOf ("error") < 0))
            return true;

        redirected (value ("code"));
        return false;
    }

    void newWindowAttemptingToLoad (const juce::String& url) override { goToURL (url); }
    bool pageLoadHadNetworkError (const juce::String&) override { return false; }

private:
    static Options options()
    {
        const auto dataFolder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                    .getChildFile ("freeloops").getChildFile ("webview2-freesound");
        return Options{}
            .withBackend (Options::Backend::webview2)
            .withWinWebView2Options (Options::WinWebView2{}.withUserDataFolder (dataFolder).withStatusBarDisabled());
    }

    juce::String state;
    std::function<void (const juce::String&)> redirected;
};

//==============================================================================
struct FreesoundSignIn::Content final : public juce::Component
{
    Content (FreesoundSignIn& owner, const juce::String& url, const juce::String& state)
    {
        hint.setText ("Your password goes only to freesound.org. freeloops never sees it.", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, juce::Colour (0xffb7bcc4));
        hint.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (hint);

        juce::Component::SafePointer<FreesoundSignIn> safe (&owner);
        browser = std::make_unique<Browser> (state, [safe] (const juce::String& code)
        {
            // Out of the browser's own callback before the window (and the browser) goes away.
            juce::MessageManager::callAsync ([safe, code] { if (safe != nullptr) safe->finish (code); });
        });
        addAndMakeVisible (*browser);
        browser->goToURL (url);
        setSize (620, 760);
    }

    void paint (juce::Graphics& g) override { g.fillAll (background); }

    void resized() override
    {
        auto area = getLocalBounds();
        hint.setBounds (area.removeFromTop (32));
        browser->setBounds (area);
    }

    juce::Label hint;
    std::unique_ptr<Browser> browser;
};

//==============================================================================
FreesoundSignIn::FreesoundSignIn (const juce::String& url, const juce::String& state, juce::Component& centreOn, Finished done)
    : DocumentWindow ("Sign in to Freesound", background, DocumentWindow::closeButton), onFinished (std::move (done))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (*this, url, state), true);
    setResizable (true, false);
    setCentrePosition (centreOn.getTopLevelComponent()->getScreenBounds().getCentre());
    setAlwaysOnTop (true); // otherwise it can open behind FL Studio
    setVisible (true);
    toFront (true);
}

FreesoundSignIn::~FreesoundSignIn() = default;

void FreesoundSignIn::finish (const juce::String& code)
{
    if (finished)
        return;

    finished = true;
    setVisible (false);
    onFinished (code); // may delete this window
}
