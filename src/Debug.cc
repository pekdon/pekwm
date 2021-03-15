//
// Debug.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
// Copyright (C) 2012 Andreas Schlick <ioerror@lavabit.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "pekwm.hh"
#include "Debug.hh"
#include "Util.hh"

#include <cstdlib>

static Util::StringMap<Debug::Level> debug_level_map =
    {{"", Debug::LEVEL_WARN},
     {"ERROR", Debug::LEVEL_ERR},
     {"WARNING", Debug::LEVEL_WARN},
     {"INFO", Debug::LEVEL_INFO},
     {"DEBUG", Debug::LEVEL_DEBUG},
     {"TRACE", Debug::LEVEL_TRACE}};

static Debug::Level _level = Debug::LEVEL_WARN;
static bool _use_cerr = true;
std::ofstream _log("/dev/null");


/**
 * Output current timestamp to log stream.
 */
static void
addTimestamp(std::ostream &log)
{
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_r(&t, &tm);
    log << std::put_time(&tm, "%Y-%m-%d %H:%M:%S ");
}

namespace Debug
{

    /**
     * Get log level from string.
     */
    Level
    getLevel(const std::string& str)
    {
        return debug_level_map.get(str);
    }

    /**
     * Get current log level.
     */
    Level
    getLevel(void)
    {
        return _level;
    }

    /**
     * Set log level.
     */
    void
    setLevel(Level level)
    {
        _level = level;
    }

    std::ostream&
    getStream(const char* prefix)
    {
        std::ostream& log = _use_cerr ? std::cerr : _log;
        addTimestamp(log);
        log << prefix;
        return log;
    }

    std::ostream&
    getStream(const char* fun, int line, const char* prefix)
    {
        std::ostream& log = _use_cerr ? std::cerr : _log;
        addTimestamp(log);
        log << fun << '@' << line << ":\n    " << prefix;
        return log;
    }

    /**
     * Set log file.
     */
    bool
    setLogFile(const std::string& path)
    {
        _log.close();
        _log.open(path);
        return _log.good();
    }

    /**
     * Debug Commands:
     *
     * logfile <filename> - set log file, use - for stderr.
     * level [err|warn|info|debug|trace] - sets log level.
     */
    void
    doAction(const std::string &cmd)
    {
        std::vector<std::string> args;
        if (Util::splitString(cmd, args, " \t") != 2) {
            return;
        }

        Util::to_lower(args[0]);
        if (args[0] == "logfile") {
            if (args[1] == "-") {
                _log.close();
                _use_cerr = true;
            } else {
                _use_cerr = ! setLogFile(args[1]);
            }
        } else if (args[0] == "level") {
            _level = debug_level_map.get(args[1]);
        }
    }
}
