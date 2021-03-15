//
// pekwm_wm.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include "Charset.hh"
#include "Compat.hh"
#include "Debug.hh"
#include "WindowManager.hh"
#include "Util.hh"

#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
#include <locale>

extern "C" {
#include <unistd.h> // execlp
#include <locale.h>
}

/**
 * Prints version
 */
void
printVersion(void)
{
    std::cout << "pekwm: version " << VERSION << std::endl;
}

/**
 * Prints version and availible options
 */
void
printUsage(void)
{
    printVersion();
    std::cout
        << " --config    alternative config file" << std::endl
        << " --display   display to connect to" << std::endl
        << " --help      show this info." << std::endl
        << " --info      extended info. Use for bug reports." << std::endl
        << " --log-file  set log file." << std::endl
        << " --log-level set log level." << std::endl
        << " --replace   replace running window manager" << std::endl
        << " --sync      run Xlib in synchronous mode" << std::endl
        << " --version   show version info" << std::endl;
}

/**
 * Prints version and build-time options
 */
void
printInfo(void)
{
    printVersion();
    std::cout << "features: " << FEATURES << std::endl;
}

void
stop(int write_fd, const std::string &msg, WindowManager *wm)
{
    if (wm) {
        delete wm;
        pekwm::cleanup();
    }

    Charset::destruct();

    if (write(write_fd, msg.c_str(), msg.size() + 1) == -1) {
        ERR("failed to write pekwm_wm result msg " << msg
            << " due to: " << strerror(errno));
    }
    exit(0);
}

/**
 * Main function of pekwm
 */
int
main(int argc, char **argv)
{
    Charset::init();

    // get the args and test for different options
    int write_fd = fileno(stdout);
    std::string config_file;
    bool synchronous = false;
    bool replace = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp("--info", argv[i]) == 0) {
            printInfo();
            stop(write_fd, "stop", nullptr);
        } else if (strcmp("--log-level", argv[i]) == 0 && ((i + 1) < argc)) {
            Debug::setLevel(Debug::getLevel(argv[++i]));
        } else if (strcmp("--log-file", argv[i]) == 0 && ((i + 1) < argc)) {
            if (! Debug::setLogFile(argv[++i])) {
                std::cerr << "Failed to open log file " << argv[i] << std::endl;
            }
        } else if (strcmp("--replace", argv[i]) == 0) {
            replace = true;
        } else if (strcmp("--sync", argv[i]) == 0) {
            synchronous = true;
        } else if (strcmp("--version", argv[i]) == 0) {
            printVersion();
            stop(write_fd, "stop", nullptr);
        } else if (strcmp("--fd", argv[i]) == 0 && ((i + 1) < argc)) {
            write_fd = std::stoi(argv[++i]);
        } else {
            printUsage();
            stop(write_fd, "stop", nullptr);
        }
    }

    // Get configuration file if none was specified as a parameter,
    // default to reading environment, if not set get ~/.pekwm/config
    if (config_file.size() == 0) {
        config_file = Util::getEnv("PEKWM_CONFIG_FILE");
        if (config_file.size() == 0) {
            auto home = Util::getEnv("HOME");
            if (home.size() == 0) {
                std::cerr << "failed to get configuration file path, "
                          << "$HOME not set." << std::endl;
                stop(write_fd, "error", nullptr);
            }
            config_file = home + "/.pekwm/config";
        }
    }

    USER_INFO("Starting pekwm. Use this information in bug reports: "
              << FEATURES << std::endl
              << "using configuration at " << config_file);

    auto wm = WindowManager::start(config_file, replace, synchronous);
    if (wm) {
        try {
            TRACE("Enter event loop.");

            wm->doEventLoop();

            if (wm->shallRestart()) {
                auto command = wm->getRestartCommand();
                stop(write_fd, "restart " + command, wm);
            }

            stop(write_fd, "stop", wm);
        } catch (StopException& ex) {
            stop(write_fd, ex.getMsg(), wm);
        } catch (std::string& ex) {
            ERR("unexpected error occurred: " << ex);
            stop(write_fd, "error", wm);
        }
    } else {
        stop(write_fd, "stop", wm);
    }

    return 0;
}
