//
// PFont.hh for pekwm
// Copyright (C) 2003-2020 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include <string>
#include <limits>

#include "pekwm.hh"

extern "C" {
#ifdef HAVE_XFT
#include <X11/Xft/Xft.h>
#endif // HAVE_XFT
}

class PFont
{
public:
    enum Type {
        FONT_TYPE_X11, FONT_TYPE_XFT, FONT_TYPE_XMB, FONT_TYPE_NO
    };
    enum TrimType {
        FONT_TRIM_END, FONT_TRIM_MIDDLE
    };

    class Color {
    public:
        Color(void) : _has_fg(false), _has_bg(false),
                      _fg_alpha(65535), _bg_alpha(65535)
                     { }
        ~Color(void) { }

        inline XColor *getFg(void) { return _fg; }
        inline XColor *getBg(void) { return _bg; }
        inline void setFg(XColor *xc) { _fg = xc; }
        inline void setBg(XColor *xc) { _bg = xc; }

        inline uint getFgAlpha(void) const { return _fg_alpha; }
        inline uint getBgAlpha(void) const { return _bg_alpha; }
        inline void setFgAlpha(uint alpha) { _fg_alpha = alpha; }
        inline void setBgAlpha(uint alpha) { _bg_alpha = alpha; }

        inline bool hasFg(void) const { return _has_fg; }
        inline bool hasBg(void) const { return _has_bg; }
        inline void setHasFg(bool f) { _has_fg = f; }
        inline void setHasBg(bool b) { _has_bg = b; }

    private:
        XColor *_fg, *_bg;
        bool _has_fg, _has_bg;

        uint _fg_alpha, _bg_alpha;
    };

    PFont(void);
    virtual ~PFont(void);

    inline uint getJustify(void) const { return _justify; }

    inline void setJustify(uint j) { _justify = j; }
    inline void setOffset(uint x, uint y) { _offset_x = x; _offset_y = y; }

    int draw(Drawable dest, int x, int y, const std::wstring &text,
             uint max_chars = 0, uint max_width = 0,
             PFont::TrimType trim_type = FONT_TRIM_END);

    void trim(std::wstring &text, TrimType trim_type, uint max_width);
    void trimEnd(std::wstring &text, uint max_width);
    void trimMiddle(std::wstring &text, uint max_width);

    static void setTrimString(const std::string &trim);

    uint justify(const std::wstring &text, uint max_width,
                 uint padding, uint chars);

    // virtual interface
    virtual bool load(const std::string& font_name) = 0;
    virtual void unload(void) { }

    virtual uint getWidth(const std::wstring& text, uint max_chars = 0) = 0;
    virtual uint getHeight(void) { return _height; }

    virtual void setColor(PFont::Color* color) = 0;

private:
    virtual void drawText(Drawable dest, int x, int y, const std::wstring &text,
                          uint chars, bool fg) = 0;

protected:
    uint _height, _ascent, _descent;
    uint _offset_x, _offset_y, _justify;

    static std::wstring _trim_string;
};

class PFontX11 : public PFont {
public:
  PFontX11(void);
  virtual ~PFontX11(void);

  // virtual interface
  virtual bool load(const std::string &name);
  virtual void unload(void);

  virtual uint getWidth(const std::wstring &text, uint max_chars = 0);

  virtual void setColor(PFont::Color *color);

private:
  virtual void drawText(Drawable dest, int x, int y, const std::wstring &text,
                        uint chars, bool fg);

private:
  XFontStruct *_font;
  GC _gc_fg, _gc_bg;
};

class PFontXmb : public PFont {
public:
    PFontXmb(void);
    virtual ~PFontXmb(void);

    // virtual interface
    virtual bool load(const std::string &name);
    virtual void unload(void);

    virtual uint getWidth(const std::wstring &text, uint max_chars = 0);

    virtual void setColor(PFont::Color *color);

private:
    virtual void drawText(Drawable dest, int x, int y, const std::wstring &text,
                          uint chars, bool fg);

    XFontSet _fontset;
    GC _gc_fg, _gc_bg;
    static const char *DEFAULT_FONTSET; /**< Default fallback fontset. */
};

#ifdef HAVE_XFT
class PFontXft : public PFont {
public:
    PFontXft(void);
    virtual ~PFontXft(void);

    // virtual interface
    virtual bool load(const std::string &font_name);
    virtual void unload(void);

    virtual uint getWidth(const std::wstring &text, uint max_chars = 0);

    virtual void setColor(PFont::Color *color);

private:
    virtual void drawText(Drawable dest, int x, int y, const std::wstring &text,
                          uint chars, bool fg);

    XftDraw *_draw;
    XftFont *_font;
    XftColor *_cl_fg, *_cl_bg;

    XRenderColor _xrender_color;
};
#endif // HAVE_XFT
