//
// PTexture.hh for pekwm
// Copyright (C) 2004-2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include "pekwm.hh"
#include "Render.hh"
#include "X11.hh"

class PTexture {
public:
    enum Type {
        TYPE_SOLID,
        TYPE_SOLID_RAISED,
        TYPE_LINES_HORZ,
        TYPE_LINES_VERT,
        TYPE_IMAGE,
        TYPE_IMAGE_MAPPED,
        TYPE_EMPTY,
        TYPE_NO
    };

    PTexture(void);
    virtual ~PTexture(void);

    void render(Drawable draw,
                int x, int y, uint width, uint height,
                int root_x=0, int root_y=0);
    void render(Render &rend,
                int x, int y, uint width, uint height,
                int root_x=0, int root_y=0);
    virtual void doRender(Render &rend,
                          int x, int y, uint width, uint height) = 0;
    virtual bool getPixel(ulong &pixel) const = 0;
    virtual Pixmap getMask(uint, uint, bool&) { return None; }

    void setBackground(Drawable draw,
                       int x, int y, uint width, uint height);

    bool isOk(void) const { return _ok; }
    uint getWidth(void) const;
    uint getHeight(void) const;
    PTexture::Type getType(void) const { return _type; }

    void setWidth(uint width) { _width = width; }
    void setHeight(uint height) { _height = height; }

    uchar getOpacity(void) const { return _opacity; }
    void setOpacity(uchar opacity) { _opacity = opacity; }

private:
    bool renderOnBackground(XImage *src_ximage,
                            int x, int y, uint width, uint height,
                            int root_x, int root_y);

protected:
    bool _ok; // Texture successfully loaded
    uint _width, _height; // for images etc, 0 for infinite like in stretch
    PTexture::Type _type; // Type of texture
    uchar _opacity; // Texture opacity, blended onto background pixmap
};
