/* Copyright 2026 Maddie Lim
 *
 * Kitty Advance Plugin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Kitty Advance Plugin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with  Kitty Advance Plugin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

const uint32_t s4aMidiInputAddr = 0x3007002;
const uint32_t s4aMidiInputSizeAddr = 0x3007000;
const uint32_t s4aMidiInputSize = 0x200;

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
{
    midiptr = s4aMidiInputAddr;
    midiptrlimit = midiptr + s4aMidiInputSize - 6;

    VFile *vf = VFileFromConstMemory(BinaryData::pluginbridge_gba, BinaryData::pluginbridge_gbaSize);
    core = mCoreFindVF(vf);
    core->init(core);
    core->setAVStream(core, &stream);
    mCoreInitConfig(core, NULL);
    
    mCoreOptions opts = {
      .skipBios = true,
      .useBios = false,
      .sampleRate = 32768,
      .volume = 0x100,
    };
    core->setAudioBufferSize(core, 2048);
    mAudioBuffer* mbuf = core->getAudioBuffer(core);  
    mAudioBufferInit(&maBuffer, 2048, 2);
    mAudioResamplerInit(&maResampler, mINTERPOLATOR_SINC);
    mAudioResamplerSetSource(&maResampler, mbuf, 131072, true);
    mAudioResamplerSetDestination(&maResampler, &maBuffer, 44100);
    mCoreConfigLoadDefaults(&core->config, &opts);
    core->loadROM(core, vf);
    core->reset(core);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
//     juce::ignoreUnused (sampleRate, samplesPerBlock);
// #ifdef ENABLE_GBA
//   core->setAudioBufferSize(core, samplesPerBlock);
// #endif
    core->setAudioBufferSize(core, samplesPerBlock / 3);
    mAudioResamplerSetDestination(&maResampler, &maBuffer, sampleRate);
}

void AudioPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    // TODO: realtime safety
    // we're touching core in realtime thread and main thread.. which is not great
    int16_t buf[16384];
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;


    size_t bufSize = static_cast<size_t>(buffer.getNumSamples());
    size_t remaining = bufSize;
    
    auto* lptr = buffer.getWritePointer(0);
    auto* rptr = buffer.getWritePointer(1);
    size_t pos = 0;    
    uint8_t run = 0;

    for (const auto &msg : midiMessages) {
      if (midiptr > midiptrlimit) {
        break;
      }
      auto mmsg = msg.getMessage();
      if (mmsg.isNoteOn()) {
        uint8_t status = 0x90 + mmsg.getChannel() - 1;
        if (status != run) {
          core->rawWrite8(core, midiptr++, 0, status);
          run = status;
        }
        core->rawWrite8(core, midiptr++, 0, mmsg.getNoteNumber());
        core->rawWrite8(core, midiptr++, 0, mmsg.getVelocity());
      } else if (mmsg.isNoteOff()) {
        uint8_t status = 0x80 + mmsg.getChannel() - 1;
        if (status != run) {
          core->rawWrite8(core, midiptr++, 0, status);
          run = status;
        }
        core->rawWrite8(core, midiptr++, 0, mmsg.getNoteNumber());
        core->rawWrite8(core, midiptr++, 0, mmsg.getVelocity());
      } else if (mmsg.isProgramChange()) {
        uint8_t status = 0xC0 + mmsg.getChannel() - 1;
        if (status != run) {
          core->rawWrite8(core, midiptr++, 0, status);
          run = status;
        }
        core->rawWrite8(core, midiptr++, 0, mmsg.getProgramChangeNumber());
      } else if (mmsg.isController()) {
        if (mmsg.getControllerNumber() == 119) {
          // fake program change
          uint8_t status = 0xC0 + mmsg.getChannel() - 1;
          if (status != run) {
            core->rawWrite8(core, midiptr++, 0, status);
            run = status;
          }
          core->rawWrite8(core, midiptr++, 0, mmsg.getControllerValue());          
        } else {
          uint8_t status = 0xB0 + mmsg.getChannel() - 1;
          if (status != run) {
            core->rawWrite8(core, midiptr++, 0, status);
            run = status;
          }
          core->rawWrite8(core, midiptr++, 0, mmsg.getControllerNumber());
          core->rawWrite8(core, midiptr++, 0, mmsg.getControllerValue());
        }
      } else if (mmsg.isPitchWheel()) {
        uint8_t status = 0xE0 + mmsg.getChannel() - 1;
        if (status != run) {
          core->rawWrite8(core, midiptr++, 0, status);
          run = status;
        }
        uint16_t wheel = mmsg.getPitchWheelValue();
        core->rawWrite8(core, midiptr++, 0, wheel & 0x7F);
        core->rawWrite8(core, midiptr++, 0, wheel >> 7);
      }
    }
    

    while (remaining) {

    while (!mAudioBufferAvailable(&maBuffer)) {
      core->rawWrite8(core, midiptr++, 0, 0xFF);
      core->rawWrite8(core, midiptr++, 0, 0x2F);
      core->rawWrite8(core, midiptr++, 0, 0);
      size_t midiSize = midiptr - s4aMidiInputAddr;
      core->rawWrite16(core, s4aMidiInputSizeAddr, 0, midiSize);
      core->runFrame(core);
      midiptr = s4aMidiInputAddr;
      mAudioResamplerProcess(&maResampler);
    }
    
      size_t processed = mAudioBufferRead(&maBuffer, buf, remaining);
      if (processed) {
        for (size_t i = 0; i < processed; ++i) {
          lptr[pos] = buf[i * 2] / 32768.0f;
          rptr[pos] = buf[i * 2 + 1] / 32768.0f;
          pos++;
        }
        remaining -= processed;
      } 
    }
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::ignoreUnused (destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}

mCore* AudioPluginAudioProcessor::getCore()
{
    return core;
}
