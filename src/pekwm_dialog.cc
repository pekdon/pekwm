//
// pekwm_dialog.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "pekwm.hh"

#include "CfgParser.hh"
#include "Debug.hh"
#include "FontHandler.hh"
#include "ImageHandler.hh"
#include "TextureHandler.hh"
#include "Theme.hh"
#include "PWinObj.hh"
#include "Render.hh"
#include "Util.hh"
#include "X11App.hh"
#include "X11.hh"

#include <algorithm>
#include <functional>
#include <iostream>
#include <iomanip>
#include <vector>
#include <sstream>

extern "C" {
#include <X11/Xutil.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>
}

static const uint WIDTH_DEFAULT = 250;
static const uint HEIGHT_DEFAULT = 50;

static FontHandler* _font_handler = nullptr;
static ImageHandler* _image_handler = nullptr;
static TextureHandler* _texture_handler = nullptr;

namespace pekwm
{
    FontHandler* fontHandler()
    {
        return _font_handler;
    }

    ImageHandler* imageHandler()
    {
        return _image_handler;
    }

    TextureHandler* textureHandler()
    {
        return _texture_handler;
    }
}

/**
 * Dialog
 *
 * --------------------------------------------
 * | TITLE                                    |
 * --------------------------------------------
 * | Image if any is displayed here           |
 * |                                          |
 * |                                          |
 * |                                          |
 * --------------------------------------------
 * | Message text goes here                   |
 * |                                          |
 * --------------------------------------------
 * |           [Option1] [Option2]            |
 * --------------------------------------------
 *
 */
class PekwmDialog : public X11App {
public:
    class Widget{
    public:
        virtual ~Widget(void)
        {
            if (_window != None) {
                X11::destroyWindow(_window);
            }
        }

        int getX(void) const { return _gm.x; }
        int getY(void) const { return _gm.y; }

        virtual bool setState(Window window, ButtonState state) {
            return false;
        }
        virtual bool click(Window window) { return false; }
        virtual void render(Render &rend) = 0;

        virtual void place(int x, int y, uint width, uint tot_height)
        {
            _gm.x = x;
            _gm.y = y;
            _gm.width = width;
        }

        /**
         * Get requested width, 0 means adapt to given width.
         */
        virtual uint widthReq(void) const { return 0; }

        /**
         * Get requested height, given the provided width.
         */
        virtual uint heightReq(uint width) const = 0;

    protected:
        Widget(Theme::DialogData* data, PWinObj &parent)
            : _data(data),
              _window(None),
              _parent(parent)
        {
        }

        void setWindow(Window window) { _window = window; }

    protected:
        Theme::DialogData *_data;
        Window _window;
        PWinObj &_parent;
        Geometry _gm;
    };

    class Button : public Widget {
    public:
        Button(Theme::DialogData* data, PWinObj& parent,
               std::function<void(int)> stop,
               int retcode, const std::wstring& text)
            : Widget(data, parent),
              _stop(stop),
              _retcode(retcode),
              _text(text),
              _font(data->getButtonFont()),
              _background(None),
              _state(BUTTON_STATE_FOCUSED)
        {
            _gm.width = widthReq();
            _gm.height = heightReq(_gm.width);
            _background = X11::createPixmap(_gm.width, _gm.height);

            XSetWindowAttributes attr;
            attr.background_pixmap = _background;
            attr.override_redirect = True;
            attr.event_mask =
                ButtonPressMask|ButtonReleaseMask|
                EnterWindowMask|LeaveWindowMask;

            setWindow(X11::createWindow(_parent.getWindow(),
                                        0, 0, _gm.width, _gm.height, 0,
                                        CopyFromParent, InputOutput,
                                        CopyFromParent,
                                        CWEventMask|CWOverrideRedirect|
                                        CWBackPixmap, &attr));
            X11::mapWindow(_window);
        }

        virtual ~Button(void)
        {
            X11::freePixmap(_background);
        }

        virtual bool setState(Window window, ButtonState state) override {
            if (window != _window) {
                return false;
            }
            _state = state;
            X11Render rend(None);
            render(rend);
            return true;
        }

        virtual bool click(Window window) override {
            if (window != _window) {
                return false;
            }
            if (_state == BUTTON_STATE_HOVER
                || _state == BUTTON_STATE_PRESSED) {
                _stop(_retcode);
            }
            return true;
        }

        virtual void place(int x, int y, uint width, uint tot_height) override {
            Widget::place(x, y, _gm.width, tot_height);
            X11::moveWindow(_window, _gm.x, _gm.y);
        }

        virtual uint widthReq(void) const override {
            return _font->getWidth(_text) + _data->padVert();
        }

        virtual uint heightReq(uint width) const override {
            return _font->getHeight() + _data->padHorz();
        }

        virtual void render(Render &rend) override {
            _data->getButton(_state)->render(_background, 0, 0,
                                            _gm.width, _gm.height);
            _font->setColor(_data->getButtonColor());
            _font->draw(_background,
                        _data->getPad(PAD_LEFT), _data->getPad(PAD_UP),
                        _text);

            X11::clearWindow(_window);
        }

    private:
        std::function<void(int)> _stop;
        int _retcode;
        std::wstring _text;
        PFont *_font;

        Pixmap _background;
        ButtonState _state;
    };

    class ButtonsRow : public Widget
    {
    public:
        ButtonsRow(Theme::DialogData* data, PWinObj& parent,
                   std::function<void(int)> stop,
                   std::vector<std::wstring> options)
            : Widget(data, parent)
        {
            int i = 0;
            for (auto it : options) {
                _buttons.push_back(new Button(_data, parent, stop, i++, it));
            }
        }

        virtual ~ButtonsRow(void)
        {
            for (auto it : _buttons) {
                delete it;
            }
        }

        virtual bool setState(Window window, ButtonState state) override {
            for (auto it : _buttons) {
                if (it->setState(window, state)) {
                    return true;
                }
            }
            return false;
        }

        virtual bool click(Window window) override {
            for (auto it : _buttons) {
                if (it->click(window)) {
                    return true;
                }
            }
            return false;

        }

        virtual void place(int x, int y, uint width, uint tot_height) override
        {
            Widget::place(x, y, width, tot_height);

            // place buttons centered on available width
            uint buttons_width = 0;
            for (auto it : _buttons) {
                buttons_width += it->widthReq();
            }
            buttons_width += _buttons.size() * _data->padHorz();

            x = (width - buttons_width) / 2;
            if (tot_height) {
                y = tot_height - _data->getPad(PAD_DOWN)
                    - _buttons[0]->heightReq(width);
            } else {
                y += _data->getPad(PAD_UP);
            }
            for (auto it : _buttons) {
                it->place(x, y, width, tot_height);
                x += it->widthReq() + _data->padHorz();
            }
        }

        virtual uint heightReq(uint width) const override {
            uint height = 0;
            for (auto it : _buttons) {
                uint height_req = it->heightReq(width);
                if (height_req > height) {
                    height = height_req;
                }
            }
            return height + _data->padVert();
        }

        virtual void render(Render &rend) override {
            for (auto it : _buttons) {
                it->render(rend);
            }
        }

    private:
        std::vector<Button*> _buttons;
    };

    class Image : public Widget {
    public:
        Image(Theme::DialogData* data, PWinObj& parent, PImage* image)
            : Widget(data, parent),
              _image(image)
        {
        }
        virtual ~Image(void) { }

        virtual uint widthReq(void) const override
        {
            return _image->getWidth();
        }

        virtual uint heightReq(uint width) const override
        {
            if (_image->getWidth() > width) {
                float aspect = float(_image->getWidth()) / _image->getHeight();
                return width / aspect;
            }
            return _image->getHeight();
        }

        virtual void render(Render &rend) override
        {
            if (_image->getWidth() > _gm.width) {
                float aspect = float(_image->getWidth()) / _image->getHeight();
                _image->draw(rend,
                             _gm.x, _gm.y, _gm.width, _gm.width / aspect);
            } else {
                // render image centered on available width
                uint x = (_gm.width - _image->getWidth()) / 2;
                _image->draw(rend,
                             x, _gm.y,
                             _image->getWidth(), _image->getHeight());
            }
        }

    private:
        PImage *_image;
    };

    class Text : public Widget {
    public:
        Text(Theme::DialogData* data, PWinObj& parent,
             const std::wstring& text, bool is_title)
            : Widget(data, parent),
              _font(is_title ? data->getTitleFont() : data->getTextFont()),
              _text(text),
              _is_title(is_title)
        {
            Util::splitString<wchar_t>(text, _words, L" \t");
        }
        virtual ~Text(void) { }

        virtual void place(int x, int y, uint width, uint tot_height) override
        {
            if (width != _gm.width) {
                _lines.clear();
            }
            Widget::place(x, y, width, tot_height);
        }

        virtual uint heightReq(uint width) const override
        {
            std::vector<std::wstring> lines;
            uint num_lines = getLines(width, lines);
            return _font->getHeight() * num_lines + _data->padVert();
        }

        virtual void render(Render &rend) override
        {
            if (_lines.empty()) {
                getLines(_gm.width, _lines);
            }

            _font->setColor(_is_title
                            ? _data->getTitleColor() : _data->getTextColor());

            uint y = _gm.y + _data->getPad(PAD_UP);
            for (auto line : _lines) {
                _font->draw(rend.getDrawable(),
                            _gm.x + _data->getPad(PAD_LEFT), y,
                            line);
                y += _font->getHeight();
            }
        }

    private:
        uint getLines(uint width, std::vector<std::wstring> &lines)  const
        {
            width -= _data->getPad(PAD_LEFT) + _data->getPad(PAD_RIGHT);

            std::wstring line;
            for (auto word : _words) {
                if (! line.empty()) {
                    line += L" ";
                }
                line += word;

                uint l_width = _font->getWidth(line);
                if (l_width > width) {
                    if (line == word) {
                        lines.push_back(line);
                    } else {
                        line = line.substr(0, line.size() - word.size() - 1);
                        lines.push_back(line);
                        line = word;
                    }
                }
            }

            if (! line.empty()) {
                lines.push_back(line);
            }

            return lines.size();
        }

    private:
        PFont *_font;
        std::wstring _text;
        std::vector<std::wstring> _words;
        std::vector<std::wstring> _lines;
        bool _is_title;
    };

    PekwmDialog(Theme::DialogData* data,
                const Geometry &gm,
                bool raise, const std::wstring& title, PImage* image,
                const std::wstring& message,
                std::vector<std::wstring> options)
        : X11App(gm, title.empty() ? L"pekwm_dialog" : title,
                 "dialog", "pekwm_dialog", WINDOW_TYPE_NORMAL),
          _data(data),
          _raise(raise),
          _background(None)
    {
        // TODO: setup size minimum based on image
        initWidgets(title, image, message, options);
        placeWidgets();
    }

    ~PekwmDialog(void)
    {
        for (auto it : _widgets) {
            delete it;
        }
        X11::freePixmap(_background);
    }

    virtual void handleEvent(XEvent *ev) override
    {
        switch (ev->type) {
        case ButtonPress:
            setState(ev->xbutton.window, BUTTON_STATE_PRESSED);
            break;
        case ButtonRelease:
            click(ev->xbutton.window);
            break;
        case ConfigureNotify:
            resize(ev->xconfigure.width, ev->xconfigure.height);
            break;
        case EnterNotify:
            setState(ev->xbutton.window, BUTTON_STATE_HOVER);
            break;
        case LeaveNotify:
            setState(ev->xbutton.window, BUTTON_STATE_FOCUSED);
            break;
        case MapNotify:
            break;
        case ReparentNotify:
            break;
        case UnmapNotify:
            break;
        default:
            DBG("UNKNOWN EVENT " << ev->type);
            break;
        }
    }

    virtual void resize(uint width, uint height) override
    {
        if (_gm.width == width && _gm.height == height) {
            return;
        }

        _gm.width = width;
        _gm.height = height;

        // FIXME: first pass calculating "important" height req, then
        // assign size left to image

        int y = 0;
        for (auto it : _widgets) {
            it->place(it->getX(), y, width, height);
            y += it->heightReq(width);
        }

        X11::freePixmap(_background);
        render();
    }

    void show(void)
    {
        render();
        if (_raise) {
            X11::mapRaised(_window);
        } else {
            X11::mapWindow(_window);
        }
    }

    void render(void)
    {
        if (_background == None) {
            _background = X11::createPixmap(_gm.width, _gm.height);
            X11::setWindowBackgroundPixmap(_window, _background);
        }
        X11Render rend(_background);
        _data->getBackground()->render(rend,
                                       0, 0, _gm.width, _gm.height);
        for (auto it : _widgets) {
            it->render(rend);
        }
        X11::clearWindow(_window);
    }

    void setState(Window window, ButtonState state)
    {
        for (auto it : _widgets) {
            if (it->setState(window, state)) {
                break;
            }
        }
    }

    void click(Window window)
    {
        for (auto it : _widgets) {
            if (it->click(window)) {
                break;
            }
        }
    }

protected:
    void initWidgets(const std::wstring& title, PImage* image,
                     const std::wstring& message,
                     std::vector<std::wstring> options)
    {
        if (title.size()) {
            _widgets.push_back(new Text(_data, *this, title, true));
        }
        if (image) {
            _widgets.push_back(new Image(_data, *this, image));
        }
        if (message.size()) {
            _widgets.push_back(new Text(_data, *this, message, false));
        }
        auto stop = [this](int retcode) { this->stop(retcode); };
        _widgets.push_back(new ButtonsRow(_data, *this, stop, options));
    }

    void placeWidgets(void)
    {
        // height is dependent on the available width, get requested
        // width first.
        uint width = _gm.width;
        for (auto it : _widgets) {
            uint width_req = it->widthReq();
            if (width_req && width_req > width) {
                width = width_req;
            }
        }
        if (width == 0) {
            width = WIDTH_DEFAULT;
        }

        uint y = 0;
        for (auto it : _widgets) {
            it->place(0, y, width, 0);
            y += it->heightReq(width);
        }

        PWinObj::resize(std::max(width, _gm.width), std::max(y, _gm.height));
    }

private:
    Theme::DialogData* _data;
    bool _raise;

    Pixmap _background;
    std::vector<Widget*> _widgets;
};

static void init(Display* dpy)
{
    _font_handler = new FontHandler();
    _image_handler = new ImageHandler();
    _texture_handler = new TextureHandler();
}

static void cleanup()
{
    delete _texture_handler;
    delete _image_handler;
    delete _font_handler;
}

static void usage(const char* name, int ret)
{
    std::cout << "usage: " << name
              << " [-dhitfl] [-o option|-o option...] message" << std::endl
              << "  -d --display dpy    Display" << std::endl
              << "  -h --help           Display this information" << std::endl
              << "  -i --image          Image under title" << std::endl
              << "  -o --option         Option (many allowed)" << std::endl
              << "  -t --title          Dialog title" << std::endl
              << "  -f --log-file        Set log file." << std::endl
              << "  -l --log-level       Set log level." << std::endl;
    exit(ret);
}

int main(int argc, char* argv[])
{
    const char* display = NULL;
    Geometry gm = { 0, 0, WIDTH_DEFAULT, HEIGHT_DEFAULT };
    int gm_mask = WIDTH_VALUE | HEIGHT_VALUE;
    bool raise;
    std::wstring title;
    std::string config_file = Util::getEnv("PEKWM_CONFIG_FILE");
    std::string image_name;
    std::vector<std::wstring> options;

    static struct option opts[] = {
        {"config", required_argument, NULL, 'c'},
        {"display", required_argument, NULL, 'd'},
        {"geometry", required_argument, NULL, 'g'},
        {"help", no_argument, NULL, 'h'},
        {"image", required_argument, NULL, 'i'},
        {"option", required_argument, NULL, 'o'},
        {"raise", no_argument, NULL, 'r'},
        {"title", required_argument, NULL, 't'},
        {"log-level", required_argument, NULL, 'l'},
        {"log-file", required_argument, NULL, 'f'},
        {NULL, 0, NULL, 0}
    };

    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error &e) {
        setlocale(LC_ALL, "");
    }

    Charset::init();

    int ch;
    while ((ch = getopt_long(argc, argv, "c:d:g:hi:o:rt:", opts, NULL)) != -1) {
        switch (ch) {
        case 'c':
            config_file = optarg;
            break;
        case 'd':
            display = optarg;
            break;
        case 'g':
            gm_mask |= X11::parseGeometry(optarg, gm);
            break;
        case 'h':
            usage(argv[0], 0);
            break;
        case 'i':
            image_name = optarg;
            Util::expandFileName(image_name);
            break;
        case 'o':
            options.push_back(Charset::to_wide_str(optarg));
            break;
        case 'r':
            raise = true;
            break;
        case 't':
            title = Charset::to_wide_str(optarg);
            break;
        case 'f':
            if (! Debug::setLogFile(optarg)) {
                std::cerr << "Failed to open log file " << optarg << std::endl;
            }
            break;
        case 'l':
            Debug::setLevel(Debug::getLevel(optarg));
            break;
        default:
            usage(argv[0], 1);
            break;
        }
    }

    if (config_file.empty()) {
        config_file = "~/.pekwm/config";
    }
    Util::expandFileName(config_file);

    if (options.empty()) {
        options.push_back(L"Ok");
    }

    std::wstring message;
    for (int i = optind; i < argc; i++) {
        if (i > optind) {
            message += L' ';
        }
        message += Charset::to_wide_str(argv[i]);
    }

    auto dpy = XOpenDisplay(display);
    if (! dpy) {
        auto actual_display = display ? display : Util::getEnv("DISPLAY");
        std::cerr << "Can not open display!" << std::endl
                  << "Your DISPLAY variable currently is set to: "
                  << actual_display << std::endl;
        return 1;
    }

    X11::init(dpy, true);
    init(dpy);

    _image_handler->path_push_back("./");
    PImage *image = nullptr;
    if (image_name.size()) {
        image = _image_handler->getImage(image_name);
        if (image) {
            image->setType(IMAGE_TYPE_SCALED);
        }
    }

    std::string theme_dir, theme_variant, theme_path;
    Util::getThemeDir(config_file, theme_dir, theme_variant, theme_path);

    int ret;
    {
        // run in separate scope to get Theme destructed before cleanup
        Theme theme(_font_handler, _image_handler, _texture_handler,
                    theme_dir, theme_variant);
        PekwmDialog dialog(theme.getDialogData(),
                           gm, raise, title, image, message, options);
        dialog.show();
        ret = dialog.main(60);
    }
    if (image) {
        _image_handler->returnImage(image);
    }

    cleanup();
    X11::destruct();
    Charset::destruct();

    return ret;
}
