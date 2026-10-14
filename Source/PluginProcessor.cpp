#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

DoodleSynthProcessor::DoodleSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    setStateJson (juce::String::fromUTF8 (BinaryData::default_json, BinaryData::default_jsonSize));
    startTimerHz (4);
}

bool DoodleSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DoodleSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    keyboardState.reset();
    // rebuild the reverb tail at the new sample rate
    float decay;
    {
        const juce::ScopedLock sl (stateLock);
        auto v = juce::JSON::parse (stateJson);
        decay = (float) (double) v["fx"]["reverb"].getProperty ("decay", 1.8);
    }
    engine.updateReverbIR (decay);
}

void DoodleSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // host tempo + song position
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;
            playing = pos->getIsPlaying();
        }
    engine.setTransport (bpm, ppq, playing);

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    int done = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > done) { engine.render (L + done, R + done, at - done); done = at; }
        if (msg.isNoteOn()) engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) engine.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allNotesOff();
        else if (msg.isPitchWheel()) engine.setPitchBend ((msg.getPitchWheelValue() - 8192) / 8192.0f * 2.0f);
    }
    if (done < n) engine.render (L + done, R + done, n - done);
    engine.postProcess (buffer, n);
}

void DoodleSynthProcessor::setStateJson (const juce::String& json)
{
    auto v = juce::JSON::parse (json);
    if (! v.isObject()) return;
    {
        const juce::ScopedLock sl (stateLock);
        stateJson = json;
    }
    engine.setPending (ds::parseState (v));
    engine.updateReverbIR ((float) (double) v["fx"]["reverb"].getProperty ("decay", 1.8));
}

juce::String DoodleSynthProcessor::getStateJson() const
{
    const juce::ScopedLock sl (stateLock);
    return stateJson;
}

void DoodleSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream out (destData, false);
    out.writeString (getStateJson());
}

void DoodleSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
    auto json = in.readString();
    if (json.isEmpty()) return;
    setStateJson (json);
    juce::MessageManager::callAsync ([safe = juce::WeakReference<DoodleSynthProcessor> (this)]
    {
        if (safe != nullptr && safe->onStateRestored) safe->onStateRestored();
    });
}

juce::AudioProcessorEditor* DoodleSynthProcessor::createEditor()
{
    return new DoodleSynthEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DoodleSynthProcessor();
}
