/*
 * alock - alock.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 - 2018 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "alock.h"

#include <ctype.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/mman.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>


static struct aModuleAuth *alock_modules_auth[] = {
#if ENABLE_PAM
    &alock_auth_pam,
#endif
    &alock_auth_plaintext,
    NULL
};

static struct aModuleBackground *alock_modules_background[] = {
    &alock_bg_blank,
    &alock_bg_image,
#if ENABLE_XRENDER
    &alock_bg_shade,
#endif
#if ENABLE_WEBKIT
    &alock_bg_web,
#endif
    &alock_bg_term,
    &alock_bg_none,
    NULL
};

static struct aModuleInput *alock_modules_input[] = {
    &alock_input_dots,   /* Default: dots with center_left placement */
    &alock_input_none,
    NULL
};


/* Find a module from a module list by name. */
static void *find_module(void **module_list, const char *arg) {
    if (!arg)
        return NULL;
    for (int i = 0; module_list[i] != NULL; i++) {
        struct aModule *m = (struct aModule *)module_list[i];
        if (strstr(arg, m->name) == arg)
            return module_list[i];
    }
    return NULL;
}


/* Register alock instance. This function returns 0 on success or -1 when
 * another instance is already registered. Note, that this function does
 * not guarantee 100% assurance, it is NOT multi-process safe! */
static int registerInstance(Display *display) {

    Window root = DefaultRootWindow(display);
    Atom atom = XInternAtom(display, "ALOCK_INSTANCE_PID", False);
    pid_t pid = 0;

    { /* detect previous instance */

        Atom ret_type;
        int ret_fmt;
        unsigned long ret_nb;
        unsigned long ret_bleft;
        pid_t *ret_data;
        int rv;

        rv = XGetWindowProperty(display, root, atom,
                0L, 1L, False, XA_CARDINAL, &ret_type, &ret_fmt,
                &ret_nb, &ret_bleft, (unsigned char **)&ret_data);
        if (rv == Success && ret_type != None && ret_data) {
            pid = *ret_data;
            XFree(ret_data);
        }

        if (pid && kill(pid, 0) == 0)
            /* atom was registered and the found process is alive */
            return -1;

    }

    debug("registering instance");
    pid = getpid();
    XChangeProperty(display, root, atom, XA_CARDINAL,
            sizeof(pid_t) * 8, PropModeReplace, (unsigned char *)&pid, 1);
    XFlush(display);

    return 0;
}

/* Unregister alock instance. */
static void unregisterInstance(Display *display) {

    Window root = DefaultRootWindow(display);
    Atom atom = XInternAtom(display, "ALOCK_INSTANCE_PID", True);

    if (atom != None) {
        debug("unregistering instance");
        XDeleteProperty(display, root, atom);
    }

}

/* Create a blank cursor for hiding the pointer. */
static Cursor create_blank_cursor(Display *display, Window win) {
    Pixmap pixmap = XCreatePixmap(display, win, 1, 1, 1);
    GC gc = XCreateGC(display, pixmap, 0, NULL);
    XDrawPoint(display, pixmap, gc, 0, 0);
    XFreeGC(display, gc);
    XColor black = { .red = 0, .green = 0, .blue = 0 };
    Cursor cursor = XCreatePixmapCursor(display, pixmap, pixmap, &black, &black, 0, 0);
    XFreePixmap(display, pixmap);
    return cursor;
}

/* Lock current display and grab pointer and keyboard. On successful
 * lock this function returns 0, otherwise -1. */
static int lockDisplay(Display *display, struct aModules *modules) {

    Window window;
    Cursor cursor;
    int i;

    for (i = 0; i < ScreenCount(display); i++) {

        /* raise background window */
        if ((window = modules->background->getwindow(i)) != None) {

            Window window_input;

            if ((window_input = modules->input->getwindow(i)) != None)
                XReparentWindow(display, window_input, window, 0, 0);

            XMapWindow(display, window);
            XRaiseWindow(display, window);

        }

        /* receive notification about root window geometry change */
        XSelectInput(display, RootWindow(display, i), StructureNotifyMask);
    }

    /* grab pointer and keyboard from the default screen */
    window = DefaultRootWindow(display);
    cursor = create_blank_cursor(display, window);

    if (XGrabPointer(display, window, False, None, GrabModeAsync, GrabModeAsync, None,
                cursor, CurrentTime) != GrabSuccess) {
        fprintf(stderr, "error: grab pointer failed\n");
        XFreeCursor(display, cursor);
        return -1;
    }

    /* try to grab 2 times, another process (windowmanager) may have grabbed
     * the keyboard already */
    if (XGrabKeyboard(display, window, True, GrabModeAsync, GrabModeAsync,
                CurrentTime) != GrabSuccess) {
        sleep(1);
        if (XGrabKeyboard(display, window, True, GrabModeAsync, GrabModeAsync,
                    CurrentTime) != GrabSuccess) {
            fprintf(stderr, "error: grab keyboard failed\n");
            XFreeCursor(display, cursor);
            return -1;
        }
    }

    XFreeCursor(display, cursor);
    return 0;
}


/* Wrapper for authentication logic. Returns 0 on success, 1 on failure. */
static int handle_authentication(Display *display, struct aModules *modules,
                                  wchar_t *pass, unsigned int *pass_len,
                                  unsigned int *pass_pos,
                                  unsigned long *keypress_time, int *retry_event) {
    char rbuf[sizeof(wchar_t) * 128];
    int rv;

    modules->input->setstate(AINPUT_STATE_CHECK);

    wcstombs(rbuf, pass, sizeof(rbuf) - 1);
    rbuf[sizeof(rbuf) - 1] = '\0';
    rv = modules->auth->authenticate(rbuf);

    memset(rbuf, 0, sizeof(rbuf));
    memset(pass, 0, 128 * sizeof(wchar_t));
    *pass_pos = *pass_len = 0;

    if (rv == 0) {
        modules->input->setstate(AINPUT_STATE_VALID);
        if (modules->input == &alock_input_dots)
            usleep(alock_input_dots_get_success_timeout() * 1000);
        else
            usleep(300000);
        return 0;
    }

    modules->input->setstate(AINPUT_STATE_ERROR);
    unsigned long anim_start = alock_mtime();
    int animation_cancelled = 0;
    unsigned long error_timeout = 500;
    if (modules->input == &alock_input_dots)
        error_timeout = alock_input_dots_get_error_timeout();

    XEvent ev;
    while (alock_mtime() - anim_start < error_timeout) {
        if (XCheckMaskEvent(display, KeyPressMask, &ev)) {
            animation_cancelled = 1;
            break;
        }
        usleep(16000);
    }

    if (modules->input->setprogress)
        modules->input->setprogress(0, 0);

    modules->input->setstate(AINPUT_STATE_NONE);
    *keypress_time = 0;

    if (animation_cancelled)
        *retry_event = 1;

    return 1;
}

/* Handle user keypress events for password input. */
static void handle_keypress(XEvent *ev, struct aModules *modules, wchar_t *pass,
                             unsigned int *pass_len, unsigned int *pass_pos,
                             unsigned long *keypress_time) {
    KeySym ks;
    char cbuf[10];
    int clen;

    *keypress_time = alock_mtime();
    clen = XLookupString(&ev->xkey, cbuf, sizeof(cbuf), &ks, NULL);
    debug("key input: %lx, %d, `%.*s`", ks, clen, clen, cbuf);

    if (modules->input->setcapslock)
        modules->input->setcapslock(ev->xkey.state & LockMask);

    if (clen == 1 && iscntrl(cbuf[0]))
        switch (cbuf[0]) {
        case 0x03: ks = XK_Escape; break;
        case 0x08: ks = XK_BackSpace; break;
        case 0x0A: case 0x0D: ks = XK_Return; break;
        }

    ks = modules->input->keypress(ks);

    switch (ks) {
    case NoSymbol:
        break;
    case XK_Escape:
    case XK_Clear:
        *pass_pos = *pass_len = 0;
        pass[0] = '\0';
        if (modules->input->setprogress)
            modules->input->setprogress(0, 0);
        break;
    case XK_Begin:
    case XK_Home:
        *pass_pos = 0;
        break;
    case XK_End:
        *pass_pos = *pass_len;
        break;
    case XK_Left:
        if (*pass_pos > 0) (*pass_pos)--;
        break;
    case XK_Right:
        if (*pass_pos < *pass_len) (*pass_pos)++;
        break;
    case XK_Delete:
        if (*pass_pos < *pass_len) {
            wmemmove(&pass[*pass_pos], &pass[*pass_pos + 1], *pass_len - *pass_pos);
            (*pass_len)--;
        }
        break;
    case XK_BackSpace:
        if (*pass_pos > 0) {
            wmemmove(&pass[*pass_pos - 1], &pass[*pass_pos], *pass_len - *pass_pos + 1);
            (*pass_pos)--;
            (*pass_len)--;
        }
        break;
    case XK_KP_Enter:
    case XK_Linefeed:
    case XK_Return:
        /* This case is handled in the main event loop. */
        break;
    default:
        if (clen > 0 && !iscntrl(cbuf[0]) && *pass_len < (128 - 1)) {
            wmemmove(&pass[*pass_pos + 1], &pass[*pass_pos], *pass_len - *pass_pos + 1);
            mbtowc(&pass[*pass_pos], cbuf, clen);
            (*pass_pos)++;
            (*pass_len)++;
        }
        break;
    }

    if (modules->input->setprogress)
        modules->input->setprogress(*pass_len, *pass_pos);

    debug("entered phrase [%zu]: `%ls`", wcslen(pass), pass);
}

static void eventLoop(Display *display, struct aModules *modules) {
    XEvent ev;
    wchar_t pass[128];
    unsigned int pass_pos = 0, pass_len = 0;
    unsigned long keypress_time = 0;

    mlock(pass, sizeof(pass));
    debug("entering event main loop");

    for (;;) {
        if (keypress_time) {
            if (!XCheckMaskEvent(display, KeyPressMask | StructureNotifyMask, &ev)) {
                if (alock_mtime() - keypress_time > 5000) {
                    modules->input->setstate(AINPUT_STATE_NONE);
                    keypress_time = 0;
                }
#if ENABLE_WEBKIT
                alock_bg_web_process_events();
#endif
                alock_bg_term_process_events();
                usleep(25000);
                continue;
            }
        } else {
            /* Use non-blocking event check to allow GTK event processing */
            while (!XCheckMaskEvent(display, KeyPressMask | StructureNotifyMask, &ev)) {
#if ENABLE_WEBKIT
                alock_bg_web_process_events();
#endif
                alock_bg_term_process_events();
                usleep(16000);  /* ~60fps for smooth animations */
            }
        }

        int retry_event = 0;
        do {
            retry_event = 0;
            if (ev.type == KeyPress) {
                if (keypress_time == 0) {
                    modules->input->setstate(AINPUT_STATE_INIT);
                    keypress_time = alock_mtime();
                    pass_pos = pass_len = 0;
                    pass[0] = L'\0';
                }

                KeySym ks;
                XLookupString(&ev.xkey, NULL, 0, &ks, NULL);

                if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_Linefeed) {
                    if (handle_authentication(display, modules, pass, &pass_len, &pass_pos, &keypress_time, &retry_event) == 0) {
                        return; /* Authentication successful */
                    }
                } else {
                    handle_keypress(&ev, modules, pass, &pass_len, &pass_pos, &keypress_time);
                }
            } else if (ev.type == ConfigureNotify) {
                debug("received configure notify event");
            }
        } while (retry_event);
    }
}

int main(int argc, char **argv) {

    int opt;
    struct option longopts[] = {
        {"help", no_argument, NULL, 'h'},
        {"modules", no_argument, NULL, 'm'},
        {"auth", required_argument, NULL, 'a'},
        {"bg", required_argument, NULL, 'b'},
        {"input", required_argument, NULL, 'i'},
        {0, 0, 0, 0},
    };

    Display *display;
    struct aModules modules;
    int retval;

    const char *args_auth = NULL;
    const char *args_background = NULL;
    const char *args_input = NULL;

    /* set-up default modules */
    modules.auth = alock_modules_auth[0];
    modules.background = alock_modules_background[0];
    modules.input = alock_modules_input[0];

    /* load configuration file */
    {
        struct aConfig file_config;
        char *config_path = alock_get_config_path();
        if (config_path && alock_load_config(config_path, &file_config) == 0) {
            debug("loaded configuration from: %s", config_path);

            /* Apply config file settings as defaults (overridden by CLI args) */
            if (strlen(file_config.auth_module) > 0) {
                /* big enough for "module:args" with both at full length */
                static char auth_str[sizeof(file_config.auth_module) + sizeof(file_config.auth_args)];
                strncpy(auth_str, file_config.auth_module, sizeof(auth_str) - 1);
                auth_str[sizeof(auth_str) - 1] = '\0';
                if (strlen(file_config.auth_args) > 0) {
                    strncat(auth_str, ":", sizeof(auth_str) - strlen(auth_str) - 1);
                    strncat(auth_str, file_config.auth_args, sizeof(auth_str) - strlen(auth_str) - 1);
                }
                args_auth = auth_str;
                modules.auth = find_module((void **)alock_modules_auth, args_auth);
            }

            if (strlen(file_config.bg_module) > 0) {
                static char bg_str[sizeof(file_config.bg_module) + sizeof(file_config.bg_args)];
                strncpy(bg_str, file_config.bg_module, sizeof(bg_str) - 1);
                bg_str[sizeof(bg_str) - 1] = '\0';
                if (strlen(file_config.bg_args) > 0) {
                    strncat(bg_str, ":", sizeof(bg_str) - strlen(bg_str) - 1);
                    strncat(bg_str, file_config.bg_args, sizeof(bg_str) - strlen(bg_str) - 1);
                }
                args_background = bg_str;
                modules.background = find_module((void **)alock_modules_background, args_background);
            }

            if (strlen(file_config.input_module) > 0) {
                static char input_str[sizeof(file_config.input_module) + sizeof(file_config.input_args)];
                strncpy(input_str, file_config.input_module, sizeof(input_str) - 1);
                input_str[sizeof(input_str) - 1] = '\0';
                if (strlen(file_config.input_args) > 0) {
                    strncat(input_str, ":", sizeof(input_str) - strlen(input_str) - 1);
                    strncat(input_str, file_config.input_args, sizeof(input_str) - strlen(input_str) - 1);
                }
                args_input = input_str;
                modules.input = find_module((void **)alock_modules_input, args_input);
            }
        }
    }

    /* parse options */
    while ((opt = getopt_long_only(argc, argv, "hma:b:c:i:", longopts, NULL)) != -1)
        switch (opt) {
        case 'h':
            printf("alock - A simple screen locker for X11\n\n");

            printf("USAGE:\n");
            printf("  %s [OPTIONS]\n\n", argv[0]);

            printf("DESCRIPTION:\n");
            printf("  alock locks the X11 display and requires authentication to unlock.\n");
            printf("  It provides modular authentication, visual feedback, and customization\n");
            printf("  through command-line options or a configuration file.\n\n");

            printf("COMMAND-LINE OPTIONS:\n");
            printf("  -help, -h              Show this help message\n");
            printf("  -modules, -m           List all available modules\n");
            printf("  -auth <type:options>   Set authentication module and options\n");
            printf("  -bg <type:options>     Set background module and options\n");
            printf("  -input <type:options>  Set input module and options\n\n");

            printf("CONFIGURATION FILE:\n");
            printf("  alock looks for configuration in these locations (in order):\n");
            printf("    1. $XDG_CONFIG_HOME/alock/config\n");
            printf("    2. $HOME/.alockrc\n\n");

            printf("  Configuration file format (INI-style):\n");
            printf("    [auth]\n");
            printf("    module = <module_name>\n");
            printf("    <option> = <value>\n\n");

            printf("    [background]\n");
            printf("    module = <module_name>\n");
            printf("    <option> = <value>\n\n");

            printf("    [input]\n");
            printf("    module = <module_name>\n");
            printf("    <option> = <value>\n\n");

            printf("AVAILABLE MODULES:\n\n");

            printf("  Authentication Modules:\n");
            printf("    pam               - Use PAM for authentication (recommended)\n");
            printf("    passwd            - Use /etc/passwd authentication\n");
            printf("    hash              - Use precalculated password hash\n");
            printf("    plaintext         - Plaintext password (TESTING ONLY - INSECURE!)\n");
            printf("                        Options: password=<password>\n\n");

            printf("  Background Modules:\n");
            printf("    blank             - Solid color background\n");
            printf("                        Options: color=<color_name>\n");
            printf("    image             - Display an image (requires Imlib2)\n");
            printf("                        Options:\n");
            printf("                          file=<path>           (required)\n");
            printf("                          scale=stretch|zoom|fill (default: stretch)\n");
            printf("                          center=true|false     (center on screen)\n");
            printf("                          tiled=true|false      (tile across screen)\n");
            printf("                          per_monitor=true      (render on each monitor independently)\n");
            printf("                          color=<color>         (background fill color)\n");
            printf("                          shade=<0-100>         (darken percentage)\n");
            printf("    shade             - Shade/blur the current screen (requires XRender)\n");
            printf("                        Options: shade=<0-100>, blur=<0-100>\n");
#if ENABLE_WEBKIT
            printf("    web               - Render HTML/CSS/JS animations (requires WebKitGTK)\n");
            printf("                        Options:\n");
            printf("                          url=<url>             (required) file:// or http(s)://\n");
            printf("                          bgcolor=<color>       (background color, default: black)\n");
#endif
            printf("    term              - Embed a terminal emulator (e.g. a ttyrec screensaver)\n");
            printf("                        Options:\n");
            printf("                          cmd=<command>         (program to run, default: top)\n");
            printf("                          cmdN=<command>        (per-monitor command; N = monitor index)\n");
            printf("                          term=<emul +flags %%w> (emulator template, default: xterm -into %%w ... -e)\n");
            printf("                          bgcolor=<color>       (background color, default: black)\n");
            printf("                          fill=0|1              (1: fill screen, 0: center, default: 0)\n");
            printf("                          respawn=0|1           (relaunch cmd on exit, default: 1)\n");
            printf("                        Best paired with -input dots (transparent overlay) or -input none\n");
            printf("    none              - No background change\n\n");

            printf("  Input Modules:\n");
            printf("    dots (default)    - Show password progress as dots\n");
            printf("                        Options:\n");
            printf("                          dot_placement   = center_left | center | center_right |\n");
            printf("                                           top_center | top_left | top_right\n");
            printf("                          dot_spacing     = <pixels> (default: 10)\n");
            printf("                          dot_radius      = <pixels> (default: 5)\n");
            printf("                          dot_color       = <color> (default: white)\n");
            printf("                          success_color   = <color> (default: green)\n");
            printf("                          error_color     = <color> (default: red)\n");
            printf("                          error_timeout   = <ms> (default: 500)\n");
            printf("                          success_timeout = <ms> (default: 300)\n");
            printf("    none              - No visual feedback\n\n");

            printf("EXAMPLES:\n\n");

            printf("  Basic usage with defaults (dots input, center-left, white/green/red):\n");
            printf("    %s\n\n", argv[0]);

            printf("  Lock with PAM authentication and black background:\n");
            printf("    %s -auth pam -bg blank:color=black\n\n", argv[0]);

            printf("  Lock with image background (command-line, old format still works):\n");
            printf("    %s -bg image:file=/path/to/image.png,center\n\n", argv[0]);

            printf("  Lock with image on each monitor independently (multi-monitor):\n");
            printf("    Config file format:\n");
            printf("      [background]\n");
            printf("      module = image\n");
            printf("      file = /path/to/image.png\n");
            printf("      scale = true\n");
            printf("      per_monitor = true\n\n");

            printf("  Lock with shaded/blurred screen:\n");
            printf("    %s -bg shade:shade=75,blur=50\n\n", argv[0]);

#if ENABLE_WEBKIT
            printf("  Lock with animated HTML/CSS/JS background:\n");
            printf("    %s -bg web:url=file:///path/to/animation.html\n\n", argv[0]);
#endif

            printf("  Lock with a ttyrec screensaver (embedded terminal):\n");
            printf("    %s -bg 'term:cmd=ttyplay /path/to/nethack.ttyrec'\n\n", argv[0]);

            printf("CONFIGURATION FILE EXAMPLE:\n\n");
            printf("  # ~/.alockrc - Complete example configuration\n");
            printf("  [auth]\n");
            printf("  module = pam\n\n");

            printf("  [background]\n");
            printf("  module = blank\n");
            printf("  color = black\n\n");

            printf("  [input]\n");
            printf("  module = dots\n");
            printf("  dot_placement = center_left\n");
            printf("  dot_spacing = 10\n");
            printf("  dot_radius = 5\n");
            printf("  dot_color = white\n");
            printf("  success_color = green\n");
            printf("  error_color = red\n");
            printf("  error_timeout = 500\n");
            printf("  success_timeout = 300\n\n");

            printf("FEATURES:\n");
            printf("  - Modular design with interchangeable components\n");
            printf("  - Password progress indicators (dots or frame)\n");
            printf("  - Visual feedback for success/failure (colored dots)\n");
            printf("  - Configurable timeouts and colors\n");
            printf("  - Caps lock indicator (frame module)\n");
            printf("  - Multi-monitor support (placement options)\n");
            printf("  - Configuration file support (INI format)\n");
            printf("  - Interruptible error animations\n");
            printf("  - No characters lost when typing during animations\n\n");

            printf("NOTES:\n");
            printf("  - Command-line options override configuration file settings\n");
            printf("  - The plaintext auth module is for testing only - never use in production!\n");
            printf("  - Dots placement 'center_left' is ideal for multi-monitor setups\n");
            printf("  - Error timeout can be cancelled by typing - no keypresses are lost\n\n");

            printf("SEE ALSO:\n");
            printf("  Use '%s -modules' to list all available modules on your system.\n\n", argv[0]);

            return EXIT_SUCCESS;

        case 'm': { /* list available modules */

            struct aModuleAuth **ia;
            struct aModuleBackground **ib;
            struct aModuleInput **ii;

            printf("authentication modules:\n");
            for (ia = alock_modules_auth; *ia; ++ia)
                printf("  %s\n", (*ia)->m.name);

            printf("background modules:\n");
            for (ib = alock_modules_background; *ib; ++ib)
                printf("  %s\n", (*ib)->m.name);

            printf("input modules:\n");
            for (ii = alock_modules_input; *ii; ++ii)
                printf("  %s\n", (*ii)->m.name);

            return EXIT_SUCCESS;
        }

        case 'a': /* authentication module */
            args_auth = optarg;
            modules.auth = find_module((void **)alock_modules_auth, args_auth);
            if (!modules.auth) {
                fprintf(stderr, "alock: authentication module `%s` not found\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'b': /* background module */
            args_background = optarg;
            modules.background = find_module((void **)alock_modules_background, args_background);
            if (!modules.background) {
                fprintf(stderr, "alock: background module `%s` not found\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'i': /* input module */
            args_input = optarg;
            modules.input = find_module((void **)alock_modules_input, args_input);
            if (!modules.input) {
                fprintf(stderr, "alock: input module `%s` not found\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        default:
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return EXIT_FAILURE;
        }

    /* required for correct input handling */
    setlocale(LC_ALL, "");

    if ((display = XOpenDisplay(NULL)) == NULL) {
        fprintf(stderr, "error: unable to connect to the X display\n");
        return EXIT_FAILURE;
    }

    /* make sure, that only one instance of alock is running */
    if (registerInstance(display)) {
        fprintf(stderr, "error: another instance seems to be running\n");
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    { /* try to initialize selected modules */

        int rv = 0;

        modules.auth->m.loadargs(args_auth);
        modules.background->m.loadargs(args_background);
        modules.input->m.loadargs(args_input);

        if (modules.auth->m.init(display)) {
            fprintf(stderr, "alock: failed init of [%s] with [%s]\n",
                    modules.auth->m.name, args_auth);
            rv |= 1;
        }

        if (modules.background->m.init(display)) {
            fprintf(stderr, "alock: failed init of [%s] with [%s]\n",
                    modules.background->m.name, args_background);
            rv |= 1;
        }
        if (modules.input->m.init(display)) {
            fprintf(stderr, "alock: failed init of [%s] with [%s]\n",
                    modules.input->m.name, args_input);
            rv |= 1;
        }

        if (rv) /* initialization failed */
            goto return_failure;

    }

    /* raise our background window and grab input, if this action has failed,
     * we are not able to lock the screen, then we're fucked... */
    if (lockDisplay(display, &modules))
        goto return_failure;

    debug("entering main event loop");
    eventLoop(display, &modules);

    retval = EXIT_SUCCESS;
    goto return_success;

return_failure:
    retval = EXIT_FAILURE;

return_success:

    modules.auth->m.free();
    modules.input->m.free();
    modules.background->m.free();

    unregisterInstance(display);
    XCloseDisplay(display);

    return retval;
}
