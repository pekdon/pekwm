//
// test_pekwm.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "test.hh"

#include "Debug.hh"

#include "test_Action.hh"
#include "test_CfgParser.hh"
#include "test_Config.hh"
#include "test_Frame.hh"
#include "test_ManagerWindows.hh"
#include "test_Theme.hh"
#include "test_Util.hh"
#include "test_WindowManager.hh"
#include "test_x11.hh"

int
main(int argc, char *argv[])
{
    // Setup environment required for the tests
    Config cfg;
    HintWO hint_wo(None);
    Debug::setLogFile("/dev/null");
    X11::addHead(Head(0, 0, 800, 600));

    // Action
    TestAction testAction;
    TestActionConfig testActionConfig;

    // CfgParser
    TestCfgParser testCfgParser;

    // Config
    TestConfig testConfig;

    // Frame
    TestFrame testFrame;

    // ManagerWindows
    TestRootWO testRootWO(&hint_wo, &cfg);

    // Theme
    TestTheme testTheme;

    // Util
    TestString testString;
    TestUtil testUtil;

    // WindowManager
    TestWindowManager testWindowManager;

    // x11
    TestX11 testX11;

    TestSuite::main(argc, argv);
}
