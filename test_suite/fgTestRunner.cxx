// SPDX-FileCopyrightText: 2016 Edward d'Auvergne
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cppunit/Protector.h>
#include <cppunit/TestResult.h>
#include <cppunit/TestResultCollector.h>
#include <cppunit/TextTestRunner.h>
#include <cppunit/extensions/TestFactoryRegistry.h>

#include "fgCompilerOutputter.hxx"
#include "fgJunitOutputter.hxx"
#include "fgTestListener.hxx"
#include "formatting.hxx"

#include "FGTestApi/testGlobals.hxx"

namespace {

// Turns FGTestApi::SkipTest into a skipped (rather than failed) test. It is the
// last protector pushed, so it sits innermost and sees the exception before
// CppUnit's default protector reports it as a failure.
class SkipProtector : public CppUnit::Protector
{
public:
    bool protect(const CppUnit::Functor& functor, const CppUnit::ProtectorContext&) override
    {
        try {
            return functor();
        } catch (const FGTestApi::SkipTest& skip) {
            FGTestApi::recordSkippedTest(skip.what());
            return true;
        }
    }
};

} // namespace


// Execute all test suites for the given test category.
int testRunner(const std::string& type, const std::string& title, char *subset, bool timings, bool ctest_output, bool junit, bool debug)
{
    // Declarations.
    CppUnit::TextTestRunner runner;

    // Print out a title.
    if (!ctest_output)
        printTitle(std::cerr, title);

    // Get all tests.
    runner.addTest(CppUnit::TestFactoryRegistry::getRegistry(type).makeTest());

    runner.eventManager().pushProtector(new SkipProtector);

    // Set up the test listener.
    fgTestListener *testListener;
    testListener = new fgTestListener;
    runner.eventManager().addListener(testListener);
    testListener->timings = timings;
    testListener->ctest_output = ctest_output;
    testListener->debug = debug;

    if (junit) {
        // Set the test suite output IO stream.
        runner.setOutputter(new fgJunitOutputter(type, &runner.result(), &testListener->test_data_records, &testListener->sum_time, std::cerr, debug));
    } else {
        runner.setOutputter(new fgCompilerOutputter(&runner.result(), &testListener->test_data_records, &testListener->sum_time, std::cerr, ctest_output, debug));
    }

    // Execute the tests.
    if (subset == NULL)
        runner.run("", false, true, false);
    else {
        std::stringstream testStream(subset);
        std::string testName;
        while( testStream.good() ) {
            getline( testStream, testName, ',' );
            runner.run(testName, false, true, false);
        }
    }

    // Clean up.
    delete testListener;

    // Report anything skipped, eg for lack of graphics.
    const auto skipped = FGTestApi::takeSkippedTests();
    if (!skipped.empty() && !ctest_output) {
        std::cerr << "\nSkipped " << skipped.size() << " test(s) in '" << type << "' (--headless):";
        for (const auto& name : skipped) {
            std::cerr << ' ' << name;
        }
        std::cerr << std::endl;
    }

    // Return the status of the tests.
    CppUnit::TestResultCollector &status = runner.result();
    return status.testFailuresTotal();
}
