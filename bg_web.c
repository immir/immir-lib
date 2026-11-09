/*
 * alock - bg_web.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 - 2018 Arkadiusz Bokowy
 *               2025 Animated web background module
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 * This background module provides:
 *  -bg web:url=<url>,bgcolor=<color>
 *
 * The module uses WebKitGTK to render HTML/CSS/JS animations.
 * URLs can be file:// for local files or http(s):// for remote content.
 *
 */

#include "alock.h"

#if ENABLE_WEBKIT

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gdk/gdkx.h>


static struct moduleData {
    Display *display;
    Window window;
    GtkWidget *gtk_window;
    GtkWidget *webview;
    char *url;
    char *bgcolor;
    int gtk_initialized;
} data = { 0 };


/* Process pending GTK events without blocking */
static void process_gtk_events(void) {
    while (gtk_events_pending())
        gtk_main_iteration_do(FALSE);
}

/* Public function to process GTK events - called from main event loop */
void alock_bg_web_process_events(void) {
    if (data.gtk_initialized)
        process_gtk_events();
}


static void module_loadargs(const char *args) {

    if (!args || strstr(args, "web:") != args)
        return;

    char *arguments = strdup(&args[4]);
    char *arg;
    char *tmp;

    for (tmp = arguments; tmp; ) {
        arg = strsep(&tmp, ",");
        if (strstr(arg, "url=") == arg) {
            free(data.url);
            data.url = strdup(&arg[4]);
        }
        else if (strstr(arg, "bgcolor=") == arg) {
            free(data.bgcolor);
            data.bgcolor = strdup(&arg[8]);
        }
    }

    free(arguments);
}


static int module_init(Display *dpy) {

    if (!data.url) {
        fprintf(stderr, "[web]: url not specified\n");
        return -1;
    }

    data.display = dpy;

    Screen *screen = DefaultScreenOfDisplay(dpy);
    const int rwidth = WidthOfScreen(screen);
    const int rheight = HeightOfScreen(screen);

    /* Initialize GTK if not already done */
    if (!data.gtk_initialized) {
        /* Force software rendering to avoid GPU/EGL errors in VMs */
        setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);

        int argc = 0;
        char **argv = NULL;
        gtk_init(&argc, &argv);
        data.gtk_initialized = 1;
    }

    /* Create GTK window */
    data.gtk_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(data.gtk_window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(data.gtk_window), rwidth, rheight);
    gtk_window_move(GTK_WINDOW(data.gtk_window), 0, 0);

    /* Prevent the window from being resized or moved by window manager */
    gtk_window_set_resizable(GTK_WINDOW(data.gtk_window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(data.gtk_window), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);

    /* Create the WebKit web view */
    data.webview = webkit_web_view_new();

    /* Configure WebKit settings for lock screen use */
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(data.webview));

    /* Enable JavaScript for animations */
    webkit_settings_set_enable_javascript(settings, TRUE);

    /* Disable developer tools and other interactive features */
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, FALSE);

    /* Disable smooth scrolling and scroll bars (not needed) */
    webkit_settings_set_enable_smooth_scrolling(settings, FALSE);

    /* Set background color for the web view */
    GdkRGBA bg_color = { 0.0, 0.0, 0.0, 1.0 };
    if (data.bgcolor && gdk_rgba_parse(&bg_color, data.bgcolor)) {
        webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(data.webview), &bg_color);
    } else {
        webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(data.webview), &bg_color);
    }

    /* Add web view to the window */
    gtk_container_add(GTK_CONTAINER(data.gtk_window), data.webview);

    /* Realize the window to get access to GdkWindow before showing */
    gtk_widget_realize(data.gtk_window);

    /* Set override-redirect on the underlying X11 window */
    GdkWindow *gdk_win = gtk_widget_get_window(data.gtk_window);
    gdk_window_set_override_redirect(gdk_win, TRUE);

    /* Get the X11 window ID for alock's window management */
    data.window = gdk_x11_window_get_xid(gdk_win);

    /* Show all widgets */
    gtk_widget_show_all(data.gtk_window);

    /* Load the URL */
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(data.webview), data.url);

    /* Process GTK events to ensure the web view starts rendering */
    process_gtk_events();

    return 0;
}


static void module_free() {

    /* Destroy GTK window (this also destroys the webview and underlying X11 window) */
    if (data.gtk_window) {
        gtk_widget_destroy(data.gtk_window);
        data.gtk_window = NULL;
        data.webview = NULL;
        data.window = None;
    }

    free(data.url);
    data.url = NULL;
    free(data.bgcolor);
    data.bgcolor = NULL;
}


static Window module_getwindow(int screen) {
    (void)screen;
    return data.window;
}


struct aModuleBackground alock_bg_web = {
    { "web",
        module_loadargs,
        module_init,
        module_free,
    },
    module_getwindow,
};

#endif /* ENABLE_WEBKIT */
