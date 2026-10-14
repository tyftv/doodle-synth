#include "PluginEditor.h"
#include "BinaryData.h"

DoodleSynthEditor::DoodleSynthEditor (DoodleSynthProcessor& p)
    : AudioProcessorEditor (&p), proc (p), web (makeOptions())
{
    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (true, true);
    setResizeLimits (720, 480, 2560, 1800);
    setSize (1180, 860);
    proc.onStateRestored = [this] { if (uiReady) sendLoad(); };
    startTimerHz (30);
}

DoodleSynthEditor::~DoodleSynthEditor()
{
    proc.onStateRestored = nullptr;
}

void DoodleSynthEditor::resized()
{
    web.setBounds (getLocalBounds());
}

juce::WebBrowserComponent::Options DoodleSynthEditor::makeOptions()
{
    using Opt = juce::WebBrowserComponent::Options;
    return Opt {}
        .withBackend (Opt::Backend::webview2)
        .withWinWebView2Options (Opt::WinWebView2 {}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DoodleSynthWebView")))
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const juce::String& url) { return getResource (url); })
        .withEventListener ("ready", [this] (const juce::var&)
        {
            uiReady = true;
            sendLoad();
        })
        .withEventListener ("state", [this] (const juce::var& v)
        {
            auto json = v.getProperty ("json", juce::var()).toString();
            if (json.isNotEmpty()) proc.setStateJson (json);
        })
        .withEventListener ("note", [this] (const juce::var& v)
        {
            int n = juce::jlimit (0, 127, (int) v.getProperty ("n", 60));
            if ((bool) v.getProperty ("on", false))
                proc.keyboardState.noteOn (1, n, juce::jlimit (0.05f, 1.0f, (float) (double) v.getProperty ("v", 0.9)));
            else
                proc.keyboardState.noteOff (1, n, 0.0f);
        })
        .withEventListener ("panic", [this] (const juce::var&)
        {
            proc.keyboardState.allNotesOff (1);
        });
}

std::optional<juce::WebBrowserComponent::Resource> DoodleSynthEditor::getResource (const juce::String& url)
{
    auto path = url.fromFirstOccurrenceOf (juce::WebBrowserComponent::getResourceProviderRoot(), false, false);
    path = path.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/");
    if (path.isEmpty() || path == "index.html")
    {
        const auto* d = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        return juce::WebBrowserComponent::Resource { std::vector<std::byte> (d, d + BinaryData::index_htmlSize), "text/html" };
    }
    return std::nullopt;
}

void DoodleSynthEditor::sendLoad()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("json", proc.getStateJson());
    o->setProperty ("bpm", proc.engine.uiBpm.load());
    web.emitEventIfBrowserIsVisible ("load", juce::var (o));
}

void DoodleSynthEditor::timerCallback()
{
    if (! uiReady) return;
    auto& e = proc.engine;
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> mp, mv, sc;
    for (int i = 0; i < 3; ++i) { mp.add (e.modPos[i]); mv.add (e.modVal[i]); }
    // trigger-aligned snapshot of the output for the scope
    const int w = e.scopeW;
    int start = (w - 1024) & 2047;
    for (int i = 1; i < 512; ++i)
    {
        int a = (start + i - 1) & 2047, b = (start + i) & 2047;
        if (e.scope[a] < 0 && e.scope[b] >= 0) { start = b; break; }
    }
    for (int i = 0; i < 512; i += 2) sc.add (e.scope[(start + i) & 2047]);
    o->setProperty ("mp", mp);
    o->setProperty ("mv", mv);
    o->setProperty ("scope", sc);
    o->setProperty ("playing", e.playingFlag.load());
    o->setProperty ("bpm", e.uiBpm.load());
    web.emitEventIfBrowserIsVisible ("viz", juce::var (o));
}
