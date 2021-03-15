//
// Charset.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Charset.hh"
#include "Debug.hh"

#include <locale>
#include <iomanip>
#include <stdexcept>

extern "C" {
#include <string.h>
#include <iconv.h>
}

static iconv_t do_iconv_open(const char **from_names, const char **to_names);
static size_t do_iconv(iconv_t ic, const char **inp, size_t *in_bytes,
                       char **outp, size_t *out_bytes);

// Initializers, members used for shared buffer
unsigned int WIDE_STRING_COUNT = 0;
iconv_t IC_TO_WC = reinterpret_cast<iconv_t>(-1);
iconv_t IC_TO_UTF8 = reinterpret_cast<iconv_t>(-1);
char *ICONV_BUF = 0;
size_t ICONV_BUF_LEN = 0;

// Constants, name of iconv internal names
const char *ICONV_WC_NAMES[] = {"WCHAR_T", "UCS-4", 0};
const char *ICONV_UTF8_NAMES[] = {"UTF-8", "UTF8", 0};

const char *ICONV_UTF8_INVALID_STR = "<INVALID>";
const wchar_t *ICONV_WIDE_INVALID_STR = L"<INVALID>";

// Constants, maximum number of bytes a single UTF-8 character can use.
const size_t UTF8_MAX_BYTES = 6;


static void
iconv_buf_grow(size_t size)
{
    if (ICONV_BUF_LEN < size) {
        // Free resources, if any.
        if (ICONV_BUF) {
            delete [] ICONV_BUF;
        }

        // Calculate new buffer length and allocate new buffer
        for (; ICONV_BUF_LEN < size; ICONV_BUF_LEN *= 2)
            ;
        ICONV_BUF = new char[ICONV_BUF_LEN];
    }
}


/**
 * Open iconv handle with to/from names.
 *
 * @param from_names null terminated list of from name alternatives.
 * @param to_names null terminated list of to name alternatives.
 * @return iconv_t handle on success, else -1.
 */
iconv_t
do_iconv_open(const char **from_names, const char **to_names)
{
    iconv_t ic = reinterpret_cast<iconv_t>(-1);

    // Try all combinations of from/to name's to get a working
    // conversion handle.
    for (unsigned int i = 0; from_names[i]; ++i) {
        for (unsigned int j = 0; to_names[j]; ++j) {
            ic = iconv_open(to_names[j], from_names[i]);
            if (ic != reinterpret_cast<iconv_t>(-1)) {
#ifdef HAVE_ICONVCTL
                int int_value_one = 1;
                iconvctl(ic, ICONV_SET_DISCARD_ILSEQ, &int_value_one);
#endif // HAVE_ICONVCTL
                return ic;
            }
        }
    }

    return ic;
}

/**
 * Iconv wrapper to hide different definitions of iconv.
 * @param ic iconv handle.
 * @param inp Input pointer.
 * @param in_bytes Input bytes.
 * @param outp Output pointer.
 * @param out_bytes Output bytes.
 * @return number of bytes converted irreversible or -1 on error.
 */
size_t
do_iconv(iconv_t ic, const char **inp, size_t *in_bytes,
         char **outp, size_t *out_bytes)
{
#ifdef ICONV_CONST
    return iconv(ic, inp, in_bytes, outp, out_bytes);
#else // !ICONV_CONST
    return iconv(ic, const_cast<char**>(inp), in_bytes, outp, out_bytes);
#endif // ICONV_CONST
}

class NoGroupingNumpunct : public std::numpunct<char>
{
protected:
    virtual std::string do_grouping(void) const { return ""; } 
};

namespace Charset
{
    /**
     * Init charset conversion resources, must be called before
     * any other call to functions in the Charset namespace.
     */
    void
    init(void)
    {
        try {
            std::locale locale(std::locale(""), new NoGroupingNumpunct());
            std::locale::global(locale);
        } catch (const std::runtime_error&) {
            ERR("The environment variables specify an unknown C++ locale - "
                "falling back to C's setlocale().");
            setlocale(LC_ALL, "");
        }

        // Cleanup previous init if any, being paranoid.
        destruct();

        // Raise exception if this fails
        IC_TO_WC = do_iconv_open(ICONV_UTF8_NAMES, ICONV_WC_NAMES);
        IC_TO_UTF8 = do_iconv_open(ICONV_WC_NAMES, ICONV_UTF8_NAMES);

        // Equal mean
        if (IC_TO_WC != reinterpret_cast<iconv_t>(-1)
            && IC_TO_UTF8 != reinterpret_cast<iconv_t>(-1)) {
            // Create shared buffer.
            ICONV_BUF_LEN = 1024;
            ICONV_BUF = new char[ICONV_BUF_LEN];
        }
    }

    /**
     * Called to free static charset conversion resources.
     */
    void
    destruct(void)
    {
        // Cleanup resources
        if (IC_TO_WC != reinterpret_cast<iconv_t>(-1)) {
            iconv_close(IC_TO_WC);
        }

        if (IC_TO_UTF8 != reinterpret_cast<iconv_t>(-1)) {
            iconv_close(IC_TO_UTF8);
        }

        if (ICONV_BUF) {
            delete [] ICONV_BUF;
        }

        // Set members to safe values
        IC_TO_WC = reinterpret_cast<iconv_t>(-1);
        IC_TO_UTF8 = reinterpret_cast<iconv_t>(-1);
        ICONV_BUF = 0;
        ICONV_BUF_LEN = 0;
    }

    /**
     * Converts wide-character string to multibyte version
     *
     * @param str String to convert.
     * @return Returns multibyte version of string.
     */
    std::string
    to_mb_str(const std::wstring &str)
    {
        size_t ret, num = str.size() * 6 + 1;
        char *buf = new char[num];
        memset(buf, '\0', num);

        ret = wcstombs(buf, str.c_str(), num);
        if (ret == static_cast<size_t>(-1)) {
            USER_WARN("failed to convert wide string to multibyte string");
        }
        std::string ret_str(buf);

        delete [] buf;

        return ret_str;
    }

    /**
     * Converts multibyte string to wide-character version
     *
     * @param str String to convert.
     * @return Returns wide-character version of string.
     */
     std::wstring
     to_wide_str(const std::string &str)
     {
         size_t ret, num = str.size() + 1;
         wchar_t *buf = new wchar_t[num];
         wmemset(buf, L'\0', num);

         ret = mbstowcs(buf, str.c_str(), num);
         if (ret == static_cast<size_t>(-1)) {
             USER_WARN("failed to convert multibyte string to wide string");
         }
         std::wstring ret_str(buf);

         delete [] buf;

         return ret_str;
     }

    /**
     * Converts wide-character string to UTF-8
     * @param str String to convert.
     * @return Returns UTF-8 representation of string.
     */
     std::string
     to_utf8_str(const std::wstring &str)
     {
         std::string utf8_str;

         // Calculate length
         size_t in_bytes = str.size() * sizeof(wchar_t);
         size_t out_bytes = str.size() * UTF8_MAX_BYTES + 1;

         iconv_buf_grow(out_bytes);

         // Convert
         const char *inp = reinterpret_cast<const char*>(str.c_str());
         char *outp = ICONV_BUF;
         size_t len = do_iconv(IC_TO_UTF8, &inp, &in_bytes, &outp, &out_bytes);
         if (len != static_cast<size_t>(-1)) {
             // Terminate string and cache result
             *outp = '\0';
             utf8_str = ICONV_BUF;
         } else {
             USER_WARN("to_utf8_str, failed with error " << strerror(errno));
             utf8_str = ICONV_UTF8_INVALID_STR;
         }

         return utf8_str;
     }

    /**
     * Converts to wide string from UTF-8
     * @param str String to convert.
     * @return Returns wide representation of string.
     */
     std::wstring
     from_utf8_str(const std::string &str)
     {
         std::wstring wide_str;

         // Calculate length
         size_t in_bytes = str.size();
         size_t out_bytes = (in_bytes + 1) * sizeof(wchar_t);

         iconv_buf_grow(out_bytes);

         // Convert
         const char *inp = str.c_str();
         char *outp = ICONV_BUF;
         size_t len = do_iconv(IC_TO_WC, &inp, &in_bytes, &outp, &out_bytes);
         if (len != static_cast<size_t>(-1)) {
             // Terminate string and cache result
             *reinterpret_cast<wchar_t*>(outp) = L'\0';
             wide_str = reinterpret_cast<wchar_t*>(ICONV_BUF);
         } else {
             USER_WARN("from_utf8_str, failed on string \"" << str << "\"");
             wide_str = ICONV_WIDE_INVALID_STR;
         }

         return wide_str;
     }
}
