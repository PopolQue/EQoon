#include <JuceHeader.h>
#include "PluginProcessor.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();
    return 0;
}
