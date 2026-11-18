// Loads the built .vst3 like a DAW does and checks it end to end:
// metadata, bus layout, latency, parameters, audio processing, presets and state.
//
// Usage: PluginHostTest <path to SasariOsaeru.vst3>

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (100) == name)
            return prm;
    return nullptr;
}

void setParam (juce::AudioPluginInstance& p, const juce::String& name, float normalised)
{
    if (auto* prm = findParam (p, name))
        prm->setValueNotifyingHost (normalised);
    else
        check (false, "parameter exists: " + name);
}

double toneDb (const juce::AudioBuffer<float>& b, int ch, double sr, double freq, int from, int to)
{
    double re = 0, im = 0;
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    for (int i = from; i < to; ++i)
    {
        re += b.getSample (ch, i) * std::cos (w * i);
        im -= b.getSample (ch, i) * std::sin (w * i);
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / (to - from) + 1e-12);
}

/** 3 s of 500 Hz + loud 7 kHz through the plugin, returns the 7 kHz change in dB. */
double render (juce::AudioPluginInstance& p, double sr, int block, double* lowChange = nullptr)
{
    const int total = (int) (3 * sr);
    juce::AudioBuffer<float> in (2, total), out (2, total);
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr;
        const float v = (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 500 * t)
                                 + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 7000 * t));
        in.setSample (0, i, v);
        in.setSample (1, i, v);
    }
    out.makeCopyOf (in);
    p.reset();
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int len = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, len);
        p.processBlock (view, midi);
    }
    const int a = (int) (1.5 * sr), b = total;
    if (lowChange != nullptr)
        *lowChange = toneDb (out, 0, sr, 500, a, b) - toneDb (in, 0, sr, 500, a, b);
    return toneDb (out, 0, sr, 7000, a, b) - toneDb (in, 0, sr, 7000, a, b);
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    if (argc < 2)
    {
        std::printf ("usage: PluginHostTest <plugin.vst3>\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, path);
    check (types.size() == 1, "one plugin found in " + path);
    if (types.isEmpty())
        return 1;
    const auto& desc = *types[0];
    std::printf ("  plugin: %s by %s, category %s, version %s\n", desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(),
                 desc.category.toRawUTF8(), desc.version.toRawUTF8());
    check (! desc.isInstrument, "is an effect");

    juce::String err;
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    auto plugin = fm.createPluginInstance (desc, 48000, 256, err);
    check (plugin != nullptr, "instantiated " + err);
    if (plugin == nullptr)
        return 1;

    check (plugin->getTotalNumInputChannels() == 2 && plugin->getTotalNumOutputChannels() == 2, "stereo in/out");
    check (plugin->getParameters().size() >= 36, "parameters: " + juce::String (plugin->getParameters().size()));
    check (plugin->getNumPrograms() == 7, "7 factory presets");

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        plugin->releaseResources(); // hosts deactivate before changing the setup
        plugin->setPlayConfigDetails (2, 2, sr, 256);
        plugin->prepareToPlay (sr, 256);
        const int expected = sr <= 50000 ? 512 : 1024;
        check (plugin->getLatencySamples() == expected,
               "latency at " + juce::String ((int) sr) + " Hz = " + juce::String (plugin->getLatencySamples()));
    }

    // Prepare for the largest block used below (hosts must not exceed the prepared size).
    plugin->releaseResources();
    plugin->setPlayConfigDetails (2, 2, 48000, 512);
    plugin->prepareToPlay (48000, 512);

    // Strongest vocal setting, fixed reference: loud 7 kHz must drop, 500 Hz must stay.
    setParam (*plugin, "Reference", 0.0f);   // fixed estimate
    setParam (*plugin, "Selectivity", 0.0f);
    setParam (*plugin, "Guitar On", 0.0f);
    setParam (*plugin, "Hi-hat On", 0.0f);
    setParam (*plugin, "Vocal S Processing", 1.0f);
    setParam (*plugin, "Vocal S Intensity", 1.0f);
    double low = 0;
    const double hi = render (*plugin, 48000, 256, &low);
    std::printf ("  strongest vocal: 7 kHz %.1f dB, 500 Hz %.2f dB\n", hi, low);
    check (hi < -10.0, "loud 7 kHz reduced");
    check (std::abs (low) < 0.5, "500 Hz untouched");

    // Mix 0 must be transparent (after latency).
    setParam (*plugin, "Mix", 0.0f);
    const double hiMix0 = render (*plugin, 48000, 113);
    check (std::abs (hiMix0) < 0.2, "mix 0 transparent (" + juce::String (hiMix0, 2) + " dB)");
    setParam (*plugin, "Mix", 1.0f);

    // Range 6 dB caps the reduction.
    if (auto* r = findParam (*plugin, "Range"))
        r->setValueNotifyingHost (6.0f / 24.0f);
    const double hiRange = render (*plugin, 48000, 512);
    std::printf ("  range 6 dB: 7 kHz %.1f dB\n", hiRange);
    check (hiRange > -7.5 && hiRange < -4.0, "range caps reduction");

    // State round trip.
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    setParam (*plugin, "Vocal S Processing", 0.2f);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (findParam (*plugin, "Vocal S Processing")->getValue() - 1.0f) < 1e-4f, "state restores parameters");

    // Presets change parameters.
    plugin->setCurrentProgram (2); // hi-hats
    check (findParam (*plugin, "Hi-hat On")->getValue() > 0.5f, "preset applied");
    check (plugin->getProgramName (1).isNotEmpty(), "preset names");

    // Analysis range 400 Hz-16 kHz doubles the frame: the host must be told the new latency.
    setParam (*plugin, "Analysis Range", 1.0f);
    {
        juce::AudioBuffer<float> b (2, 512);
        b.clear();
        juce::MidiBuffer m;
        plugin->processBlock (b, m);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    }
    check (plugin->getLatencySamples() == 1024, "latency reported after switching the analysis range: "
                                                    + juce::String (plugin->getLatencySamples()));
    setParam (*plugin, "Analysis Range", 0.0f);
    {
        juce::AudioBuffer<float> b (2, 512);
        b.clear();
        juce::MidiBuffer m;
        plugin->processBlock (b, m);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    }
    check (plugin->getLatencySamples() == 512, "latency back to 512");

    // Custom target in the extended range reduces a loud 600 Hz tone (preset "Nasal mids").
    plugin->setCurrentProgram (6);
    check (findParam (*plugin, "Custom On")->getValue() > 0.5f && findParam (*plugin, "Analysis Range")->getValue() > 0.5f,
           "nasal preset enables the custom target and the extended range");

    // Defaults with auto level: processing a long block must stay finite.
    plugin->setCurrentProgram (0);
    setParam (*plugin, "Reference", 0.5f); // auto
    juce::AudioBuffer<float> noise (2, 48000 * 5);
    juce::Random rnd (1);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample (c, i, rnd.nextFloat() * 0.6f - 0.3f);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < noise.getNumSamples(); pos += 256)
    {
        juce::AudioBuffer<float> view (noise.getArrayOfWritePointers(), 2, pos, juce::jmin (256, noise.getNumSamples() - pos));
        plugin->processBlock (view, midi);
    }
    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            finite = finite && std::isfinite (noise.getSample (c, i));
    check (finite, "noise with auto level stays finite");

    check (plugin->hasEditor(), "has an editor");
    plugin->releaseResources();
    plugin.reset();

    std::printf ("%s (%d failures)\n", failures == 0 ? "ALL OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
