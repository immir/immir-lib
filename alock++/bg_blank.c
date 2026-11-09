/*
 * alock - bg_blank.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 - 2016 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 * This background module provides:
 *  -bg blank:color=<color>
 *
 * Used resources:
 *  ALock.Background.Blank.Color
 *
 */

#include "alock.h"

#include <stdlib.h>
#include <string.h>


static struct moduleData {
    Display *display;
    Window window;
    char *colorname;
} data = { 0 };


static void module_loadargs(const char *args) {

    if (!args || strstr(args, "blank:") != args)
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
    }

    free(arguments);
}

static int module_init(Display *dpy) {

    Screen *screen = DefaultScreenOfDisplay(dpy);
    Colormap colormap = DefaultColormapOfScreen(screen);
    XSetWindowAttributes xswa;
    XColor color;

    data.display = dpy;

    alock_alloc_color(dpy, colormap, data.colorname, "black", &color);

    xswa.override_redirect = True;
    xswa.colormap = colormap;
    xswa.background_pixel = color.pixel;

    data.window = XCreateWindow(dpy, RootWindowOfScreen(screen),
            0, 0, WidthOfScreen(screen), HeightOfScreen(screen), 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWColormap | CWBackPixel,
            &xswa);

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


struct aModuleBackground alock_bg_blank = {
    { "blank",
        module_loadargs,
        module_init,
        module_free,
    },
    module_getwindow,
};
