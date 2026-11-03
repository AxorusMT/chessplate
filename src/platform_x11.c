/* X11 backend: plain Xlib, no toolkit. */
#ifndef _WIN32

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#include "platform.h"

static Display *g_display;
static Window g_window;
static GC g_gc;
static Visual *g_visual;
static int g_depth;
static int g_width, g_height;
static Atom g_wm_delete;
static Atom g_clipboard, g_targets, g_utf8, g_text;
static char *g_clip_text;
static XImage *g_image;
static uint32_t *g_image_pixels;
static int g_image_w, g_image_h;
static bool g_pending_quit;

static void set_icon(const Image *icon) {
    if (!icon || !icon->pixels) return;
    size_t count = 2 + (size_t)icon->w * (size_t)icon->h;
    unsigned long *data = (unsigned long *)malloc(count * sizeof(unsigned long));
    if (!data) return;
    data[0] = (unsigned long)icon->w;
    data[1] = (unsigned long)icon->h;
    for (size_t i = 0; i < (size_t)icon->w * (size_t)icon->h; i++) {
        /* _NET_WM_ICON expects non-premultiplied ARGB. */
        uint32_t p = icon->pixels[i];
        uint32_t a = p >> 24;
        uint32_t r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
        if (a > 0 && a < 255) {
            r = r * 255 / a;
            g = g * 255 / a;
            b = b * 255 / a;
        }
        data[2 + i] = (unsigned long)((a << 24) | (r << 16) | (g << 8) | b);
    }
    Atom net_wm_icon = XInternAtom(g_display, "_NET_WM_ICON", False);
    XChangeProperty(g_display, g_window, net_wm_icon, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data,
                    (int)count);
    free(data);
}

bool platform_init(const char *title, int width, int height, int min_width, int min_height, const Image *icon) {
    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        fprintf(stderr, "chessplate: cannot open X display\n");
        return false;
    }
    int screen = DefaultScreen(g_display);
    XVisualInfo vinfo;
    if (XMatchVisualInfo(g_display, screen, 24, TrueColor, &vinfo)) {
        g_visual = vinfo.visual;
        g_depth = 24;
    } else {
        fprintf(stderr, "chessplate: a 24-bit TrueColor visual is required\n");
        XCloseDisplay(g_display);
        g_display = NULL;
        return false;
    }

    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.colormap = XCreateColormap(g_display, RootWindow(g_display, screen), g_visual, AllocNone);
    attrs.background_pixel = 0x1b1b1b;
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                       KeyPressMask | EnterWindowMask | LeaveWindowMask;
    g_window = XCreateWindow(g_display, RootWindow(g_display, screen), 0, 0, (unsigned)width, (unsigned)height, 0,
                             g_depth, InputOutput, g_visual, CWColormap | CWBackPixel | CWBorderPixel | CWEventMask,
                             &attrs);
    g_width = width;
    g_height = height;

    XStoreName(g_display, g_window, title);
    Atom net_wm_name = XInternAtom(g_display, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(g_display, "UTF8_STRING", False);
    XChangeProperty(g_display, g_window, net_wm_name, utf8, 8, PropModeReplace, (const unsigned char *)title,
                    (int)strlen(title));

    XClassHint *class_hint = XAllocClassHint();
    if (class_hint) {
        class_hint->res_name = (char *)"chessplate";
        class_hint->res_class = (char *)"Chessplate";
        XSetClassHint(g_display, g_window, class_hint);
        XFree(class_hint);
    }

    XSizeHints *hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize;
        hints->min_width = min_width;
        hints->min_height = min_height;
        XSetWMNormalHints(g_display, g_window, hints);
        XFree(hints);
    }

    g_wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, g_window, &g_wm_delete, 1);
    g_clipboard = XInternAtom(g_display, "CLIPBOARD", False);
    g_targets = XInternAtom(g_display, "TARGETS", False);
    g_utf8 = utf8;
    g_text = XInternAtom(g_display, "TEXT", False);

    set_icon(icon);

    g_gc = XCreateGC(g_display, g_window, 0, NULL);
    XMapWindow(g_display, g_window);
    XFlush(g_display);
    return true;
}

static void destroy_image(void) {
    if (g_image) {
        g_image->data = NULL; /* we own the pixel memory */
        XDestroyImage(g_image);
        g_image = NULL;
    }
    free(g_image_pixels);
    g_image_pixels = NULL;
    g_image_w = g_image_h = 0;
}

void platform_shutdown(void) {
    if (!g_display) return;
    destroy_image();
    free(g_clip_text);
    g_clip_text = NULL;
    XFreeGC(g_display, g_gc);
    XDestroyWindow(g_display, g_window);
    XCloseDisplay(g_display);
    g_display = NULL;
}

static void handle_selection_request(XSelectionRequestEvent *req) {
    XSelectionEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = SelectionNotify;
    reply.display = req->display;
    reply.requestor = req->requestor;
    reply.selection = req->selection;
    reply.target = req->target;
    reply.time = req->time;
    reply.property = None;

    Atom property = req->property != None ? req->property : req->target;
    if (g_clip_text) {
        if (req->target == g_targets) {
            Atom supported[4] = {g_targets, g_utf8, XA_STRING, g_text};
            XChangeProperty(g_display, req->requestor, property, XA_ATOM, 32, PropModeReplace,
                            (unsigned char *)supported, 4);
            reply.property = property;
        } else if (req->target == g_utf8 || req->target == XA_STRING || req->target == g_text) {
            XChangeProperty(g_display, req->requestor, property, req->target == g_text ? g_utf8 : req->target, 8,
                            PropModeReplace, (unsigned char *)g_clip_text, (int)strlen(g_clip_text));
            reply.property = property;
        }
    }
    XSendEvent(g_display, req->requestor, False, 0, (XEvent *)&reply);
}

void platform_set_clipboard(const char *utf8) {
    if (!g_display) return;
    free(g_clip_text);
    size_t len = strlen(utf8);
    g_clip_text = (char *)malloc(len + 1);
    if (!g_clip_text) return;
    memcpy(g_clip_text, utf8, len + 1);
    XSetSelectionOwner(g_display, g_clipboard, g_window, CurrentTime);
    XSetSelectionOwner(g_display, XA_PRIMARY, g_window, CurrentTime);
    XFlush(g_display);
}

static void process_event(XEvent *ev, InputState *in) {
    switch (ev->type) {
    case ConfigureNotify:
        g_width = ev->xconfigure.width;
        g_height = ev->xconfigure.height;
        break;
    case ClientMessage:
        if ((Atom)ev->xclient.data.l[0] == g_wm_delete) g_pending_quit = true;
        break;
    case DestroyNotify: g_pending_quit = true; break;
    case MotionNotify:
        in->mouse_x = (float)ev->xmotion.x;
        in->mouse_y = (float)ev->xmotion.y;
        in->mouse_inside = true;
        break;
    case EnterNotify:
        in->mouse_x = (float)ev->xcrossing.x;
        in->mouse_y = (float)ev->xcrossing.y;
        in->mouse_inside = true;
        break;
    case LeaveNotify:
        /* Keep tracking while a button is held (implicit grab). */
        if (!(in->down[0] || in->down[1] || in->down[2])) in->mouse_inside = false;
        break;
    case ButtonPress:
    case ButtonRelease: {
        bool press = ev->type == ButtonPress;
        in->mouse_x = (float)ev->xbutton.x;
        in->mouse_y = (float)ev->xbutton.y;
        in->mouse_inside = true;
        int button = -1;
        switch (ev->xbutton.button) {
        case Button1: button = MOUSE_LEFT; break;
        case Button2: button = MOUSE_MIDDLE; break;
        case Button3: button = MOUSE_RIGHT; break;
        case Button4:
            if (press) in->wheel += 1.0f;
            break;
        case Button5:
            if (press) in->wheel -= 1.0f;
            break;
        default: break;
        }
        if (button >= 0) {
            if (press) {
                in->pressed[button] = true;
                in->down[button] = true;
            } else {
                in->released[button] = true;
                in->down[button] = false;
            }
        }
        break;
    }
    case KeyPress: {
        KeySym sym = XLookupKeysym(&ev->xkey, 0);
        if (sym == XK_Escape) in->key_escape = true;
        break;
    }
    case SelectionRequest: handle_selection_request(&ev->xselectionrequest); break;
    case SelectionClear:
        if (ev->xselectionclear.selection == g_clipboard) {
            /* Another client owns the clipboard now. */
        }
        break;
    default: break;
    }
}

void platform_wait_events(InputState *in, int timeout_ms) {
    if (!g_display) {
        in->quit = true;
        return;
    }
    XFlush(g_display);
    if (XPending(g_display) == 0 && timeout_ms != 0) {
        int fd = ConnectionNumber(g_display);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv, *tvp = NULL;
        if (timeout_ms > 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            tvp = &tv;
        }
        int rc;
        do {
            rc = select(fd + 1, &fds, NULL, NULL, tvp);
        } while (rc < 0 && errno == EINTR);
    }
    while (XPending(g_display) > 0) {
        XEvent ev;
        XNextEvent(g_display, &ev);
        process_event(&ev, in);
    }
    if (g_pending_quit) in->quit = true;
}

void platform_window_size(int *width, int *height) {
    *width = g_width > 0 ? g_width : 1;
    *height = g_height > 0 ? g_height : 1;
}

void platform_present(const uint32_t *pixels, int width, int height) {
    if (!g_display) return;
    if (!g_image || g_image_w != width || g_image_h != height) {
        destroy_image();
        g_image_pixels = (uint32_t *)malloc((size_t)width * (size_t)height * 4u);
        if (!g_image_pixels) return;
        g_image = XCreateImage(g_display, g_visual, (unsigned)g_depth, ZPixmap, 0, (char *)g_image_pixels,
                               (unsigned)width, (unsigned)height, 32, width * 4);
        if (!g_image) {
            free(g_image_pixels);
            g_image_pixels = NULL;
            return;
        }
        g_image_w = width;
        g_image_h = height;
    }
    if (g_image->byte_order == LSBFirst && g_image->bits_per_pixel == 32 && g_image->red_mask == 0xff0000 &&
        g_image->blue_mask == 0xff) {
        memcpy(g_image_pixels, pixels, (size_t)width * (size_t)height * 4u);
    } else {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint32_t p = pixels[(size_t)y * (size_t)width + (size_t)x];
                unsigned long r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
                unsigned long value = 0;
                /* Generic mask-based conversion for unusual visuals. */
                unsigned long masks[3] = {g_image->red_mask, g_image->green_mask, g_image->blue_mask};
                unsigned long comps[3] = {r, g, b};
                for (int i = 0; i < 3; i++) {
                    unsigned long m = masks[i];
                    int shift = 0;
                    while (m && !(m & 1)) {
                        m >>= 1;
                        shift++;
                    }
                    int bits = 0;
                    while (m & 1) {
                        m >>= 1;
                        bits++;
                    }
                    value |= (bits >= 8 ? comps[i] << (bits - 8) : comps[i] >> (8 - bits)) << shift;
                }
                XPutPixel(g_image, x, y, value);
            }
        }
    }
    XPutImage(g_display, g_window, g_gc, g_image, 0, 0, 0, 0, (unsigned)width, (unsigned)height);
    XFlush(g_display);
}

#endif /* !_WIN32 */
