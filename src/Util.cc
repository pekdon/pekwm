//
// Util.cc for pekwm
// Copyright (C) 2002-2020 Claes Nästén <pekdon@gmail.com>
//
// misc.cc for aewm++
// Copyright (C) 2000 Frank Hale <frankhale@yahoo.com>
// http://sapphire.sourceforge.net/
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include <cerrno>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>

extern "C" {
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
}

#include "CfgParser.hh"
#include "Debug.hh"
#include "Util.hh"

#define THEME_DEFAULT DATADIR "/pekwm/themes/default/theme"

namespace String
{
    Key::Key(const char *key)
        : _key(key)
    {
    }

    Key::Key(const std::string &key)
        : _key(key)
    {
    }

    Key::~Key(void)
    {
    }

    bool
    Key::operator==(const std::string &rhs) const {
        return (strcasecmp(_key.c_str(), rhs.c_str()) == 0);
    }

    bool
    Key::operator!=(const std::string &rhs) const {
        return (strcasecmp(_key.c_str(), rhs.c_str()) != 0);
    }

    bool
    Key::operator<(const Key &rhs) const {
        return (strcasecmp(_key.c_str(), rhs._key.c_str()) < 0);
    }

    bool
    Key::operator>(const Key &rhs) const {
        return (strcasecmp(_key.c_str(), rhs._key.c_str()) > 0);
    }

    /** Get safe version of position */
    size_t safe_position(size_t pos, size_t fallback, size_t add) {
        return pos == std::wstring::npos ? fallback : (pos + add);
    }

    /**
     * Split string into tokens similar to how a shell interprets
     * escape and quote characets.
     */
    std::vector<std::string>
    shell_split(const std::string& str)
    {
        bool in_tok = false;
        bool in_escape = false;
        char in_quote = 0;
        std::vector<std::string> toks;

        std::string tok;
        for (auto c : str) {
            if (! in_tok && ! isspace(c)) {
                in_tok = true;
            }

            if (in_tok) {
                if (in_escape) {
                    tok.push_back(c);
                    in_escape = false;
                } else if (c == in_quote) {
                    in_quote = 0;
                    toks.push_back(tok);
                    tok.clear();
                    in_tok = false;
                } else if (c == '\\') {
                    in_escape = true;
                } else if (in_quote) {
                    tok.push_back(c);
                } else if (c == '"' || c == '\'') {
                    in_quote = c;
                } else if (isspace(c)) {
                    toks.push_back(tok);
                    tok.clear();
                    in_tok = false;
                } else {
                    tok.push_back(c);
                }
            }
        }

        if (in_tok && tok.size()) {
            toks.push_back(tok);
        }

        return toks;
    }
}

namespace Util {

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif // HOST_NAME_MAX

/**
 * Return environment variabel as string.
 */
std::string getEnv(const std::string& key)
{
    auto val = getenv(key.c_str());
    return val ? val : "";
}

/**
 * Fork and execute command with /bin/sh and execlp
 */
void
forkExec(std::string command)
{
    if (command.length() == 0) {
        ERR("command length == 0");
        return;
    }

    pid_t pid = fork();
    switch (pid) {
    case 0:
        setsid();
        execlp("/bin/sh", "sh", "-c", command.c_str(), (char *) 0);
        ERR("execlp failed: " << strerror(errno));
        exit(1);
    case -1:
        ERR("fork failed: " << strerror(errno));
        break;
    default:
        TRACE("started child " << pid);
        break;
    }
}

pid_t
forkExec(const std::vector<std::string>& args)
{
    assert(! args.empty());

    pid_t pid = fork();
    switch (pid) {
    case 0: {
        int i = 0;
        auto argv = new char*[args.size() + 1];
        auto it = args.begin();
        for (; it != args.end(); ++it) {
            argv[i++] = const_cast<char*>(it->c_str());
        }
        argv[i] = nullptr;

        setsid();
        execvp(argv[0], argv);
        exit(1);
    }
    case -1:
        ERR("fork failed: " << strerror(errno));
    default:
        TRACE("started child " << pid);
        return pid;
    }
}

/**
 * Wrapper for gethostname returning a string instead of populating
 * char buffer.
 */
std::string
getHostname(void)
{
    std::string hostname;

    // Set WM_CLIENT_MACHINE
    char hostname_buf[HOST_NAME_MAX + 1];
    if (! gethostname(hostname_buf, HOST_NAME_MAX)) {
      // Make sure it is null terminated
      hostname_buf[HOST_NAME_MAX] = '\0';
      hostname = hostname_buf;
    }

    return hostname;
}

/**
 * Set file descriptor in non-blocking mode.
 */
bool
setNonBlock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        ERR("failed to get flags from fd " << fd << ": " << strerror(errno));
        return false;
    }
    int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1) {
        ERR("failed to set O_NONBLOCK on fd " << fd << ": " << strerror(errno));
        return false;
    }
    return true;
}

//! @brief Determines if the file exists
bool
isFile(const std::string &file)
{
    if (file.size() == 0) {
        return false;
    }

    struct stat stat_buf;
    if (stat(file.c_str(), &stat_buf) == 0) {
        return (S_ISREG(stat_buf.st_mode));
    }

    return false;
}

//! @brief Determines if the file is executable for the current user.
bool
isExecutable(const std::string &file)
{
    if (file.size() == 0) {
        ERR("file length == 0");
        return false;
    }

    struct stat stat_buf;
    if (! stat(file.c_str(), &stat_buf)) {
        if (stat_buf.st_uid == getuid()) { // user readable and executable
            if ((stat_buf.st_mode&S_IRUSR) && (stat_buf.st_mode&S_IXUSR)) {
                return true;
            }
        }
        if (getgid() == stat_buf.st_gid) { // group readable and executable
            if ((stat_buf.st_mode&S_IRGRP) && (stat_buf.st_mode&S_IXGRP)) {
                return true;
            }
        }
        if ((stat_buf.st_mode&S_IROTH) && (stat_buf.st_mode&S_IXOTH)) {
            return true; // other readable and executable
        }
    }

    return false;
}

/**
 * Get file mtime.
 */
time_t
getMtime(const std::string &file)
{
    struct stat stat_buf;

    if (! stat(file.c_str(), &stat_buf)) {
        return stat_buf.st_mtime;
    } else {
        return 0;
    }
}

/**
 * Copies a single text file.
 */
bool
copyTextFile(const std::string &from, const std::string &to)
{
    if ((from.length() == 0) || (to.length() == 0)) {
        return false;
    }

    std::ifstream stream_from(from.c_str());
    if (! stream_from.good()) {
        USER_WARN("can not copy: " << from << " to: " << to);
        return false;
    }

    std::ofstream stream_to(to.c_str());
    if (! stream_to.good()) {
        USER_WARN("can not copy: " << from << " to: " << to);
        return false;
    }

    stream_to << stream_from.rdbuf();

    return true;
}

/**
 * Get name of the current user.
 */
std::string
getUserName(void)
{
    // Try to lookup current user with
    struct passwd *entry = getpwuid(geteuid());

    if (entry && entry->pw_name) {
        return entry->pw_name;
    } else {
        if (getenv("USER")) {
            return getenv("USER");
        } else {
            return "UNKNOWN";
        }
    }
}


//! @brief Returns .extension of file
std::string
getFileExt(const std::string &file)
{
    std::string::size_type pos = file.find_last_of('.');
    if ((pos != std::string::npos) && (pos < file.size())) {
        return file.substr(pos + 1, file.size() - pos - 1);
    } else {
        return std::string("");
    }
}

//! @brief Returns dir part of file
std::string
getDir(const std::string &file)
{
    std::string::size_type pos = file.find_last_of('/');
    if ((pos != std::string::npos) && (pos < file.size())) {
        return file.substr(0, pos);
    } else {
        return std::string("");
    }
}

//! @brief Replaces the ~ with the complete homedir path.
void
expandFileName(std::string &file)
{
    if (file.size() > 0) {
        if (file[0] == '~') {
            file.replace(0, 1, getEnv("HOME"));
        }
    }
}

void
getThemeDir(const std::string& config_file,
            std::string& dir, std::string& variant,
            std::string& theme_file)
{
    CfgParser cfg;
    cfg.parse(config_file, CfgParserSource::SOURCE_FILE, true);
    auto files = cfg.getEntryRoot()->findSection("FILES");
    if (files != nullptr) {
        std::vector<CfgParserKey*> keys;
        keys.push_back(new CfgParserKeyPath("THEME", dir, THEME_DEFAULT));
        keys.push_back(new CfgParserKeyString("THEMEVARIANT", variant));
        files->parseKeyValues(keys.begin(), keys.end());
        std::for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
    } else {
        dir = THEME_DEFAULT;
        variant = "";
    }

    std::string norm_dir(dir);
    if (dir.size() && dir.at(dir.size() - 1) == '/') {
        norm_dir.erase(norm_dir.end() - 1);
    }

    theme_file = norm_dir + "/theme";
    if (! variant.empty()) {
        auto theme_file_variant = theme_file + "-" + variant;
        if (isFile(theme_file_variant)) {
            theme_file = theme_file_variant;
        } else {
            DBG("theme variant " << variant << " does not exist");
        }
    }
}

const char*
spaceChars(char escape)
{
    return " \t\n";
}

const wchar_t*
spaceChars(wchar_t escape)
{
    return L" \t\n";
}
    /**
     * Returns true if value represents true(1 or TRUE).
     */
    bool
    isTrue(const std::string &value)
    {
        if (value.size() > 0) {
            if ((value[0] == '1') // check for 1 / 0
                || ! ::strncasecmp(value.c_str(), "TRUE", 4)) {
                return true;
            }
        }
        return false;
    }

    std::string to_string(void* v)
    {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }

} // end namespace Util.
