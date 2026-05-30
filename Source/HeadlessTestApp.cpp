#include <JuceHeader.h>
#include "PluginProcessor.h"

/** Parse a command-line option value from a token array. */
static juce::String getOption(const juce::StringArray& tokens, const juce::String& key, const juce::String& def = {})
{
    for (int i = 0; i + 1 < tokens.size(); ++i)
        if (tokens[i] == key)
            return tokens[i + 1];
    return def;
}

static bool hasOption(const juce::StringArray& tokens, const juce::String& key)
{
    return tokens.contains(key);
}

static void writeWav(juce::AudioFormatManager& fmtMgr,
                     const juce::File& file,
                     const juce::AudioBuffer<float>& buffer,
                     double sampleRate)
{
    if (file.exists())
        file.deleteFile();

    auto outStream = std::make_unique<juce::FileOutputStream>(file);

    auto* fmt = fmtMgr.findFormatForFileExtension("wav");
    jassert(fmt != nullptr);

    std::unique_ptr<juce::AudioFormatWriter> writer(
        fmt->createWriterFor(outStream.release(),
                             sampleRate,
                             (unsigned int)buffer.getNumChannels(),
                             24, {}, 0));
    jassert(writer != nullptr);

    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer->flush();
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto tokens = juce::StringArray(argv, argc);

    auto inputPath  = getOption(tokens, "--input");
    auto outputPath = getOption(tokens, "--output");
    auto paramsPath = getOption(tokens, "--params");
    auto saveStatePath = getOption(tokens, "--save-state");

    if (inputPath.isEmpty() || outputPath.isEmpty())
    {
        std::cerr << "Usage: EQoonHeadless --input <in.wav> --output <out.wav>\n"
                  << "       [--params <state.xml>] [--set <paramID value>]...\n"
                  << "       [--save-state <out.xml>] [--duration <sec>] [--bypass]\n";
        return 1;
    }

    // --- Read input WAV ---------------------------------------------------
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto inFile = juce::File(inputPath);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(inFile));
    if (reader == nullptr)
    {
        std::cerr << "ERROR: Could not open input file: " << inputPath << "\n";
        return 1;
    }

    auto srcChannels = (int) reader->numChannels;
    auto totalSamples = (int) reader->lengthInSamples;
    double sampleRate = reader->sampleRate;

    juce::AudioBuffer<float> rawBuffer(srcChannels, totalSamples);
    reader->read(&rawBuffer, 0, totalSamples, 0, true, true);
    reader.reset();

    // The plugin expects stereo (2 channels).  Convert mono → stereo if needed.
    int numChannels = 2;
    juce::AudioBuffer<float> inputBuffer(numChannels, totalSamples);
    inputBuffer.clear();
    for (int ch = 0; ch < juce::jmin(srcChannels, numChannels); ++ch)
        inputBuffer.copyFrom(ch, 0, rawBuffer, ch, 0, totalSamples);
    if (srcChannels == 1)
        inputBuffer.copyFrom(1, 0, rawBuffer, 0, 0, totalSamples);  // mono → both channels

    int numSamples = totalSamples;
    int blockSize = juce::jmin(512, numSamples);

    auto durationStr = getOption(tokens, "--duration");
    if (durationStr.isNotEmpty())
    {
        int maxSamples = (int)(durationStr.getDoubleValue() * sampleRate);
        if (maxSamples < numSamples)
            numSamples = maxSamples;
    }

    // --- Create & prepare processor ---------------------------------------
    EQoonAudioProcessor proc;
    proc.setRateAndBufferSizeDetails(sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // --- Load state from XML file -----------------------------------------
    if (paramsPath.isNotEmpty())
    {
        auto stateFile = juce::File(paramsPath);
        if (stateFile.existsAsFile())
        {
            auto xml = juce::XmlDocument::parse(stateFile);
            if (xml != nullptr)
            {
                auto vt = juce::ValueTree::fromXml(*xml);
                if (vt.isValid())
                    proc.apvts.replaceState(vt);
            }
        }
        proc.updateFilters();
    }

    // --- Apply --set overrides --------------------------------------------
    for (int i = 0; i + 2 < tokens.size(); ++i)
    {
        if (tokens[i] == "--set")
        {
            auto paramID = tokens[i + 1];
            float value  = tokens[i + 2].getFloatValue();
            if (auto* rp = proc.apvts.getRawParameterValue(paramID))
                *rp = value;
            i += 2;
        }
    }
    if (hasOption(tokens, "--set"))
        proc.updateFilters();

    if (hasOption(tokens, "--bypass"))
    {
        *proc.apvts.getRawParameterValue("Bypass") = 1.0f;
        proc.updateFilters();
    }

    // --- Process audio ----------------------------------------------------
    juce::AudioBuffer<float> outputBuffer(numChannels, numSamples);
    outputBuffer.clear();

    int processed = 0;
    while (processed < numSamples)
    {
        int block = juce::jmin(blockSize, numSamples - processed);
        juce::AudioBuffer<float> blockBuffer(numChannels, block);
        blockBuffer.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            blockBuffer.copyFrom(ch, 0, inputBuffer, ch, processed, block);

        juce::MidiBuffer midi;
        proc.processBlock(blockBuffer, midi);

        for (int ch = 0; ch < numChannels; ++ch)
            outputBuffer.copyFrom(ch, processed, blockBuffer, ch, 0, block);

        processed += block;
    }
    // --- Write output WAV -------------------------------------------------
    writeWav(formatManager, juce::File(outputPath), outputBuffer, sampleRate);

    // --- Print stats for automated analysis -------------------------------
    double rms = 0.0;
    float peak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
        {
            float sample = outputBuffer.getSample(ch, s);
            rms  += (double)(sample * sample);
            peak  = juce::jmax(peak, std::abs(sample));
        }

    rms = std::sqrt(rms / (numChannels * numSamples));
    float peakDb = juce::Decibels::gainToDecibels(peak);
    float rmsDb  = juce::Decibels::gainToDecibels((float)rms);

    std::cout << "STATS"
              << " peak=" << peak
              << " peak_db=" << peakDb
              << " rms=" << rms
              << " rms_db=" << rmsDb
              << " samples=" << numSamples
              << " rate=" << (int)sampleRate
              << " channels=" << numChannels
              << " input=" << inputPath
              << " output=" << outputPath
              << "\n";

    // --- Save APVTS state to XML ------------------------------------------
    if (saveStatePath.isNotEmpty())
    {
        auto stateXml = proc.apvts.copyState().toXmlString();
        auto stateFile = juce::File(saveStatePath);
        stateFile.replaceWithText(stateXml);
        std::cout << "STATE_SAVED path=" << saveStatePath << "\n";
    }

    proc.releaseResources();
    return 0;
}
