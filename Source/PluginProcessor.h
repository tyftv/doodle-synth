#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"

class DoodleSynthProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DoodleSynthProcessor();
    ~DoodleSynthProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Doodle Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // UI bridge (message thread)
    void setStateJson (const juce::String& json);
    juce::String getStateJson() const;
    std::function<void()> onStateRestored;   // editor reloads the faceplate when the DAW restores a session

    juce::MidiKeyboardState keyboardState;    // notes played on the faceplate keys
    ds::Engine engine;

private:
    void timerCallback() override { engine.collectGarbage(); }

    juce::String stateJson;
    juce::CriticalSection stateLock;

    JUCE_DECLARE_WEAK_REFERENCEABLE (DoodleSynthProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DoodleSynthProcessor)
};
