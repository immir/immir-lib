/*
 * alock - bg_shade.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 - 2018 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 * This background module provides:
 *  -bg shade:color=<color>,shade=<int>,mono
 *
 * Used resources:
 *  ALock.Background.Shade.Color
 *  ALock.Background.Shade.Shade
 *  ALock.Background.Shade.Mono
 *
 */

#include "alock.h"

#include <stdlib.h>
#include <string.h>
#include <X11/extensions/Xrender.h>


static struct moduleData {
    Display *display;
    Window window;
    char *colorname;
    unsigned int shade;
    char monochrome;
} data = { NULL, None, NULL, 80, 0 };


static void module_loadargs(const char *args) {

    if (!args || strstr(args, "shade:") != args)
        return;

    char *arguments = strdup(&args[6]);
    char *arg;
    char *tmp;

    for (tmp = arguments; tmp; ) {
        arg = strsep(&tmp, ",");
        if (strstr(arg, "color=") == arg) {
            free(data.colorname);
            data.colorname = strdup(&arg[6]);
        }
        else if (strstr(arg, "shade=") == arg) {
            data.shade = strtol(&arg[6], NULL, 0);
        }
        else if (strcmp(arg, "mono") == 0) {
            data.monochrome = 1;
        }
    }

    free(arguments);
}

static int module_init(Display *dpy) {

    if (!alock_check_xrender(dpy))
        return -1;

    /* show warning message when value is out of reasonable range */
    if (data.shade > 100)
        fprintf(stderr, "[shade]: shade not in range [0, 100]\n");

    data.display = dpy;

    Screen *screen = DefaultScreenOfDisplay(dpy);
    Window root = RootWindowOfScreen(screen);
    Colormap colormap = DefaultColormapOfScreen(screen);
    GC gc = DefaultGCOfScreen(screen);
    int width = WidthOfScreen(screen);
    int height = HeightOfScreen(screen);
    int depth = DefaultDepthOfScreen(screen);

    /* grab whats on the screen */
    XImage *image = XGetImage(dpy, root, 0, 0, width, height, AllPlanes, ZPixmap);
    if (data.monochrome)  /* optional monochrome conversion */
        alock_grayscale_image(image, 0, 0, width, height);
    Pixmap src_pm = XCreatePixmap(dpy, root, width, height, depth);
    XPutImage(dpy, src_pm, gc, image, 0, 0, 0, 0, width, height);
    XDestroyImage(image);

    XColor color;
    alock_alloc_color(dpy, colormap, data.colorname, "black", &color);
    XGCValues tintval = { .foreground = color.pixel };

    Pixmap dst_pm = XCreatePixmap(dpy, root, width, height, depth);
    GC tintgc = XCreateGC(dpy, dst_pm, GCForeground, &tintval);
    XFillRectangle(dpy, dst_pm, tintgc, 0, 0, width, height);
    XFreeGC(dpy, tintgc);

    Visual *vis = DefaultVisualOfScreen(screen);

    /* Apply shade: blend src_pm over the tinted destination (dst_pm).
     * If shade is 0, just copy src_pm to dst_pm. */
    if (data.shade > 0) {
        alock_shade_pixmap(dpy, vis, src_pm, dst_pm, data.shade, 0, 0, 0, 0, width, height);
    } else {
        XCopyArea(dpy, src_pm, dst_pm, gc, 0, 0, width, height, 0, 0);
    }

    /* create final window */
    XSetWindowAttributes xswa = {
        .background_pixmap = dst_pm,
        .override_redirect = True,
        .colormap = colormap,
    };
    data.window = XCreateWindow(dpy, root,
            0, 0, width, height, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWColormap | CWBackPixmap,
            &xswa);

    XFreePixmap(dpy, src_pm);
    XFreePixmap(dpy, dst_pm);

    return 0;
}

static void module_free() {

    if (data.window != None) {
        XDestroyWindow(data.display, data.window);
        data.window = None;
    }

    free(data.colorname);
    data.colorname = NULL;

}

static Window module_getwindow(int screen) {
    (void)screen;
    return data.window;
}


struct aModuleBackground alock_bg_shade = {
    { "shade",
        module_loadargs,
        module_init,
        module_free,
    },
    module_getwindow,
};
