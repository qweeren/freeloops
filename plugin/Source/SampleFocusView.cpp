// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SampleFocusView.h"

namespace
{
    constexpr auto origin = "https://samplefocus.com";
    constexpr auto home = "https://samplefocus.com/";
    constexpr auto loginUrl = "https://samplefocus.com/users/sign_in";
    constexpr auto userAgent = "freeloops/0.1 (open-source FL Studio sample browser)";
    constexpr double searchGapMs = 1500.0;   // never more than one search page every 1.5 s
    constexpr int jobTimeoutMs = 90 * 1000;
    constexpr size_t maxFileBytes = 200 * 1024 * 1024;

    // Cloudflare's "Just a moment..." check, Sample Focus's own test for a signed-in page
    // (Helpers.isLoggedIn), and the credit count it shows.
    constexpr auto sessionScript = R"JS(
(() => {
  const challenge = document.title.startsWith("Just a moment") || location.search.includes("__cf_chl")
                    || !!document.querySelector("#challenge-form, #challenge-stage");
  const account = document.querySelector(".account-dropdown");
  const shown = document.querySelector(".nav-credit-count");
  const credits = parseInt(account && account.dataset.credits != null ? account.dataset.credits : shown ? shown.textContent : "", 10);
  return { challenge, signedIn: !!account, credits: isNaN(credits) ? -1 : credits };
})()
)JS";

    // Runs inside the samplefocus.com page, so requests are the page's own: they pass Cloudflare with the
    // user's browser session, its CDN serves previews to it, and downloads carry the user's sign-in.
    // A download is the site's own request (POST <sample>/download, which spends a credit unless the user
    // already has the sample), then the file it points to. Files come back as base64, or as their URL if
    // the page isn't allowed to read them.
    const char* jobScript = R"JS(
(async (job, kind, url, pageUrl) => {
  const send = (o) => window.__JUCE__.backend.emitEvent("freeloopsSampleFocus", Object.assign({ job }, o));
  const challenged = (res) => res.headers.get("cf-mitigated") === "challenge";
  const encode = async (res) => {
    const bytes = new Uint8Array(await res.arrayBuffer());
    let binary = "";
    for (let i = 0; i < bytes.length; i += 0x8000) binary += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(binary);
  };
  try {
    if (kind !== "download") {
      const res = await fetch(url);
      if (challenged(res)) return send({ error: "challenge" });
      if (!res.ok) return send({ error: "HTTP " + res.status });
      return send(kind === "search" ? { html: await res.text() } : { data: await encode(res) });
    }

    const token = document.querySelector('meta[name="csrf-token"]');
    if (!document.querySelector(".account-dropdown") || !token) return send({ error: "signed-out" });

    const res = await fetch(pageUrl + "/download", {
      method: "POST", credentials: "include",
      headers: { "X-CSRF-Token": token.content, Accept: "application/json", "Content-Type": "application/json" },
    });
    if (challenged(res)) return send({ error: "challenge" });
    if (res.status === 401) return send({ error: "signed-out" });
    if (res.status === 402) return send({ error: "out-of-credits", credits: 0 });
    if (!res.ok) return send({ error: "HTTP " + res.status });

    const reply = await res.json();
    const credits = typeof reply.credits_remaining === "number" ? reply.credits_remaining : -1;
    if (!reply.download_url) return send({ error: "no-download-url", credits });

    let file;
    try {
      file = await fetch(reply.download_url, { credentials: "include" });
    } catch (e) {
      return send({ url: reply.download_url, credits }); // not readable from the page: fetched natively
    }
    if (!file.ok) return send({ error: "HTTP " + file.status, credits });
    if (/text\/html/i.test(file.headers.get("content-type") || "")) return send({ error: "not-a-file", credits });
    send({ data: await encode(file), credits });
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

    bool isSignInPage (const juce::String& url)
    {
        return url.contains ("/users/sign_in") || url.contains ("/users/sign_up") || url.contains ("/users/password");
    }

    juce::var errorResult (const juce::String& message)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("error", message);
        return juce::var (o);
    }
}

//==============================================================================
class SampleFocusView::Browser final : public juce::WebBrowserComponent
{
public:
    Browser (const Options& options, std::function<void (const juce::String&)> onLoaded)
        : WebBrowserComponent (options), loaded (std::move (onLoaded)) {}

    void pageFinishedLoading (const juce::String& url) override { loaded (url); }
    void newWindowAttemptingToLoad (const juce::String& url) override { goToURL (url); }

    // WebView2 reports "Error code: 9" when one navigation interrupts another; see LoopermanView.
    bool pageLoadHadNetworkError (const juce::String&) override { return false; }

    // Both browsers share this folder, so a sign-in (or a passed browser check) in the popup is the
    // session the hidden page uses.
    static Options baseOptions()
    {
        const auto dataFolder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                    .getChildFile ("freeloops").getChildFile ("webview2-samplefocus");
        return Options{}
            .withBackend (Options::Backend::webview2)
            .withWinWebView2Options (Options::WinWebView2{}.withUserDataFolder (dataFolder).withStatusBarDisabled());
    }

private:
    std::function<void (const juce::String&)> loaded;
};

//==============================================================================
class SampleFocusView::Popup final : public juce::DocumentWindow
{
public:
    Popup (SampleFocusView& o, const juce::String& title, const juce::String& url, const juce::String& hintText)
        : DocumentWindow (title, juce::Colour (0xff1d2024), DocumentWindow::closeButton), owner (o)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new Content (o, url, hintText), false);
        setResizable (true, false);
        setSize (560, 720);
        setCentrePosition (o.getTopLevelComponent()->getScreenBounds().getCentre());
        setAlwaysOnTop (true); // otherwise it can open behind FL Studio
        setVisible (true);
        toFront (true);
    }

    juce::WebBrowserComponent& getBrowser() { return *static_cast<Content*> (getContentComponent())->browser; }

    void closeButtonPressed() override { owner.closePopup(); }

private:
    struct Content final : public juce::Component
    {
        Content (SampleFocusView& owner, const juce::String& url, const juce::String& hintText)
        {
            hint.setText (hintText, juce::dontSendNotification);
            hint.setColour (juce::Label::textColourId, juce::Colour (0xffb7bcc4));
            hint.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (hint);

            browser = std::make_unique<Browser> (Browser::baseOptions(), [&owner] (const juce::String& u) { owner.popupPageLoaded (u); });
            addAndMakeVisible (*browser);
            browser->goToURL (url);
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

    SampleFocusView& owner;
};

//==============================================================================
SampleFocusView::SampleFocusView (SampleFocusClient& c) : client (c)
{
    const auto options = Browser::baseOptions()
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withEventListener ("freeloopsSampleFocus", [this] (juce::var result) { jobFinished (result); });

    browser = std::make_unique<Browser> (options, [this] (const juce::String& url) { pageLoaded (url); });
    addAndMakeVisible (*browser);
    browser->goToURL (home);
}

SampleFocusView::~SampleFocusView()
{
    popup.reset();
    files.removeAllJobs (true, 5000);
    for (auto& job : queue)
    {
        for (auto& cb : job.callbacks)
            cb ({}, "closed");
        for (auto& reply : job.replies)
            reply (errorResult ("closed"));
    }
}

//==============================================================================
void SampleFocusView::showSignIn()
{
    openPopup (false);
}

void SampleFocusView::showBrowserCheck()
{
    openPopup (true);
}

void SampleFocusView::openPopup (bool check)
{
    if (popup != nullptr)
    {
        popup->toFront (true);
        return;
    }

    popupIsCheck = check;
    popup = check ? std::make_unique<Popup> (*this, "Sample Focus", home, "Answer Sample Focus's browser check, then this window closes by itself.")
                  : std::make_unique<Popup> (*this, "Sign in to Sample Focus", loginUrl, "Your password goes only to samplefocus.com. freeloops never sees it.");
    notify();
}

void SampleFocusView::popupPageLoaded (const juce::String& url)
{
    // The login page itself proves nothing; wait for wherever Sample Focus sends the user after it.
    if (popup == nullptr || ! url.startsWith (origin) || isSignInPage (url))
        return;

    juce::Component::SafePointer<SampleFocusView> safe (this);
    readSession (popup->getBrowser(), [safe] (const juce::var& session)
    {
        if (safe != nullptr && ! (bool) session["challenge"] && (safe->popupIsCheck || (bool) session["signedIn"]))
            safe->closePopup();
    });
}

void SampleFocusView::closePopup()
{
    // Close the popup from outside its own callbacks, then reload the session page to read the state.
    juce::Component::SafePointer<SampleFocusView> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe == nullptr || safe->popup == nullptr)
            return;

        safe->popup.reset();
        safe->browser->goToURL (home);
        safe->notify();
    });
}

void SampleFocusView::signOut()
{
    // Sample Focus signs out with DELETE /users/sign_out and the page's CSRF token, then goes home.
    signedIn = false;
    credits = -1;
    browser->evaluateJavascript (R"JS(
(() => {
  const token = document.querySelector('meta[name="csrf-token"]');
  fetch("/users/sign_out", { method: "DELETE", credentials: "include",
                             headers: { "X-CSRF-Token": token ? token.content : "", Accept: "application/json" } })
    .finally(() => { location.href = "/"; });
})()
)JS");
    notify();
}

void SampleFocusView::pageLoaded (const juce::String& url)
{
    pageReady = false;
    if (! url.startsWith (origin))
        return;

    juce::Component::SafePointer<SampleFocusView> safe (this);
    readSession (*browser, [safe] (const juce::var& session)
    {
        if (safe == nullptr)
            return;

        safe->challenged = (bool) session["challenge"];
        safe->signedIn = ! safe->challenged && (bool) session["signedIn"];
        safe->credits = safe->signedIn ? (int) session["credits"] : -1;
        safe->pageReady = ! safe->challenged;
        safe->notify();

        if (safe->challenged)
            safe->failAll ("challenge"); // nothing gets through until the user answers the check
        else
            safe->startNextJob();
    });
}

void SampleFocusView::readSession (juce::WebBrowserComponent& page, std::function<void (const juce::var&)> callback)
{
    page.evaluateJavascript (sessionScript, [callback] (juce::WebBrowserComponent::EvaluationResult result)
    {
        const auto* value = result.getResult();
        callback (value != nullptr ? *value : juce::var());
    });
}

void SampleFocusView::notify()
{
    if (onStateChange != nullptr)
        onStateChange();
}

//==============================================================================
void SampleFocusView::search (const juce::String& query, int page, const juce::var& filters, SearchCallback callback)
{
    page = juce::jmax (1, page);

    // Without search words Sample Focus shows its home page, not results: nothing to ask for.
    if (query.trim().isEmpty())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("samples", juce::Array<juce::var>());
        o->setProperty ("page", page);
        o->setProperty ("hasMore", false);
        callback (juce::var (o));
        return;
    }

    const auto url = SampleFocusClient::searchUrl (query, page, filters);
    if (const auto cached = client.cachedResults (url, page); ! cached.isVoid())
    {
        callback (cached);
        return;
    }

    dropQueued (Kind::search, url); // typing replaces the search still waiting for its turn
    enqueue (Kind::search, url, page, {}, std::move (callback));
}

void SampleFocusView::fetchPreview (const juce::String& id, FileCallback callback)
{
    if (client.find (id) == nullptr)
    {
        callback ({}, "unknown sample");
        return;
    }

    for (const auto& f : { client.findPreview (id), client.findDownload (id) })
    {
        if (f.existsAsFile())
        {
            f.setLastModificationTime (juce::Time::getCurrentTime()); // keeps it out of the next trim
            callback (f, {});
            return;
        }
    }

    dropQueued (Kind::preview, id); // auditioning down a list shouldn't fetch every row passed on the way
    enqueue (Kind::preview, id, 1, std::move (callback), {});
}

void SampleFocusView::download (const juce::String& id, FileCallback callback)
{
    if (client.find (id) == nullptr)
    {
        callback ({}, "unknown sample");
        return;
    }

    if (const auto kept = client.findDownload (id); kept.existsAsFile())
    {
        callback (kept, {});
        return;
    }

    enqueue (Kind::download, id, 1, std::move (callback), {}); // fails once the page has loaded if the user isn't signed in
}

void SampleFocusView::enqueue (Kind kind, const juce::String& key, int page, FileCallback callback, SearchCallback reply)
{
    // Already queued (say, dragged twice): one request serves both.
    auto job = std::find_if (queue.begin(), queue.end(), [&] (const Job& j) { return j.kind == kind && j.key == key; });
    if (job == queue.end())
        job = queue.insert (queue.end(), Job { kind, key, page, {}, {} });

    if (callback != nullptr)
        job->callbacks.push_back (std::move (callback));
    if (reply != nullptr)
        job->replies.push_back (std::move (reply));

    startNextJob();
}

void SampleFocusView::dropQueued (Kind kind, const juce::String& except)
{
    for (auto it = queue.begin(); it != queue.end();)
    {
        if ((busy && it == queue.begin()) || it->kind != kind || it->key == except)
        {
            ++it;
            continue;
        }

        auto dropped = std::move (*it);
        it = queue.erase (it);
        for (auto& cb : dropped.callbacks)
            cb ({}, "superseded");
        for (auto& reply : dropped.replies)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("superseded", true);
            reply (juce::var (o));
        }
    }
}

void SampleFocusView::startNextJob()
{
    if (busy || waitingForGap || queue.empty() || ! pageReady || popup != nullptr)
        return;

    const auto& job = queue.front();
    juce::String url, pageUrl;

    if (job.kind == Kind::search)
    {
        const auto wait = lastSearchMs + searchGapMs - juce::Time::getMillisecondCounterHiRes();
        if (wait > 0)
        {
            waitingForGap = true;
            juce::Component::SafePointer<SampleFocusView> safe (this);
            juce::Timer::callAfterDelay ((int) wait + 1, [safe]
            {
                if (safe == nullptr)
                    return;

                safe->waitingForGap = false;
                safe->startNextJob();
            });
            return;
        }

        lastSearchMs = juce::Time::getMillisecondCounterHiRes();
        url = job.key;
    }
    else
    {
        const auto* sample = client.find (job.key);
        if (sample == nullptr)
        {
            finishFront ({}, "unknown sample");
            return;
        }

        if (job.kind == Kind::download && ! signedIn)
        {
            finishFront ({}, "signed-out");
            return;
        }

        url = sample->previewUrl;
        if (job.kind == Kind::download)
            pageUrl = sample->pageUrl;
    }

    const auto kind = job.kind == Kind::search ? "search" : job.kind == Kind::preview ? "preview" : "download";
    busy = true;
    ++jobNumber;
    startTimer (jobTimeoutMs);
    browser->evaluateJavascript (juce::String (jobScript) + "(" + juce::String (jobNumber) + ", \"" + kind + "\", "
                                 + juce::JSON::toString (url) + ", " + juce::JSON::toString (pageUrl) + ")");
}

void SampleFocusView::jobFinished (const juce::var& result)
{
    // Only the job we started is accepted; anything else the page emits is ignored.
    if (! busy || queue.empty() || (int) result["job"] != jobNumber)
        return;

    const auto& job = queue.front();

    if (job.kind == Kind::download && result.hasProperty ("credits") && (int) result["credits"] >= 0)
    {
        credits = (int) result["credits"];
        notify();
    }

    if (result["error"] == "challenge")
    {
        // Cloudflare wants a browser check: reload the page to show it, and wait for the user to answer it.
        challenged = true;
        pageReady = false;
        notify();
        failAll ("challenge");
        browser->goToURL (home);
        return;
    }

    if (result.hasProperty ("error"))
    {
        finishFront ({}, result["error"].toString());
        return;
    }

    if (job.kind == Kind::search)
    {
        finishFront ({}, {}, client.addResults (job.key, job.page, result["html"].toString()));
        return;
    }

    if (result.hasProperty ("url"))
    {
        fetchDirectly (result["url"].toString());
        return;
    }

    const auto base64 = result["data"].toString();
    juce::MemoryOutputStream decoded;
    if ((size_t) base64.length() > maxFileBytes * 4 / 3 || ! juce::Base64::convertFromBase64 (decoded, base64))
    {
        finishFront ({}, "bad-data");
        return;
    }

    save (decoded.getMemoryBlock());
}

// The download link Sample Focus handed the page, when the page itself may not read it.
void SampleFocusView::fetchDirectly (const juce::String& url)
{
    const auto job = jobNumber;
    juce::Component::SafePointer<SampleFocusView> safe (this);
    files.addJob ([safe, job, url]
    {
        int status = 0;
        juce::MemoryBlock data;
        auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                             .withExtraHeaders (juce::String ("User-Agent: ") + userAgent)
                                                             .withConnectionTimeoutMs (15000)
                                                             .withNumRedirectsToFollow (5)
                                                             .withStatusCode (&status));
        const auto ok = stream != nullptr && status < 400 && stream->readIntoMemoryBlock (data, (juce::int64) maxFileBytes) > 0;

        juce::MessageManager::callAsync ([safe, job, ok, status, data = std::move (data)]
        {
            if (safe == nullptr || ! safe->busy || safe->jobNumber != job)
                return;

            if (ok)
                safe->save (data);
            else
                safe->finishFront ({}, status > 0 ? "HTTP " + juce::String (status) : "Couldn't reach Sample Focus");
        });
    });
}

void SampleFocusView::save (const juce::MemoryBlock& data)
{
    const auto extension = extensionFor (data);
    if (extension.isEmpty())
    {
        finishFront ({}, data.getSize() >= 2 && std::memcmp (data.getData(), "PK", 2) == 0 ? "zip-not-supported" : "unknown-format");
        return;
    }

    const auto& job = queue.front();
    const auto isDownload = job.kind == Kind::download;
    const auto file = (isDownload ? client.downloadPathFor (job.key) : client.previewPathFor (job.key)).withFileExtension (extension);
    const auto part = file.getSiblingFile (file.getFileName() + ".part");
    file.getParentDirectory().createDirectory();

    if (! (part.replaceWithData (data.getData(), data.getSize()) && part.moveFileTo (file)))
    {
        finishFront ({}, "couldn't save the file");
        return;
    }

    if (! isDownload)
        SampleFocusClient::trimPreviewCache();

    finishFront (file, {});
}

void SampleFocusView::finishFront (const juce::File& file, const juce::String& error, const juce::var& reply)
{
    if (queue.empty())
        return;

    auto job = std::move (queue.front());
    queue.pop_front();
    busy = false;
    stopTimer();

    for (auto& cb : job.callbacks)
        cb (file, error);
    for (auto& r : job.replies)
        r (reply.isVoid() ? errorResult (error) : reply);

    startNextJob();
}

void SampleFocusView::failAll (const juce::String& error)
{
    while (! queue.empty())
        finishFront ({}, error);
}

void SampleFocusView::timerCallback()
{
    finishFront ({}, "timed out");
}

//==============================================================================
void SampleFocusView::resized()
{
    browser->setBounds (getLocalBounds());
}
