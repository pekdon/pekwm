//
// RegexString.hh for pekwm
// Copyright ©  2003-2008 Claes Nästén <me{@}pekdon{.}net>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _REGEX_STRING_HH_
#define _REGEX_STRING_HH_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

#include <string>
#include <list>

extern "C" {
#include <sys/types.h>
#include <regex.h>
}

#include "Types.hh"

//! @brief POSIX regular expression wrapper.
class RegexString
{
public:
    //! @brief Part of parsed replace data.
    class Part
    {
    public:
        //! @brief RegexString::Part constructor.
        Part (const std::wstring &or_string, int i_ref = -1) :
        m_o_string (or_string), m_i_ref (i_ref) { }
        //! @brief RegexString::Part destructor.
        ~Part (void) { }

        //! @brief Returns string data.
        const std::wstring &get_string (void) { return m_o_string; }
        //! @brief Returns reference number.
        int get_reference (void) { return m_i_ref; }

    private:
        std::wstring m_o_string; //!< String data at item.
        int m_i_ref; //!< Reference string should be replaced with.
    };

    RegexString (void);
    ~RegexString (void);

    //! @brief Returns parse_match data status.
    bool is_match_ok (void) { return m_reg_ok; }

    bool ed_s (std::wstring &or_string);

    bool parse_match(const std::wstring &or_match, bool full = false);
    bool parse_replace (const std::wstring &or_replace);
    bool parse_ed_s (const std::wstring &or_ed_s);

    bool operator== (const std::wstring &or_rhs);

private:
    void free_regex (void);

private:
    regex_t m_o_regex; //!< Compiled regular expression holder.
    bool m_reg_ok; //!< m_o_regex compiled ok flag.

    int m_i_ref_max; //!< Highest reference used.
    std::list<RegexString::Part> m_o_ref_list; //!< List of RegexString::Part holding data generated by parse_replace.
};

#endif // _REGEX_STRING_HH_
