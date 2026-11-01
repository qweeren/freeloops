// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "LoopermanView.h"

namespace
{
    constexpr auto home = "https://www.looperman.com/";
    constexpr auto loginUrl = "https://www.looperman.com/account/login";
    constexpr auto logoutUrl = "https://www.looperman.com/account/logout";
    constexpr int downloadTimeoutMs = 90 * 1000;
    constexpr size_t maxDownloadBytes = 200 * 1024 * 1024;

    // Signed-out pages always show "Login" buttons. (A logout link is no signal: it's in the page's
    // hidden menu even when signed out.)
    constexpr auto signedInCheck = "!document.querySelector('a[href$=\"/account/login\"]')";

    // Runs inside the signed-in looperman.com page, so requests carry the user's own session.
    // It opens the loop's page, follows its download button, and hands the file back as base64.
    const char* downloadScript = R"JS(
(async (id, detailUrl) => {
  const send = (o) => window.__JUCE__.backend.emitEvent("freeloopsDownload", Object.assign({ id }, o));
  try {
    const page = await fetch(detailUrl, { credentials: "include" });
    const doc = new DOMParser().parseFromString(await page.text(), "text/html");
    if (doc.querySelector('a[href$="/account/login"]')) return send({ error: "signed-out" });

    const label = (el) => `${el.getAttribute("data-bs-title") || ""} ${el.getAttribute("title") || ""} ${el.textContent || ""}`;
    const target = (el) => el.getAttribute("href") || el.getAttribute("action") || "";
    const button = [...doc.querySelectorAll("form[action], a[href]")].find((el) =>
      /download/i.test(label(el) + " " + target(el)) &&
      !/mydl=|\/account\/|\/help\/|#/.test(target(el)) &&
      !/downloaded/i.test(label(el)));
    if (!button) return send({ error: "no-download-button" });

    const url = new URL(target(button), detailUrl);
    const res = button.tagName === "FORM"
      ? await fetch(url, { method: (button.getAttribute("method") || "POST").toUpperCase(), body: new FormData(button), credentials: "include" })
      : await fetch(url, { credentials: "include" });
    if (!res.ok) return send({ error: "HTTP " + res.status });
    if (/text\/html/i.test(res.headers.get("content-type") || "")) return send({ error: "not-a-file", url: res.url });

    const bytes = new Uint8Array(await res.arrayBuffer());
    let binary = "";
    for (let i = 0; i < bytes.length; i += 0x8000) binary += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    send({ data: btoa(binary) });
  } catch (e) {
    send({ error: String(e) });
  }
})
)JS";

    juce::String extensionFor (const juce::MemoryBlock& data)
    {
        const auto* b = static_cast<const char*> (data.getData());
        const auto starts = [&] (const char* magic) { return data.getSize() >= 4 && std::memcmp (b, magic, std::strlen (magic)) == 0; };

        if (starts ("RIFF")) return ".wav";
        if (starts ("FORM")) return ".aif";
        if (starts ("fLaC")) return ".flac";
        if (starts ("OggS")) return ".ogg";
        if (starts ("ID3") || (data.getSize() > 2 && (unsigned char) b[0] == 0xff && ((unsigned char) b[1] & 0xe0) == 0xe0)) return ".mp3";
        return {};
    }
}

//==============================================================================
class LoopermanView::Browser final : public juce::WebBrowserComponent
{
public:
    Browser (const Options& options, std::function<void (const juce::String&)> onLoaded)
        : WebBrowserComponent (options), loaded (std::move (onLoaded)) {}

    void pageFinishedLoading (const juce::String& url) override { loaded (url); }
    void newWindowAttemptingToLoad (const juce::String& url) override { goToURL (url); }

    // WebView2 reports "Error code: 9" when one navigation interrupts another. JUCE's default reaction is
    // to navigate to an error page, which interrupts the page we actually want, so ignore these.
    bool pageLoadHadNetworkError (const juce::String&) override { return false; }

    // Both browsers share this folder, so a sign-in in the popup is the session the downloads use.
    static Options baseOptions()
    {
        const auto dataFolder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                    .getChildFile ("freeloops").getChildFile ("webview2-looperman");
        return Options{}
            .withBackend (Options::Backend::webview2)
            .withWinWebView2Options (Options::WinWebView2{}.withUserDataFolder (dataFolder).withStatusBarDisabled());
    }

private:
    std::function<void (const juce::String&)> loaded;
};

//==============================================================================
class LoopermanView::SignInWindow final : public juce::DocumentWindow
{
public:
    explicit SignInWindow (LoopermanView& o)
        : DocumentWindow ("Sign in to Looperman", juce::Colour (0xff1d2024), DocumentWindow::closeButton), owner (o)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new Content (o), false);
        setResizable (true, false);
        setSize (560, 720);
        setCentrePosition (o.getTopLevelComponent()->getScreenBounds().getCentre());
        setAlwaysOnTop (true); // otherwise it can open behind FL Studio
        setVisible (true);
        toFront (true);
    }

    juce::WebBrowserComponent& getBrowser() { return *static_cast<Content*> (getContentComponent())->browser; }

    void closeButtonPressed() override { owner.finishSignIn(); }

private:
    struct Content final : public juce::Component
    {
        explicit Content (LoopermanView& owner)
        {
            hint.setText ("Your password goes only to looperman.com. freeloops never sees it.", juce::dontSendNotification);
            hint.setColour (juce::Label::textColourId, juce::Colour (0xffb7bcc4));
            hint.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (hint);

            browser = std::make_unique<Browser> (Browser::baseOptions(), [&owner] (const juce::String& url) { owner.signInPageLoaded (url); });
            addAndMakeVisible (*browser);
            browser->goToURL (loginUrl);
            setSize (560, 720);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1d2024)); }

        void resized() override
        {
            auto area = getLocalBounds();
            hint.setBounds (area.removeFromTop (32));
            browser->setBounds (area);
        }

        juce::Label hint;
        std::unique_ptr<Browser> browser;
    };

    LoopermanView& owner;
};

//==============================================================================
LoopermanView::LoopermanView (LoopermanClient& c) : client (c)
{
    const auto options = Browser::baseOptions()
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withEventListener ("freeloopsDownload", [this] (juce::var result) { downloadFinished (result); });

    browser = std::make_unique<Browser> (options, [this] (const juce::String& url) { pageLoaded (url); });
    addAndMakeVisible (*browser);
    browser->goToURL (home);
}

LoopermanView::~LoopermanView()
{
    signInWindow.reset();
    for (auto& p : queue)
        p.callback ({}, "closed");
}

//==============================================================================
void LoopermanView::showSignIn()
{
    if (signInWindow != nullptr)
    {
        signInWindow->toFront (true);
        return;
    }

    signInWindow = std::make_unique<SignInWindow> (*this);
    notify();
}

void LoopermanView::signInPageLoaded (const juce::String& url)
{
    // The login page itself proves nothing; wait for wherever Looperman sends the user after it.
    if (signInWindow == nullptr || ! url.startsWith ("https://www.looperman.com") || url.contains ("/account/login"))
        return;

    juce::Component::SafePointer<LoopermanView> safe (this);
    signInWindow->getBrowser().evaluateJavascript (signedInCheck, [safe] (juce::WebBrowserComponent::EvaluationResult result)
    {
        const auto* value = result.getResult();
        if (safe != nullptr && value != nullptr && (bool) *value)
            safe->finishSignIn();
    });
}

void LoopermanView::finishSignIn()
{
    // Close the popup from outside its own callbacks, then reload the session page to read the state.
    juce::Component::SafePointer<LoopermanView> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe == nullptr || safe->signInWindow == nullptr)
            return;

        safe->signInWindow.reset();
        safe->browser->goToURL (home);
        safe->notify();
    });
}

void LoopermanView::signOut()
{
    signedIn = false;
    browser->goToURL (logoutUrl);
    notify();
}

void LoopermanView::pageLoaded (const juce::String& url)
{
    pageReady = url.startsWith ("https://www.looperman.com");
    if (! pageReady || url.contains ("/account/login"))
        return;

    juce::Component::SafePointer<LoopermanView> safe (this);
    browser->evaluateJavascript (signedInCheck, [safe] (juce::WebBrowserComponent::EvaluationResult result)
    {
        if (safe == nullptr)
            return;

        const auto* value = result.getResult();
        safe->signedIn = value != nullptr && (bool) *value;
        safe->notify();
        safe->startNextDownload();
    });
}

void LoopermanView::notify()
{
    if (onStateChange != nullptr)
        onStateChange();
}

//==============================================================================
void LoopermanView::download (const juce::String& id, bool keep, DownloadCallback callback)
{
    if (client.find (id) == nullptr)
    {
        callback ({}, "unknown loop");
        return;
    }

    if (const auto kept = client.findDownload (id); kept.existsAsFile())
    {
        callback (kept, {});
        return;
    }

    // Already auditioned: no new request. Keeping it moves it out of the cache.
    if (const auto cached = client.findCached (id); cached.existsAsFile())
    {
        if (! keep)
        {
            callback (cached, {});
            return;
        }

        const auto target = client.pathFor (id, true).withFileExtension (cached.getFileExtension());
        target.getParentDirectory().createDirectory();
        if (cached.moveFileTo (target))
            callback (target, {});
        else
            callback ({}, "couldn't save the file");
        return;
    }

    // Already queued (say, previewed and then dragged): one download serves both.
    for (auto& p : queue)
    {
        if (p.id != id)
            continue;

        p.keep = p.keep || keep;
        p.callback = [first = std::move (p.callback), second = std::move (callback)] (juce::File f, juce::String e)
        {
            first (f, e);
            second (f, e);
        };
        return;
    }

    queue.push_back ({ id, keep, std::move (callback) });
    startNextDownload();
}

void LoopermanView::startNextDownload()
{
    if (downloading || queue.empty() || ! pageReady || isSigningIn())
        return;

    if (! signedIn)
    {
        while (! queue.empty())
            finishFront ({}, "signed-out");
        return;
    }

    const auto* loop = client.find (queue.front().id);
    if (loop == nullptr)
    {
        finishFront ({}, "unknown loop");
        return;
    }

    downloading = true;
    startTimer (downloadTimeoutMs);
    browser->evaluateJavascript (juce::String (downloadScript) + "(" + juce::JSON::toString (loop->id) + ", "
                                 + juce::JSON::toString (loop->detailUrl) + ")");
}

void LoopermanView::downloadFinished (const juce::var& result)
{
    // Only the download we started is accepted; anything else the page emits is ignored.
    if (! downloading || queue.empty() || result["id"].toString() != queue.front().id)
        return;

    stopTimer();

    if (result.hasProperty ("error"))
    {
        finishFront ({}, result["error"].toString());
        return;
    }

    const auto base64 = result["data"].toString();
    juce::MemoryOutputStream decoded;
    if ((size_t) base64.length() > maxDownloadBytes * 4 / 3 || ! juce::Base64::convertFromBase64 (decoded, base64))
    {
        finishFront ({}, "bad-data");
        return;
    }

    const auto data = decoded.getMemoryBlock();
    const auto extension = extensionFor (data);
    if (extension.isEmpty())
    {
        finishFront ({}, data.getSize() >= 2 && std::memcmp (data.getData(), "PK", 2) == 0 ? "zip-not-supported" : "unknown-format");
        return;
    }

    const auto keep = queue.front().keep;
    const auto file = client.pathFor (queue.front().id, keep).withFileExtension (extension);
    const auto part = file.getSiblingFile (file.getFileName() + ".part");
    file.getParentDirectory().createDirectory();

    if (! (part.replaceWithData (data.getData(), data.getSize()) && part.moveFileTo (file)))
    {
        finishFront ({}, "couldn't save the file");
        return;
    }

    if (! keep)
        LoopermanClient::trimAuditionCache();

    finishFront (file, {});
}

void LoopermanView::finishFront (const juce::File& file, const juce::String& error)
{
    if (queue.empty())
        return;

    auto pending = std::move (queue.front());
    queue.pop_front();
    downloading = false;
    stopTimer();

    pending.callback (file, error);
    startNextDownload();
}

void LoopermanView::timerCallback()
{
    finishFront ({}, "timed out");
}

//==============================================================================
void LoopermanView::resized()
{
    browser->setBounds (getLocalBounds());
}
