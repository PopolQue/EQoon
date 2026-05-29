#include <JuceHeader.h>
#include "PluginProcessor.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.setPassesAreLogged(true);
    
    std::cout << "=================================================================" << std::endl;
    std::cout << "EQoon DSP Test Suite" << std::endl;
    std::cout << "=================================================================" << std::endl;

    runner.runAllTests();
    
    std::cout << "=================================================================" << std::endl;
    std::cout << "Test Summary:" << std::endl;
    int totalTests = runner.getNumResults();
    int failedTests = 0;
    for (int i = 0; i < totalTests; ++i)
    {
        if (runner.getResult(i)->failures > 0)
            failedTests++;
    }
    
    std::cout << "Total Tests Run: " << totalTests << std::endl;
    std::cout << "Passed: " << (totalTests - failedTests) << std::endl;
    std::cout << "Failed: " << failedTests << std::endl;
    std::cout << "=================================================================" << std::endl;

    return failedTests > 0 ? 1 : 0;
}
