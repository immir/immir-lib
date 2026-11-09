/*
 * alock - input_dots.c
 * Copyright (c) 2025
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 * This input module provides:
 *  -input dots:dot_color=<color>,dot_radius=<int>,dot_spacing=<int>,dot_placement=<position>
 *
 */

#include "alock.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>


static struct moduleData {
    Display *display;
    Window window;

    /* Password progress tracking */
    unsigned int password_length;

    /* Configuration options */
    int dot_spacing;
    int dot_radius;
    char *dot_placement;
    char *dot_color;
    char *success_color;
    char *error_color;
    unsigned long error_timeout;   /* milliseconds */
    unsigned long success_timeout; /* milliseconds */

    int shape_available;           /* X Shape extension present */

} data = {
    NULL, None,                      /* display, window */
    0,                               /* password_length */
    20, 10,                          /* dot_spacing, dot_radius */
    NULL, NULL, NULL, NULL,          /* placement and colors - defaults handled in code */
    500, 300                         /* error_timeout, success_timeout */
};


/* Compute dot layout for `count` dots. Returns 0 (no dots) when nothing should
 * be drawn, otherwise fills the geometry out-params. Shared by the colored
 * drawing pass and the Shape mask so the two always agree. */
static int compute_dot_layout(unsigned int count, float scale,
                               unsigned int screen_width, unsigned int screen_height,
                               int *out_start_x, int *out_y,
                               int *out_radius, int *out_diameter) {
    if (count == 0)
        return 0;

    int effective_radius = (int)(data.dot_radius * scale);
    if (effective_radius <= 0)
        return 0;

    int dot_diameter = effective_radius * 2;
    int total_width = count * dot_diameter + (count - 1) * data.dot_spacing;
    int start_x, y;

    const char *placement = data.dot_placement ? data.dot_placement : "center_left";

    if (strcmp(placement, "top_left") == 0) {
        start_x = 10;
        y = 10 + effective_radius;
    }
    else if (strcmp(placement, "top_right") == 0) {
        start_x = screen_width - total_width - 10;
        y = 10 + effective_radius;
    }
    else if (strcmp(placement, "top_center") == 0) {
        start_x = (screen_width - total_width) / 2;
        y = 10 + effective_radius;
    }
    else if (strcmp(placement, "center_left") == 0) {
        start_x = screen_width / 4 - total_width / 2;
        y = screen_height / 2;
    }
    else if (strcmp(placement, "center_right") == 0) {
        start_x = (screen_width * 3) / 4 - total_width / 2;
        y = screen_height / 2;
    }
    else { /* center (default) */
        start_x = (screen_width - total_width) / 2;
        y = screen_height / 2;
    }

    *out_start_x = start_x;
    *out_y = y;
    *out_radius = effective_radius;
    *out_diameter = dot_diameter;
    return 1;
}


/* Shape the window's bounding region to only the dot circles, so that any
 * live background sibling (e.g. an embedded terminal from bg_term) shows
 * through everywhere else. With `count == 0` the window becomes fully
 * transparent. Harmless over solid backgrounds, which show their own color
 * behind the dots. No-op when the Shape extension is unavailable. */
static void update_shape(unsigned int count, float scale) {
    if (!data.shape_available)
        return;

    Display *dpy = data.display;
    Screen *screen = DefaultScreenOfDisplay(dpy);
    unsigned int width = WidthOfScreen(screen);
    unsigned int height = HeightOfScreen(screen);

    Pixmap mask = XCreatePixmap(dpy, data.window, width, height, 1);
    GC mgc = XCreateGC(dpy, mask, 0, NULL);

    /* Start fully transparent (all zero). */
    XSetForeground(dpy, mgc, 0);
    XFillRectangle(dpy, mask, mgc, 0, 0, width, height);

    int start_x, y, effective_radius, dot_diameter;
    if (compute_dot_layout(count, scale, width, height,
                           &start_x, &y, &effective_radius, &dot_diameter)) {
        XSetForeground(dpy, mgc, 1);
        for (unsigned int i = 0; i < count; i++) {
            int x = start_x + i * (dot_diameter + data.dot_spacing);
            XFillArc(dpy, mask, mgc, x, y - effective_radius,
                     dot_diameter, dot_diameter, 0, 360 * 64);
        }
    }

    XShapeCombineMask(dpy, data.window, ShapeBounding, 0, 0, mask, ShapeSet);

    XFreeGC(dpy, mgc);
    XFreePixmap(dpy, mask);
}


/* Draw password progress dots */
static void draw_progress_dots(Display *dpy, Window win, GC gc, Colormap colormap,
                                unsigned int screen_width, unsigned int screen_height,
                                unsigned int count, const char *color_name, float scale) {
    int start_x, y, effective_radius, dot_diameter;
    if (!compute_dot_layout(count, scale, screen_width, screen_height,
                            &start_x, &y, &effective_radius, &dot_diameter))
        return;

    XColor color;
    if (alock_alloc_color(dpy, colormap, color_name, "white", &color)) {
        XSetForeground(dpy, gc, color.pixel);

        for (unsigned int i = 0; i < count; i++) {
            int x = start_x + i * (dot_diameter + data.dot_spacing);
            XFillArc(dpy, win, gc, x, y - effective_radius,
                     dot_diameter, dot_diameter, 0, 360 * 64);
        }
    }
}


static void redraw_window(const char *color_name, float scale) {
    Display *dpy = data.display;
    Screen *screen = DefaultScreenOfDisplay(dpy);
    Colormap colormap = DefaultColormapOfScreen(screen);
    int width = WidthOfScreen(screen);
    int height = HeightOfScreen(screen);

    /* Shape the window to just the dots, then clear and draw */
    update_shape(data.password_length, scale);
    XClearWindow(dpy, data.window);

    /* Draw dots on our transparent window */
    if (data.password_length > 0) {
        GC gc = XCreateGC(dpy, data.window, 0, 0);
        draw_progress_dots(dpy, data.window, gc, colormap, width, height,
                          data.password_length, color_name, scale);
        XFreeGC(dpy, gc);
    }

    XFlush(dpy);
}


static void module_loadargs(const char *args) {
    if (!args || strstr(args, "dots:") != args)
        return;

    char *arguments = strdup(&args[5]);
    char *arg;
    char *tmp;

    for (tmp = arguments; tmp; ) {
        arg = strsep(&tmp, ",");

        if (strstr(arg, "dot_spacing=") == arg) {
            data.dot_spacing = strtol(&arg[12], NULL, 0);
        }
        else if (strstr(arg, "dot_radius=") == arg) {
            data.dot_radius = strtol(&arg[11], NULL, 0);
        }
        else if (strstr(arg, "dot_placement=") == arg) {
            free(data.dot_placement);
            char *value = &arg[14];
            size_t len = strlen(value);
            while (len > 0 && (isspace(value[len-1]) || value[len-1] == ','))
                len--;
            data.dot_placement = malloc(len + 1);
            if (data.dot_placement) {
                strncpy(data.dot_placement, value, len);
                data.dot_placement[len] = '\0';
            }
        }
        else if (strstr(arg, "dot_color=") == arg) {
            free(data.dot_color);
            data.dot_color = strdup(strchr(arg, '=') + 1);
        }
        else if (strstr(arg, "success_color=") == arg) {
            free(data.success_color);
            data.success_color = strdup(strchr(arg, '=') + 1);
        }
        else if (strstr(arg, "error_color=") == arg) {
            free(data.error_color);
            data.error_color = strdup(strchr(arg, '=') + 1);
        }
        else if (strstr(arg, "error_timeout=") == arg) {
            data.error_timeout = strtoul(&arg[14], NULL, 0);
        }
        else if (strstr(arg, "success_timeout=") == arg) {
            data.success_timeout = strtoul(&arg[16], NULL, 0);
        }
    }

    free(arguments);
}


static int module_init(Display *dpy) {
    Screen *screen = DefaultScreenOfDisplay(dpy);
    XSetWindowAttributes xswa;

    data.display = dpy;

    /* The Shape extension lets the window be transparent everywhere except the
     * dot circles, so a live background (e.g. bg_term) shows through. */
    int shape_event_base, shape_error_base;
    data.shape_available = XShapeQueryExtension(dpy, &shape_event_base, &shape_error_base);

    /* Create transparent window using ParentRelative */
    xswa.override_redirect = True;
    xswa.background_pixmap = ParentRelative;
    xswa.backing_store = NotUseful;
    xswa.save_under = False;

    data.window = XCreateWindow(dpy, RootWindowOfScreen(screen),
            0, 0, WidthOfScreen(screen), HeightOfScreen(screen), 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWBackPixmap | CWBackingStore | CWSaveUnder,
            &xswa);

    return 0;
}


static void module_free(void) {
    if (data.display && data.window != None)
        XDestroyWindow(data.display, data.window);

    free(data.dot_placement);
    data.dot_placement = NULL;
    free(data.dot_color);
    data.dot_color = NULL;
    free(data.success_color);
    data.success_color = NULL;
    free(data.error_color);
    data.error_color = NULL;
}


static Window module_getwindow(int screen) {
    if (screen == DefaultScreen(data.display))
        return data.window;
    return None;
}


static KeySym module_keypress(KeySym key) {
    /* Treat delete key as backspace */
    if (key == XK_Delete)
        return XK_BackSpace;

    return key;
}


static void module_setprogress(unsigned int length, unsigned int position) {
    (void)position;  /* unused */
    data.password_length = length;

    /* Normal input: use default color */
    const char *color = data.dot_color ? data.dot_color : "white";
    redraw_window(color, 1.0);
}


static void module_setstate(enum aInputState state) {
    debug("setstate: %d", state);

    Display *dpy = data.display;

    if (state == AINPUT_STATE_NONE) {
        /* Password cleared - either successful unlock or after error
         * Just clear the display, no animations (main loop handles that) */
        data.password_length = 0;
        update_shape(0, 1.0);  /* fully transparent */
        XClearWindow(dpy, data.window);
        XFlush(dpy);
        return;
    }

    if (state == AINPUT_STATE_INIT) {
        /* Show window and reset */
        data.password_length = 0;
        XMapWindow(dpy, data.window);
        XRaiseWindow(dpy, data.window);
        update_shape(0, 1.0);  /* fully transparent */
        XClearWindow(dpy, data.window);  /* Clear to transparent */
        return;
    }

    if (state == AINPUT_STATE_CHECK) {
        /* Password being validated - keep current appearance */
        /* Success will be handled by AINPUT_STATE_NONE, error by AINPUT_STATE_ERROR */
        return;
    }

    if (state == AINPUT_STATE_ERROR) {
        /* Wrong password - show error color immediately (no blocking sleep/animation)
         * The main event loop handles timing and animation cancellation */
        const char *color = data.error_color ? data.error_color : "red";
        redraw_window(color, 1.0);
        return;
    }

    if (state == AINPUT_STATE_VALID) {
        /* Successful password - show success color immediately (no blocking)
         * The main event loop handles timing */
        const char *color = data.success_color ? data.success_color : "green";
        redraw_window(color, 1.0);
        return;
    }
}


/* Accessor functions for timeout configuration */
unsigned long alock_input_dots_get_error_timeout(void) {
    return data.error_timeout;
}

unsigned long alock_input_dots_get_success_timeout(void) {
    return data.success_timeout;
}


struct aModuleInput alock_input_dots = {
    { "dots",
        module_loadargs,
        module_init,
        module_free,
    },
    module_getwindow,
    module_keypress,
    module_setstate,
    module_setprogress,
    module_dummy_setcapslock,
};
