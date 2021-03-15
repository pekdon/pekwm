//
// X11.cc for pekwm
// Copyright (C) 2009-2021 the pekwm development team
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include <string>
#include <iostream>
#include <cassert>
#include <cstring> // required for memset in FD_ZERO
#include <limits>

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#ifdef HAVE_SHAPE
#include <X11/extensions/shape.h>
#endif // HAVE_SHAPE
#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif // HAVE_XRANDR
#include <X11/keysym.h> // For XK_ entries
#include <sys/select.h>
#ifdef HAVE_X11_XKBLIB_H
#include <X11/XKBlib.h>
#endif
bool xerrors_ignore = false;

unsigned int xerrors_count = 0;
}

#include "X11.hh"
#include "Debug.hh"

const uint X11::MODIFIER_TO_MASK[] = {
    ShiftMask, LockMask, ControlMask,
    Mod1Mask, Mod2Mask, Mod3Mask, Mod4Mask, Mod5Mask
};
const uint X11::MODIFIER_TO_MASK_NUM =
    sizeof(X11::MODIFIER_TO_MASK) /
    sizeof(X11::MODIFIER_TO_MASK[0]);
Atom X11::_atoms[MAX_NR_ATOMS];

extern "C" {
    /**
     * Invoked after all Xlib calls if run in synchronous mode.
     */
    static int afterXlibCall(Display *dpy)
    {
        static uint last_xerrors_count = 0;
        if (xerrors_count != last_xerrors_count) {
            last_xerrors_count = xerrors_count;
        }
        return 0;
    }

    /**
      * XError handler, prints error.
      */
    static int
    handleXError(Display *dpy, XErrorEvent *ev)
    {
        if (xerrors_ignore) {
            return 0;
        }

        ++xerrors_count;
        if (Debug::getLevel() >= Debug::LEVEL_TRACE) {
            char error_buf[256];
            XGetErrorText(dpy, ev->error_code, error_buf, 256);
            TRACE("XError: " << error_buf << " id: " << ev->resourceid);
        }

        return 0;
    }
}

static const char *atomnames[] = {
    // EWMH atoms
    "_NET_SUPPORTED",
    "_NET_CLIENT_LIST", "_NET_CLIENT_LIST_STACKING",
    "_NET_NUMBER_OF_DESKTOPS",
    "_NET_DESKTOP_GEOMETRY", "_NET_DESKTOP_VIEWPORT",
    "_NET_CURRENT_DESKTOP", "_NET_DESKTOP_NAMES",
    "_NET_ACTIVE_WINDOW", "_NET_WORKAREA",
    "_NET_DESKTOP_LAYOUT", "_NET_SUPPORTING_WM_CHECK",
    "_NET_CLOSE_WINDOW",
    "_NET_WM_MOVERESIZE",
    "_NET_REQUEST_FRAME_EXTENTS",
    "_NET_WM_NAME", "_NET_WM_VISIBLE_NAME",
    "_NET_WM_ICON_NAME", "_NET_WM_VISIBLE_ICON_NAME",
    "_NET_WM_ICON", "_NET_WM_DESKTOP",
    "_NET_WM_STRUT", "_NET_WM_PID",
    "_NET_WM_USER_TIME",
    "_NET_FRAME_EXTENTS",
    "_NET_WM_WINDOW_OPACITY",

    "_NET_WM_WINDOW_TYPE",
    "_NET_WM_WINDOW_TYPE_DESKTOP",
    "_NET_WM_WINDOW_TYPE_DOCK",
    "_NET_WM_WINDOW_TYPE_TOOLBAR",
    "_NET_WM_WINDOW_TYPE_MENU",
    "_NET_WM_WINDOW_TYPE_UTILITY",
    "_NET_WM_WINDOW_TYPE_SPLASH",
    "_NET_WM_WINDOW_TYPE_DIALOG",
    "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
    "_NET_WM_WINDOW_TYPE_POPUP_MENU",
    "_NET_WM_WINDOW_TYPE_TOOLTIP",
    "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    "_NET_WM_WINDOW_TYPE_COMBO",
    "_NET_WM_WINDOW_TYPE_DND",
    "_NET_WM_WINDOW_TYPE_NORMAL",

    "_NET_WM_STATE",
    "_NET_WM_STATE_MODAL", "_NET_WM_STATE_STICKY",
    "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
    "_NET_WM_STATE_SHADED",
    "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER",
    "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_FULLSCREEN",
    "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_BELOW",
    "_NET_WM_STATE_DEMANDS_ATTENTION",

    "_NET_WM_ALLOWED_ACTIONS",
    "_NET_WM_ACTION_MOVE", "_NET_WM_ACTION_RESIZE",
    "_NET_WM_ACTION_MINIMIZE", "_NET_WM_ACTION_SHADE",
    "_NET_WM_ACTION_STICK",
    "_NET_WM_ACTION_MAXIMIZE_VERT", "_NET_WM_ACTION_MAXIMIZE_HORZ",
    "_NET_WM_ACTION_FULLSCREEN", "_NET_WM_ACTION_CHANGE_DESKTOP",
    "_NET_WM_ACTION_CLOSE",

    "UTF8_STRING",
    "STRING",
    "MANAGER",

    // pekwm atoms
    "_PEKWM_FRAME_ID",
    "_PEKWM_FRAME_ORDER",
    "_PEKWM_FRAME_ACTIVE",
    "_PEKWM_FRAME_DECOR",
    "_PEKWM_FRAME_SKIP",
    "_PEKWM_TITLE",
    "_PEKWM_BG_PID",
    "_PEKWM_CMD",
    "_PEKWM_THEME",

    // ICCCM atoms
    "WM_NAME",
    "WM_ICON_NAME",
    "WM_HINTS",
    "WM_CLASS",
    "WM_STATE",
    "WM_CHANGE_STATE",
    "WM_PROTOCOLS",
    "WM_DELETE_WINDOW",
    "WM_COLORMAP_WINDOWS",
    "WM_TAKE_FOCUS",
    "WM_WINDOW_ROLE",
    "WM_CLIENT_MACHINE",

    // miscellaneous atoms
    "_MOTIF_WM_HINTS",

    "_XROOTPMAP_ID",
    "_XSETROOT_ID",
};

std::ostream&
operator<<(std::ostream& os, const Geometry& gm)
{
    os << "Geometry";
    os << " x:" << gm.x << " y: " << gm.y;
    os << " width: " << gm.width << " height: " << gm.height;
    return os;
}

/**
 * Helper class for XColor.
 */
class X11::ColorEntry {
public:
    ColorEntry(const std::string &name) : _name(name), _ref(0) { }
    ~ColorEntry(void) { }

    inline XColor *getColor(void) { return &_xc; }

    inline uint getRef(void) const { return _ref; }
    inline void incRef(void) { _ref++; }
    inline void decRef(void) { if (_ref > 0) { _ref--; } }

    inline bool operator==(const std::string &name) {
        return (::strcasecmp(_name.c_str(), name.c_str()) == 0);
    }

private:
    std::string _name;
    XColor _xc;
    uint _ref;
};

/**
 * Init X11 connection, must be called before any X11:: call is made.
 */
void
X11::init(Display *dpy, bool synchronous, bool honour_randr)
{
    if (_dpy) {
        throw std::string("X11, trying to create multiple instances");
    }

    _dpy = dpy;
    _honour_randr = honour_randr;

    if (synchronous) {
        XSynchronize(_dpy, True);
        XSetAfterFunction(_dpy, afterXlibCall);
    }
    XSetErrorHandler(handleXError);

    grabServer();

    _fd = ConnectionNumber(dpy);
    _screen = DefaultScreen(_dpy);
    _root = RootWindow(_dpy, _screen);

    _depth = DefaultDepth(_dpy, _screen);
    _visual = DefaultVisual(_dpy, _screen);
    _gc = DefaultGC(_dpy, _screen);
    XGCValues gv;
    gv.function = GXcopy;
    XChangeGC(_dpy, _gc, GCFunction, &gv);
    _colormap = DefaultColormap(_dpy, _screen);
    _modifier_map = XGetModifierMapping(_dpy);

    _screen_gm.width = WidthOfScreen(ScreenOfDisplay(_dpy, _screen));
    _screen_gm.height = HeightOfScreen(ScreenOfDisplay(_dpy, _screen));

    // create resize cursors
    _cursor_map[CURSOR_TOP_LEFT] = XCreateFontCursor(_dpy, XC_top_left_corner);
    _cursor_map[CURSOR_TOP] = XCreateFontCursor(_dpy, XC_top_side);
    _cursor_map[CURSOR_TOP_RIGHT] =
        XCreateFontCursor(_dpy, XC_top_right_corner);
    _cursor_map[CURSOR_LEFT] = XCreateFontCursor(_dpy, XC_left_side);
    _cursor_map[CURSOR_RIGHT] = XCreateFontCursor(_dpy, XC_right_side);
    _cursor_map[CURSOR_BOTTOM_LEFT] =
        XCreateFontCursor(_dpy, XC_bottom_left_corner);
    _cursor_map[CURSOR_BOTTOM] = XCreateFontCursor(_dpy, XC_bottom_side);
    _cursor_map[CURSOR_BOTTOM_RIGHT] =
        XCreateFontCursor(_dpy, XC_bottom_right_corner);
    // create other cursors
    _cursor_map[CURSOR_ARROW] = XCreateFontCursor(_dpy, XC_left_ptr);
    _cursor_map[CURSOR_MOVE]  = XCreateFontCursor(_dpy, XC_fleur);
    _cursor_map[CURSOR_RESIZE] = XCreateFontCursor(_dpy, XC_plus);

#ifdef HAVE_SHAPE
    {
        int dummy_error;
        _has_extension_shape =
            XShapeQueryExtension(_dpy, &_event_shape, &dummy_error);
    }
#endif // HAVE_SHAPE

#ifdef HAVE_XRANDR
    {
        int dummy_error;
        _has_extension_xrandr =
            XRRQueryExtension(_dpy, &_event_xrandr, &dummy_error);
    }
#endif // HAVE_XRANDR

#ifdef HAVE_X11_XKBLIB_H
    {
        int major = XkbMajorVersion;
        int minor = XkbMinorVersion;
        int ext_opcode, ext_ev, ext_err;
        _has_extension_xkb =
            XkbQueryExtension(_dpy, &ext_opcode, &ext_ev, &ext_err,
                              &major, &minor);
    }
#endif // HAVE_X11_XKBLIB_H

    // Now screen geometry has been read and extensions have been
    // looked for, read head information.
    initHeads();

    // initialize array values
    for (uint i = 0; i < (BUTTON_NO - 1); ++i) {
        _last_click_time[i] = 0;
    }

    // Figure out what keys the Num and Scroll Locks are
    setLockKeys();

    ungrabServer(true);

    _xc_default.pixel = BlackPixel(_dpy, _screen);
    _xc_default.red = _xc_default.green = _xc_default.blue = 0;

    assert(sizeof(atomnames)/sizeof(char*) == MAX_NR_ATOMS);
    if (! XInternAtoms(_dpy, const_cast<char**>(atomnames), MAX_NR_ATOMS,
                       0, _atoms)) {
        ERR("XInternAtoms did not return all requested atoms");
    }
}

//! @brief X11 destructor
void
X11::destruct(void) {
    if (_colours.size() > 0) {
        ulong *pixels = new ulong[_colours.size()];
        for (uint i=0; i < _colours.size(); ++i) {
            pixels[i] = _colours[i]->getColor()->pixel;
            delete _colours[i];
        }
        XFreeColors(_dpy, X11::getColormap(),
                    pixels, _colours.size(), 0);
        delete [] pixels;
    }

    if (_modifier_map) {
        XFreeModifiermap(_modifier_map);
    }

    for (const auto &cursor : _cursor_map) {
        XFreeCursor(_dpy, cursor);
    }

    // Under certain circumstances trying to restart pekwm can cause it to
    // use 100% of the CPU without making any progress with the restart.
    // This X11:sync() seems to be work around the issue (c.f. #300).
    X11::sync(True);

    XCloseDisplay(_dpy);
    _dpy = 0;
}

XColor *
X11::getColor(const std::string &color)
{
    // check for already existing entry
    auto it(_colours.begin());

    if (strcasecmp(color.c_str(), "EMPTY") == 0) {
        return &_xc_default;
    }

    for (; it != _colours.end(); ++it) {
        if (*(*it) == color) {
            (*it)->incRef();
            return (*it)->getColor();
        }
    }

    // create new entry
    ColorEntry *entry = new ColorEntry(color);

    // X alloc
    XColor dummy;
    if (XAllocNamedColor(_dpy, X11::getColormap(),
                         color.c_str(), entry->getColor(), &dummy) == 0) {
        ERR("failed to alloc color: " << color);
        delete entry;
        entry = 0;
    } else {
        _colours.push_back(entry);
        entry->incRef();
    }

    return entry ? entry->getColor() : &_xc_default;
}

void
X11::returnColor(XColor *xc)
{
    if (&_xc_default == xc) { // no need to return default color
        return;
    }

    auto it(_colours.begin());
    for (; it != _colours.end(); ++it) {
        if ((*it)->getColor() == xc) {
            (*it)->decRef();
            if (((*it)->getRef() == 0)) {
                ulong pixels[1] = { (*it)->getColor()->pixel };
                XFreeColors(X11::getDpy(), X11::getColormap(), pixels, 1, 0);

                delete *it;
                _colours.erase(it);
            }
            break;
        }
    }
}

/**
 * Figure out what keys the Num and Scroll Locks are
 */
void
X11::setLockKeys(void)
{
    _num_lock = getMaskFromKeycode(XKeysymToKeycode(_dpy, XK_Num_Lock));
    _scroll_lock = getMaskFromKeycode(XKeysymToKeycode(_dpy, XK_Scroll_Lock));
}

//! @brief Get next event using select to avoid signal blocking
//! @param ev Event to fill in.
//! @return true if event was fetched, else false.
bool
X11::getNextEvent(XEvent &ev, struct timeval *timeout)
{
    if (pending()) {
        XNextEvent(_dpy, &ev);
        return true;
    }

    int ret;
    fd_set rfds;

    flush();

    FD_ZERO(&rfds);
    FD_SET(_fd, &rfds);

    ret = select(_fd + 1, &rfds, nullptr, nullptr, timeout);
    if (ret > 0) {
        XNextEvent(_dpy, &ev);
    }

    return ret > 0;
}

//! @brief Grabs the server, counting number of grabs
bool
X11::grabServer(void)
{
    if (_server_grabs == 0) {
        TRACE("grabbing server");
        XGrabServer(_dpy);
        ++_server_grabs;
    } else {
        ++_server_grabs;
    }
    return _server_grabs == 1;
}

//! @brief Ungrabs the server, counting number of grabs
bool
X11::ungrabServer(bool sync)
{
    if (_server_grabs > 0) {
        if (--_server_grabs == 0) {
            if (sync) {
                TRACE("0 server grabs left, syncing and ungrab.");
                X11::sync(False);
            } else {
                TRACE("0 server grabs left, ungrabbing server.");
            }
            XUngrabServer(_dpy);
        }
    }
    return _server_grabs == 0;
}

//! @brief Grabs the keyboard
bool
X11::grabKeyboard(Window win)
{
    TRACE("grabbing keyboard");
    if (XGrabKeyboard(_dpy, win, false, GrabModeAsync, GrabModeAsync,
                      CurrentTime) == GrabSuccess) {
        return true;
    }
    ERR("failed to grab keyboard on " << win);
    return false;
}

//! @brief Ungrabs the keyboard
bool
X11::ungrabKeyboard(void)
{
    TRACE("ungrabbing keyboard");
    XUngrabKeyboard(_dpy, CurrentTime);
    return false;
}

//! @brief Grabs the pointer
bool
X11::grabPointer(Window win, uint event_mask, CursorType type)
{
    TRACE("grabbing pointer");
    auto cursor = type < _cursor_map.size() ? _cursor_map[type] : None;
    if (XGrabPointer(_dpy, win, false, event_mask, GrabModeAsync, GrabModeAsync,
                     None, cursor, CurrentTime) == GrabSuccess) {
        return true;
    }
    ERR("failed to grab pointer on " << win
        << ", event_mask " << event_mask
        << ", type " << type);
    return false;
}

//! @brief Ungrabs the pointer
bool
X11::ungrabPointer(void)
{
    TRACE("ungrabbing pointer");
    XUngrabPointer(_dpy, CurrentTime);
    return false;
}

//! @brief Refetches the root-window size.
bool
X11::updateGeometry(uint width, uint height)
{
#ifdef HAVE_XRANDR
  if (! _honour_randr || ! _has_extension_xrandr) {
    return false;
  }

  // The screen has changed geometry in some way. To handle this the
  // head information is read once again, the root window is re sized
  // and strut information is updated.
  initHeads();

  _screen_gm.width = width;
  _screen_gm.height = height;
  return true;
#else // ! HAVE_XRANDR
  return false;
#endif // HAVE_XRANDR
}

//! @brief Searches for the head closest to the coordinates x,y.
//! @return The nearest head.  Head numbers are indexed from 0.
uint
X11::getNearestHead(int x, int y)
{
  if(_heads.size() > 1) {
        // set distance to the highest uint value
        uint min_distance = std::numeric_limits<uint>::max();
        uint nearest_head = 0;

        uint distance;
        int head_t, head_b, head_l, head_r;
        for(uint head = 0; head < _heads.size(); ++head) {
            head_t = _heads[head].y;
            head_b = _heads[head].y + _heads[head].height;
            head_l = _heads[head].x;
            head_r = _heads[head].x + _heads[head].width;

            if(x > head_r) {
                if(y < head_t) {
                    // above and right of the head
                    distance = calcDistance(x, y, head_r, head_t);
                } else if(y > head_b) {
                    // below and right of the head
                    distance = calcDistance(x, y, head_r, head_b);
                } else {
                    // right of the head
                    distance = calcDistance(x, head_r);
                }
            } else if(x < head_l) {
                if(y < head_t) {
                    // above and left of the head
                    distance = calcDistance(x, y, head_l, head_t);
                } else if(y > head_b) {
                    // below and left of the head
                    distance = calcDistance(x, y, head_l, head_b);
                } else {
                    // left of the head
                    distance = calcDistance(x, head_l);
                }
            } else {
                if(y < head_t) {
                    // above the head
                    distance = calcDistance(y, head_t);
                } else if(y > head_b) {
                    // below the head
                    distance = calcDistance(y, head_b);
                } else {
                    // on the head
                    return head;
                }
            }


            if(distance < min_distance) {
                min_distance = distance;
                nearest_head = head;
            }
        }
        return nearest_head;
    } else {
        return 0;
    }
}

/**
 * Searches for the head that the pointer currently is on.
 *
 * @return Head number for the head that the cursor is on.
 */
uint
X11::getCursorHead(void)
{
    uint head = 0;

    if (_heads.size() > 1) {
        int x = 0, y = 0;
        getMousePosition(x, y);
        head = getNearestHead(x, y);
    }

    return head;
}

/**
 * Fills head_info with info about head nr head
 *
 * @param head Head number to examine
 * @param head_info Returning info about the head
 * @return true if xinerama is off or head exists.
 */
bool
X11::getHeadInfo(uint head, Geometry &head_info)
{
    if (head  < _heads.size()) {
        head_info.x = _heads[head].x;
        head_info.y = _heads[head].y;
        head_info.width = _heads[head].width;
        head_info.height = _heads[head].height;
        return true;
    } else {
        ERR("head " << head << " does not exist");
        return false;
    }
}

/**
 * Fill head_info with info about head at x/y.
 */
void
X11::getHeadInfo(int x, int y, Geometry &head_info)
{
    for (auto head : _heads) {
        if (x >= head.x && x <= signed(head.x + head.width)
            && y >= head.y && y <= signed(head.y + head.height)) {
            head_info.x = head.x;
            head_info.y = head.y;
            head_info.width = head.width;
            head_info.height = head.height;
            return;
        }
    }
    head_info = _screen_gm;
}

/**
 * Same as getHeadInfo but returns Geometry instead of filling it in.
 */
Geometry
X11::getHeadGeometry(uint head)
{
    Geometry gm(_screen_gm);
    getHeadInfo(head, gm);
    return gm;
}

const char*
X11::getAtomString(AtomName name)
{
    return atomnames[name];
}

AtomName
X11::getAtomName(Atom atom)
{
    for (int i = 0; i < MAX_NR_ATOMS; i++) {
        if (_atoms[i] == atom) {
            return static_cast<AtomName>(i);
        }
    }
    return MAX_NR_ATOMS;
}

bool
X11::getProperty(Window win, Atom atom, Atom type,
                 ulong expected, uchar **data_ret, ulong *actual)
{
    if (expected == 0) {
        expected = 1024;
    }

    uchar *data = nullptr;
    ulong read = 0, left = 0;
    do {
        if (data != nullptr) {
            X11::free(data);
            data = nullptr;
        }
        expected += left;

        Atom r_type;
        int r_format, status;
        status =
            XGetWindowProperty(_dpy, win, atom,
                               0L, expected, False, type,
                               &r_type, &r_format, &read, &left, &data);
        if (status != Success || type != r_type || read == 0) {
            if (data != nullptr) {
                X11::free(data);
                data = nullptr;
            }
            left = 0;
        }
    } while (left);

    if (actual) {
        *actual = read;
    }
    *data_ret = data;

    return data != nullptr;
}

bool
X11::getTextProperty(Window win, Atom atom, std::string &value)
{
    // Read text property, return if it fails.
    XTextProperty text_property;
    if (! XGetTextProperty(_dpy, win, &text_property, atom)
        || ! text_property.value || ! text_property.nitems) {
        return false;
    }

    if (text_property.encoding == XA_STRING) {
        value = reinterpret_cast<const char*>(text_property.value);
    } else {
        char **mb_list;
        int num;

        XmbTextPropertyToTextList(_dpy, &text_property, &mb_list, &num);
        if (mb_list && num > 0) {
            value = *mb_list;
            XFreeStringList(mb_list);
        }
    }

    X11::free(text_property.value);

    return true;
}

void*
X11::getEwmhPropData(Window win, AtomName prop, Atom type, int &num)
{
    Atom type_ret;
    int format_ret;
    ulong items_ret, after_ret;
    uchar *prop_data = 0;

    XGetWindowProperty(_dpy, win, _atoms[prop], 0, 0x7fffffff,
                       False, type, &type_ret, &format_ret, &items_ret,
                       &after_ret, &prop_data);
    num = items_ret;
    return prop_data;
}

void
X11::getMousePosition(int &x, int &y)
{
    Window d_root, d_win;
    int win_x, win_y;
    uint mask;

    XQueryPointer(_dpy, _root, &d_root, &d_win, &x, &y, &win_x, &win_y, &mask);
}

uint
X11::getButtonFromState(uint state)
{
    uint button = 0;

    if (state&Button1Mask)
        button = BUTTON1;
    else if (state&Button2Mask)
        button = BUTTON2;
    else if (state&Button3Mask)
        button = BUTTON3;
    else if (state&Button4Mask)
        button = BUTTON4;
    else if (state&Button5Mask)
        button = BUTTON5;

    return button;
}

int
X11::sendEvent(Window dest, Window win, Atom atom, long mask,
               long v1, long v2, long v3, long v4, long v5)
{
    XEvent e;
    e.type = e.xclient.type = ClientMessage;
    e.xclient.serial = 0;
    e.xclient.send_event = True;
    e.xclient.message_type = atom;
    e.xclient.window = win;
    e.xclient.format = 32;

    e.xclient.data.l[0] = v1;
    e.xclient.data.l[1] = v2;
    e.xclient.data.l[2] = v3;
    e.xclient.data.l[3] = v4;
    e.xclient.data.l[4] = v5;

    return sendEvent(dest, False, mask, &e);
}

int
X11::sendEvent(Window dest, Bool propagate, long mask, XEvent *ev)
{
    if (_dpy) {
        return XSendEvent(_dpy, dest, propagate, mask, ev);
    }
    return BadValue;
}

//! @brief Initialize head information
void
X11::initHeads(void)
{
    _heads.clear();

    // Read head information, randr has priority over xinerama then
    // comes ordinary X11 information.

    initHeadsRandr();
    if (! _heads.size()) {
        initHeadsXinerama();

        if (! _heads.size()) {
            addHead(Head(0, 0, _screen_gm.width, _screen_gm.height));
        }
    }
}

//! @brief Initialize head information from Xinerama
void
X11::initHeadsXinerama(void)
{
#ifdef HAVE_XINERAMA
    // Check if there are heads already initialized from example Randr
    if (! XineramaIsActive(_dpy)) {
        return;
    }

    int num_heads = 0;
    XineramaScreenInfo *infos = XineramaQueryScreens(_dpy, &num_heads);

    for (int i = 0; i < num_heads; ++i) {
        addHead(Head(infos[i].x_org, infos[i].y_org,
                     infos[i].width, infos[i].height));
    }

    X11::free(infos);
#endif // HAVE_XINERAMA
}

//! @brief Initialize head information from RandR
void
X11::initHeadsRandr(void)
{
#ifdef HAVE_XRANDR
    if (! _honour_randr || ! _has_extension_xrandr) {
        return;
    }

    XRRScreenResources *resources = XRRGetScreenResources(_dpy, _root);
    if (! resources) {
        return;
    }

    for (int i = 0; i < resources->noutput; ++i) {
        auto output = XRRGetOutputInfo(_dpy, resources, resources->outputs[i]);
        if (output->crtc) {
            auto crtc = XRRGetCrtcInfo(_dpy, resources, output->crtc);
            addHead(Head(crtc->x, crtc->y, crtc->width, crtc->height));
            XRRFreeCrtcInfo (crtc);
        }
        XRRFreeOutputInfo (output);
    }

    XRRFreeScreenResources (resources);
#endif // HAVE_XRANDR
}

/**
 * Lookup mask from keycode.
 *
 * @param keycode KeyCode to lookup.
 * @return Mask for keycode, 0 if something fails.
 */
uint
X11::getMaskFromKeycode(KeyCode keycode)
{
    // Make sure modifier mappings were looked up ok
    if (! _modifier_map || _modifier_map->max_keypermod < 1) {
        return 0;
    }

    // .h files states that modifiermap is an 8 * max_keypermod array.
    int max_info = _modifier_map->max_keypermod * 8;
    for (int i = 0; i < max_info; ++i) {
        if (_modifier_map->modifiermap[i] == keycode) {
            return MODIFIER_TO_MASK[i / _modifier_map->max_keypermod];
        }
    }

    return 0;
}

/**
 * Figure out what key you can press to generate mask
 *
 * @param mask Modifier mask to get keycode for.
 * @return KeyCode for mask, 0 if failing.
 */
KeyCode
X11::getKeycodeFromMask(uint mask)
{
    // Make sure modifier mappings were looked up ok
    if (! _modifier_map || _modifier_map->max_keypermod < 1) {
        return 0;
    }

    for (int i = 0; i < 8; ++i) {
        if (MODIFIER_TO_MASK[i] == mask) {
            // FIXME: Is iteration over the range required?
            return _modifier_map->modifiermap[i * _modifier_map->max_keypermod];
        }
    }

    return 0;
}

/**
 * Wrapper for XKeycodeToKeysym and XkbKeycodeToKeysym depending on
 * which one is available.
 */
#ifdef __GNUG__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#else // ! __GNUG__
#ifdef __SUNPRO_CC
#pragma error_messages (off,symdeprecated)
#endif // __SUNPRO_CC
#endif // __GNUG__
KeySym
X11::getKeysymFromKeycode(KeyCode keycode)
{
#ifdef HAVE_X11_XKBLIB_H
    if (_has_extension_xkb)
        return XkbKeycodeToKeysym(_dpy, keycode, 0, 0);
    else
#endif
        return XKeycodeToKeysym(_dpy, keycode, 0);
}
#ifdef __GNUG__
#pragma GCC diagnostic pop
#endif // __GNUG__

/**
 * Parse string and set on geometry, same format as XParseGeometry
 * however both size and position can be given in percent.
 *
 * Format: [=][<width>{xX}<height>][{+-}<xoffset>{+-}<yoffset>]
 */
int
X11::parseGeometry(const std::string& str, Geometry& gm)
{
    int mask = 0;
    if (str.size() < 3) {
        // no valid geometry can fit in less than 3 characters.
        return mask;
    }

    // skip initial = if given
    const char *cstr = str.c_str();
    std::string::size_type s_start = str[0] == '=' ? 1 : 0;
    std::string::size_type s_end = str.find_first_of("+-", s_start);

    int ret;
    if (s_end == std::string::npos) {
        s_end = str.size();
    } else {
        // position
        std::string::size_type y_start = str.find_first_of("+-", s_end + 1);
        if ((ret = parseGeometryVal(cstr + s_end + 1,
                                    cstr + y_start, gm.x)) > 0) {
            mask |= X_VALUE;
            if (str[s_end] == '-') {
                mask |= X_NEGATIVE;
            }
            if (ret == 2) {
                mask |= X_PERCENT;
            }
        }
        if ((ret = parseGeometryVal(cstr + y_start + 1,
                                    cstr + str.size(), gm.y)) > 0) {
            mask |= Y_VALUE;
            if (str[y_start] == '-') {
                mask |= Y_NEGATIVE;
            }
            if (ret == 2) {
                mask |= Y_PERCENT;
            }
        }
    }

    if (s_end > s_start) {
        // size
        int width, height;
        std::string::size_type h_start = str.find_first_of("xX", s_start);
        if ((ret = parseGeometryVal(cstr + s_start, cstr + h_start, width)) > 0
            && width > 0) {
            gm.width = width;
            mask |= WIDTH_VALUE;
            if (ret == 2) {
                mask |= WIDTH_PERCENT;
            }
        }
        if ((ret = parseGeometryVal(cstr + h_start + 1,
                                    cstr + s_end, height)) > 0
            && height > 0) {
            gm.height = height;
            mask |= HEIGHT_VALUE;
            if (ret == 2) {
                mask |= HEIGHT_PERCENT;
            }
        }
    }

    return mask;
}

int
X11::parseGeometryVal(const char *cstr, const char *e_end, int &val)
{
    char *end = 0;
    val = strtoll(cstr, &end, 10);
    if (*end == '%') {
        if (val < 0 || val > 100) {
            return 0;
        }
        return 2;
    }
    return end == e_end ? 1 : 0;
}

Display *X11::_dpy;
bool X11::_honour_randr = false;
int X11::_fd = -1;
int X11::_screen = -1;
int X11::_depth = -1;
Geometry X11::_screen_gm;
Window X11::_root = None;
Visual *X11::_visual = 0;
GC X11::_gc = None;
Colormap X11::_colormap = None;
XModifierKeymap *X11::_modifier_map;
bool X11::_has_extension_shape = false;
int X11::_event_shape = -1;
bool X11::_has_extension_xkb = false;
bool X11::_has_extension_xinerama = false;
bool X11::_has_extension_xrandr = false;
int X11::_event_xrandr = -1;
uint X11::_num_lock;
uint X11::_scroll_lock;
std::vector<Head> X11::_heads;
uint X11::_server_grabs;
Time X11::_last_event_time;
Window X11::_last_click_id = None;
Time X11::_last_click_time[BUTTON_NO - 1];
std::vector<X11::ColorEntry*> X11::_colours;
XColor X11::_xc_default;
std::array<Cursor, CURSOR_NONE> X11::_cursor_map;
