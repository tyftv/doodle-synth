#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// The faceplate is the same HTML page as the browser version, shown in a WebView.
// It talks to the engine with a few events:
//   UI -> plugin : "ready", "state" {json}, "note" {n,on,v}, "panic"
//   plugin -> UI : "load" {json,bpm}, "viz" {mp,mv,playing,bpm,scope}
class DoodleSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DoodleSynthEditor (DoodleSynthProcessor&);
    ~DoodleSynthEditor() override;

    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0b0b0d)); }

private:
    void timerCallback() override;
    void sendLoad();
    juce::WebBrowserComponent::Options makeOptions();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    DoodleSynthProcessor& proc;
    bool uiReady = false;
    juce::WebBrowserComponent web;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DoodleSynthEditor)
};
