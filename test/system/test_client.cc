/**
 * Client used for testing pekwm.
 */

#include <iostream>

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
}

void
query_pointer(Display *dpy, int screen, Window root)
{
    Window root_ret, child_ret;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    if (XQueryPointer(dpy, root, &root_ret, &child_ret,
                      &root_x, &root_y, &win_x, &win_y,
                      &mask)) {
        std::cout << "root"
                  << " x " << root_x << " y " << root_y << std::endl;
        std::cout << "child " << child_ret
                  << " x " << win_x << " y " << win_y << std::endl;
    } else {
        std::cerr << "ERROR: failed to query pointer" << std::endl;
    }
}

void
window(Display *dpy, int screen, Window root)
{
    XSetWindowAttributes attrs = {0};
    attrs.event_mask = PropertyChangeMask;
    unsigned long attrs_mask = CWEventMask;

    Window win = XCreateWindow(dpy, root,
                               0, 0, 100, 100, 0,
                               CopyFromParent, //depth
                               InputOutput, // class
                               CopyFromParent, // visual
                               attrs_mask,
                               &attrs);
    char wm_name[] = "test_client";
    char wm_class[] = "pekwm";
    XClassHint hint = {wm_name, wm_class};
    XSetClassHint(dpy, win, &hint);
    XMapWindow(dpy, win);

    std::cout << "Window " << win << std::endl;
    std::cout << "WindowHex " << std::showbase << std::hex << win << std::endl;

    XEvent ev;
    while (! XNextEvent(dpy, &ev)) {
    }
}

int
main(int argc, char *argv[])
{
    Display *dpy = XOpenDisplay(NULL);
    if (dpy == NULL) {
        std::cerr << "ERROR: unable to open display" << std::endl;
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    if (argc == 2 && std::string(argv[1]) == "query_pointer") {
        query_pointer(dpy, screen, root);
    } else {
        window(dpy, screen, root);
    }

    XCloseDisplay(dpy);

    return 0;
}
