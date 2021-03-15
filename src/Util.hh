//
// Util.hh for pekwm
// Copyright (C) 2002-2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include "Types.hh"

#include <algorithm>
#include <string>
#include <cstring>
#include <map>
#include <functional>
#include <sstream>
#include <vector>

extern "C" {
#include <string.h>
}

/**
 * String utilities, convenience functions for making life easier
 * when working with strings.
 */
namespace String {
    class Key {
    public:
        Key(const char *key);
        Key(const std::string &key);
        ~Key(void);

        const std::string& str(void) const { return _key; }

        bool operator==(const std::string &rhs) const;
        bool operator!=(const std::string &rhs) const;
        bool operator<(const Key &rhs) const;
        bool operator>(const Key &rhs) const;

    private:
        std::string _key;
    };

    size_t safe_position(size_t pos, size_t fallback = 0, size_t add = 0);
    std::vector<std::string> shell_split(const std::string& str);
}

namespace Util {
    template<typename T>
    class StringMap : public std::map<String::Key, T> {
    public:
        using std::map<String::Key, T>::map;

        T& get(const std::string& key) {
            typename StringMap<T>::iterator it = this->find(key);
            return it == this->end() ? this->at("") : it->second;
        }
    };

    /**
     * Reference counted entry.
     */
    template<class T>
    class RefEntry {
    public:
        RefEntry(T data = nullptr)
            : _data(data),
              _ref(data == nullptr ? 0 : 1)
        {
        }
        virtual ~RefEntry(void) { }

        T get(void) { return _data; }
        void set(T data) { _data = data; }

        uint getRef(void) const { return _ref; }
        uint incRef(void) { _ref++; return _ref; }
        uint decRef(void) { if (_ref > 0) { _ref--; } return _ref; }

    private:
        T _data;
        uint _ref;
    };

    std::string getEnv(const std::string& key);

    void forkExec(std::string command);
    pid_t forkExec(const std::vector<std::string>& args);
    std::string getHostname(void);
    bool setNonBlock(int fd);

    bool isFile(const std::string &file);
    bool isExecutable(const std::string &file);
    time_t getMtime(const std::string &file);

    bool copyTextFile(const std::string &from, const std::string &to);

    std::string getUserName(void);

    std::string getFileExt(const std::string &file);
    std::string getDir(const std::string &file);
    void expandFileName(std::string &file);

    void getThemeDir(const std::string &config_file,
                     std::string &theme_dir, std::string &theme_variant,
                     std::string &theme_path);

    const char* spaceChars(char escape);
    const wchar_t* spaceChars(wchar_t escape);

    /**
     * Split the string str based on separator sep and put into vals
     *
     * This splits the string str into to max_tokens parts and puts in
     * the vector vals. If max is 0 then it'll split it into as many
     * tokens as possible, max defaults to 0.  splitString returns the
     * number of tokens it put into vals.
     *
     * @param str String to split
     * @param vals Vector to put split values into
     * @param sep Separators to use when splitting string
     * @param max Maximum number of elements to put into vals (optional)
     * @param include_empty Include empty elements, defaults to false.
     * @param escape Escape character (optional)
     * @return Number of tokens inserted into vals
     */
    template<typename T>
    uint splitString(const std::basic_string<T> &str,
                     std::vector<std::basic_string<T>> &toks,
                     const T *sep, uint max = 0,
                     bool include_empty = false, T escape = 0)
    {
        auto start = str.find_first_not_of(spaceChars(escape));
        if (str.size() == 0 || start == std::string::npos) {
            return 0;
        }

        std::basic_string<T> token;
        token.reserve(str.size());
        bool in_escape = false;
        uint num_tokens = 1;

        auto it = str.cbegin() + start;
        for (; it != str.cend() && (max == 0 || num_tokens < max); ++it) {
            if (in_escape) {
                token += *it;
                in_escape = false;
            } else if (*it == escape) {
                in_escape = true;
            } else {
                const T *p = sep;
                for (; *p; p++) {
                    if (*p == *it) {
                        if (token.size() > 0 || include_empty) {
                            toks.push_back(token);
                            ++num_tokens;
                        }
                        token.clear();
                        break;
                    }
                }

                if (! *p) {
                    token += *it;
                }
            }
        }

        // Get the last token (if any)
        if (it != str.cend()) {
            copy(it, str.cend(), back_inserter(token));
        }

        if (token.size() > 0 || include_empty) {
            toks.push_back(token);
            ++num_tokens;
        }

        return num_tokens - 1;
    }

    std::string to_string(void* v);

    /**
     * Converts string to uppercase
     *
     * @param str Reference to the string to convert
     */
    inline void to_upper(std::string &str) {
        std::transform(str.begin(), str.end(), str.begin(),
                       (int(*)(int)) std::toupper);
    }

    /**
     * Converts string to lowercase
     *
     * @param str Reference to the string to convert
     */
    inline void to_lower(std::string &str) {
        std::transform(str.begin(), str.end(), str.begin(),
                       (int(*)(int)) std::tolower);
    }

    /**
     * Converts wide string to uppercase
     *
     * @param str Reference to the string to convert
     */
    inline void to_upper(std::wstring &str) {
        std::transform(str.begin(), str.end(), str.begin(),
                       (int(*)(int)) std::towupper);
    }


    /**
     * Converts wide string to lowercase
     *
     * @param str Reference to the string to convert
     */
    inline void to_lower(std::wstring &str) {
        std::transform(str.begin(), str.end(), str.begin(),
                       (int(*)(int)) std::towlower);
    }

    /**
     * Return value within bounds of min and max value.
     */
    template<typename T>
    T between(T value, T min_val, T max_val) {
        if (value < min_val) {
            value = min_val;
        } else if (value > max_val) {
            value = max_val;
        }
        return value;
    }

    //! @brief Removes leading blanks( \n\t) from string.
    inline void trimLeadingBlanks(std::string &trim) {
        std::string::size_type first = trim.find_first_not_of(" \n\t");
        if ((first != std::string::npos) &&
            (first != (std::string::size_type) trim[0])) {
            trim = trim.substr(first, trim.size() - first);
        }
    }

    bool isTrue(const std::string &value);

    //! @brief for_each delete utility.
    template<class T> struct Free : public std::unary_function<T, void> {
        void operator ()(T t) { delete t; }
    };
}
