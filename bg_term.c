/*
 * alock - bg_term.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 - 2018 Arkadiusz Bokowy
 *               2026 Terminal/ttyrec screensaver background module
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 * This background module embeds a real terminal emulator (default xterm)
 * into the lock screen, running a configurable command -- e.g. a ttyrec
 * playback of NetHack as a "screensaver". The emulator is reparented into
 * the lock window via its embed flag (xterm `-into`, st `-w`, urxvt
 * `-embed`). Because alock grabs the keyboard/pointer on the root window,
 * the embedded terminal never receives input: typing the password still
 * unlocks, and the terminal cannot be used to escape the lock.
 *
 * On a multi-monitor (Xrandr) setup each connected output gets its own
 * terminal, and per-monitor commands can be given with indexed `cmdN=` keys
 * (N is the monitor index in Xrandr order); a bare `cmd=` is the fallback for
 * any monitor without its own command.
 *
 * This background module provides:
 *  -bg term:term=<emulator+flags with %w>,cmd=<command>,cmdN=<command>,bgcolor=<color>,fill=<0|1>,respawn=<0|1>
 *
 * Examples:
 *  -bg term                                          (placeholder: xterm running `top`)
 *  -bg 'term:cmd=ttyplay /path/game.ttyrec'          (same playback on every monitor)
 *  -bg 'term:cmd0=ttyplay a.ttyrec,cmd1=ttyplay b.ttyrec'  (different per monitor)
 *  -bg 'term:term=st -w %w -e,cmd=top'               (use st instead of xterm)
 *
 */

#include "alock.h"

#include <ctype.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#if ENABLE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif


/* Minimum interval between respawns and the number of consecutive too-fast
 * exits we tolerate before giving up on a monitor (guards against a fork-storm
 * when the configured command is broken or missing). */
#define TERM_RESPAWN_MIN_MS 2000UL
#define TERM_RESPAWN_MAX_FAILS 5

static const char *const TERM_DEFAULT_TEMPLATE =
    "xterm -into %w -fa Monospace -fs 18 -bg black -fg white"
    " -xrm *VT100.allowWindowOps:false -xrm *VT100.allowTitleOps:false -e";
static const char *const TERM_DEFAULT_CMD = "top";


/* One embedded terminal, bound to one monitor. */
struct termInstance {
    Window holder;          /* full-screen-of-monitor child of `window` */
    Window child;           /* embedded terminal's top window (None until known) */
    int x, y, w, h;         /* monitor geometry within the combined screen */
    const char *cmd;        /* command for this monitor (borrowed pointer) */

    pid_t child_pid;        /* -1 when no child running */
    unsigned long last_spawn;
    int fail_count;
    int gave_up;            /* stopped respawning after repeated fast exits */
    int respawn_pending;
    unsigned long respawn_at;
};


static struct moduleData {
    Display *display;
    Window window;          /* full-screen background window we own (holds dots) */
    int swidth, sheight;    /* combined screen dimensions */

    /* Configuration */
    char *term;             /* emulator + embed flags, contains `%w` */
    char *cmd;              /* fallback command (bare `cmd=`) */
    char **cmds;            /* indexed per-monitor commands (cmd0..) */
    int cmds_n;             /* allocated length of cmds */
    char *bgcolor;          /* window background color name */
    int fill;               /* 1: resize terminal to fill its monitor; 0: center */
    int respawn;            /* 1: relaunch a command when it exits */

    /* One terminal per monitor */
    struct termInstance *inst;
    int inst_n;
} data = {
    .window = None,
    .fill = 0,
    .respawn = 1,
};


/* Split `s` on whitespace into a NULL-terminated array of strdup'd tokens. */
static char **tokenize(const char *s) {

    int cap = 8, n = 0;
    char **v = malloc(cap * sizeof(*v));
    const char *p = s ? s : "";

    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;

        const char *start = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;

        if (n + 2 > cap) {
            cap *= 2;
            v = realloc(v, cap * sizeof(*v));
        }
        v[n++] = strndup(start, (size_t)(p - start));
    }

    v[n] = NULL;
    return v;
}

static void free_tokens(char **v) {
    for (char **p = v; *p; p++)
        free(*p);
    free(v);
}

/* Return a copy of `tok` with every "%w" replaced by the decimal window id. */
static char *subst_window(const char *tok, Window win) {

    if (!strstr(tok, "%w"))
        return strdup(tok);

    char winbuf[32];
    snprintf(winbuf, sizeof(winbuf), "%lu", (unsigned long)win);
    const size_t wlen = strlen(winbuf);

    size_t hits = 0;
    for (const char *p = tok; (p = strstr(p, "%w")); p += 2)
        hits++;

    char *out = malloc(strlen(tok) + hits * (wlen - 2) + 1);
    char *o = out;
    const char *p = tok;
    const char *q;
    while ((q = strstr(p, "%w"))) {
        memcpy(o, p, (size_t)(q - p));
        o += q - p;
        memcpy(o, winbuf, wlen);
        o += wlen;
        p = q + 2;
    }
    strcpy(o, p);
    return out;
}

/* Build the child argv: emulator template (with %w substituted to embed into
 * `embed_into`) followed by `cmd`. Returns a NULL-terminated, owned argv. */
static char **build_argv(Window embed_into, const char *cmd) {

    char **term_tok = tokenize(data.term ? data.term : TERM_DEFAULT_TEMPLATE);
    char **cmd_tok = tokenize(cmd);

    int n = 0;
    for (char **p = term_tok; *p; p++) n++;
    for (char **p = cmd_tok; *p; p++) n++;

    char **argv = malloc((n + 1) * sizeof(*argv));
    int k = 0;
    for (char **p = term_tok; *p; p++)
        argv[k++] = subst_window(*p, embed_into);
    for (char **p = cmd_tok; *p; p++)
        argv[k++] = strdup(*p);
    argv[k] = NULL;

    free_tokens(term_tok);
    free_tokens(cmd_tok);
    return argv;
}


/* Fork and exec the terminal emulator for one monitor, embedded into its
 * holder window. */
static void spawn_instance(struct termInstance *t) {

    char **argv = build_argv(t->holder, t->cmd);

    t->last_spawn = alock_mtime();
    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "[term]: fork failed\n");
        fflush(stderr);
        free_tokens(argv);
        t->child_pid = -1;
        return;
    }

    if (pid == 0) {
        /* Child: drop inherited fds (X socket, PAM state, ...), restore a
         * clean signal environment, then exec. No Xlib calls here -- the
         * forked child shares Xlib's locks and must not touch them. */
        for (int fd = 3; fd < 256; fd++)
            close(fd);

        signal(SIGCHLD, SIG_DFL);
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, NULL);

        execvp(argv[0], argv);
        _exit(127);  /* exec failed: no stdio flush, no atexit */
    }

    t->child_pid = pid;
    t->child = None;
    free_tokens(argv);
}


static struct termInstance *find_by_holder(Window w) {
    for (int i = 0; i < data.inst_n; i++)
        if (data.inst[i].holder == w)
            return &data.inst[i];
    return NULL;
}

static struct termInstance *find_by_child(Window w) {
    for (int i = 0; i < data.inst_n; i++)
        if (data.inst[i].child == w)
            return &data.inst[i];
    return NULL;
}


/* Position the terminal within its monitor: fill it or center it. */
static void place_child(struct termInstance *t, int cw, int ch) {
    if (data.fill) {
        XMoveResizeWindow(data.display, t->child, 0, 0, t->w, t->h);
    } else {
        int x = (t->w - cw) / 2;
        int y = (t->h - ch) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        XMoveWindow(data.display, t->child, x, y);
    }
}


/* Drain holder structure-notify events and (re)position each terminal. */
static void process_geometry_events(void) {

    XEvent ev;
    while (XCheckMaskEvent(data.display, SubstructureNotifyMask, &ev)) {

        if (ev.type == ReparentNotify) {
            struct termInstance *t = find_by_holder(ev.xreparent.parent);
            if (!t)
                continue;
            t->child = ev.xreparent.window;
            if (data.fill) {
                place_child(t, 0, 0);
            } else {
                Window root_ret;
                int rx, ry;
                unsigned int cw, ch, bw, depth;
                if (XGetGeometry(data.display, t->child, &root_ret,
                            &rx, &ry, &cw, &ch, &bw, &depth))
                    place_child(t, (int)cw, (int)ch);
            }
        }
        else if (ev.type == ConfigureNotify && !data.fill) {
            struct termInstance *t = find_by_child(ev.xconfigure.window);
            if (!t)
                continue;
            int x = (t->w - ev.xconfigure.width) / 2;
            int y = (t->h - ev.xconfigure.height) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (ev.xconfigure.x != x || ev.xconfigure.y != y)
                place_child(t, ev.xconfigure.width, ev.xconfigure.height);
        }
    }
}


/* Reap exited children and schedule rate-limited respawns, per monitor. */
static void process_child_lifecycle(void) {

    const unsigned long now = alock_mtime();

    for (int i = 0; i < data.inst_n; i++) {
        struct termInstance *t = &data.inst[i];

        if (t->child_pid > 0) {
            int status;
            if (waitpid(t->child_pid, &status, WNOHANG) == t->child_pid) {
                t->child_pid = -1;
                t->child = None;

                if (!data.respawn || t->gave_up)
                    continue;

                if (now - t->last_spawn < TERM_RESPAWN_MIN_MS)
                    t->fail_count++;
                else
                    t->fail_count = 0;

                if (t->fail_count >= TERM_RESPAWN_MAX_FAILS) {
                    fprintf(stderr, "[term]: monitor %d command exits too fast; "
                            "giving up respawn\n", i);
                    fflush(stderr);
                    t->gave_up = 1;
                    continue;
                }

                t->respawn_pending = 1;
                t->respawn_at = (t->fail_count > 0) ? now + TERM_RESPAWN_MIN_MS : now;
            }
        }

        if (t->respawn_pending && now >= t->respawn_at) {
            t->respawn_pending = 0;
            spawn_instance(t);
        }
    }
}


/* Public idle hook, called from the main event loop. */
void alock_bg_term_process_events(void) {
    if (data.window == None)
        return;
    process_geometry_events();
    process_child_lifecycle();
}


/* Store an indexed per-monitor command: cmds[index] = strdup(value). */
static void set_indexed_cmd(int index, const char *value) {
    if (index < 0)
        return;
    if (index >= data.cmds_n) {
        int n = index + 1;
        data.cmds = realloc(data.cmds, n * sizeof(*data.cmds));
        for (int i = data.cmds_n; i < n; i++)
            data.cmds[i] = NULL;
        data.cmds_n = n;
    }
    free(data.cmds[index]);
    data.cmds[index] = strdup(value);
}


static void module_loadargs(const char *args) {

    if (!args || strstr(args, "term:") != args)
        return;

    char *arguments = strdup(&args[5]);
    char *arg;
    char *tmp;

    for (tmp = arguments; tmp; ) {
        arg = strsep(&tmp, ",");
        /* indexed per-monitor command: cmd<N>=... */
        if (strncmp(arg, "cmd", 3) == 0 && isdigit((unsigned char)arg[3])) {
            char *endp;
            long idx = strtol(&arg[3], &endp, 10);
            if (*endp == '=')
                set_indexed_cmd((int)idx, endp + 1);
        }
        else if (strstr(arg, "cmd=") == arg) {
            free(data.cmd);
            data.cmd = strdup(&arg[4]);
        }
        else if (strstr(arg, "term=") == arg) {
            free(data.term);
            data.term = strdup(&arg[5]);
        }
        else if (strstr(arg, "bgcolor=") == arg) {
            free(data.bgcolor);
            data.bgcolor = strdup(&arg[8]);
        }
        else if (strstr(arg, "fill=") == arg) {
            data.fill = strtol(&arg[5], NULL, 0) != 0;
        }
        else if (strstr(arg, "respawn=") == arg) {
            data.respawn = strtol(&arg[8], NULL, 0) != 0;
        }
    }

    free(arguments);
}


/* Collect monitor geometries into a malloc'd array; returns the count and
 * sets *out. Falls back to one full-screen monitor when Xrandr is disabled,
 * unavailable, or reports nothing. Mirrored outputs sharing a CRTC are
 * collapsed to a single entry. */
static int enumerate_monitors(Display *dpy, struct termInstance **out) {

    struct termInstance *mon = NULL;
    int n = 0;

#if ENABLE_XRANDR
    Window root = RootWindow(dpy, DefaultScreen(dpy));
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
    if (res) {
        RRCrtc *seen = malloc(res->ncrtc * sizeof(*seen));
        int seen_n = 0;
        for (int i = 0; i < res->noutput; i++) {
            XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
            if (oi && oi->connection == RR_Connected && oi->crtc) {
                int dup = 0;
                for (int j = 0; j < seen_n; j++)
                    if (seen[j] == oi->crtc) { dup = 1; break; }
                if (!dup) {
                    XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
                    if (ci) {
                        if (ci->width > 0 && ci->height > 0) {
                            mon = realloc(mon, (n + 1) * sizeof(*mon));
                            mon[n] = (struct termInstance){0};
                            mon[n].x = ci->x;
                            mon[n].y = ci->y;
                            mon[n].w = ci->width;
                            mon[n].h = ci->height;
                            n++;
                            seen[seen_n++] = oi->crtc;
                        }
                        XRRFreeCrtcInfo(ci);
                    }
                }
            }
            if (oi)
                XRRFreeOutputInfo(oi);
        }
        free(seen);
        XRRFreeScreenResources(res);
    }
#endif

    if (n == 0) {
        mon = realloc(mon, sizeof(*mon));
        mon[0] = (struct termInstance){0};
        mon[0].x = 0;
        mon[0].y = 0;
        mon[0].w = data.swidth;
        mon[0].h = data.sheight;
        n = 1;
    }

    *out = mon;
    return n;
}


static int module_init(Display *dpy) {

    Screen *screen = DefaultScreenOfDisplay(dpy);
    Colormap colormap = DefaultColormapOfScreen(screen);
    XColor color;

    data.display = dpy;
    data.swidth = WidthOfScreen(screen);
    data.sheight = HeightOfScreen(screen);

    alock_alloc_color(dpy, colormap, data.bgcolor, "black", &color);

    /* The background window holds the input (dots) overlay, which alock
     * reparents into it. */
    XSetWindowAttributes xswa;
    xswa.override_redirect = True;
    xswa.colormap = colormap;
    xswa.background_pixel = color.pixel;

    data.window = XCreateWindow(dpy, RootWindowOfScreen(screen),
            0, 0, data.swidth, data.sheight, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWColormap | CWBackPixel,
            &xswa);

    /* One terminal per monitor. Each embeds into a dedicated holder child so
     * its reparent/configure events are isolated from the dots window that
     * alock reparents into `window`. */
    struct termInstance *mons = NULL;
    data.inst_n = enumerate_monitors(dpy, &mons);
    data.inst = mons;

    XSetWindowAttributes hswa;
    hswa.background_pixel = color.pixel;
    hswa.event_mask = SubstructureNotifyMask;

    for (int i = 0; i < data.inst_n; i++) {
        struct termInstance *t = &data.inst[i];
        t->child = None;
        t->child_pid = -1;

        if (i < data.cmds_n && data.cmds[i])
            t->cmd = data.cmds[i];
        else if (data.cmd)
            t->cmd = data.cmd;
        else
            t->cmd = TERM_DEFAULT_CMD;

        t->holder = XCreateWindow(dpy, data.window,
                t->x, t->y, t->w, t->h, 0,
                CopyFromParent, InputOutput, CopyFromParent,
                CWBackPixel | CWEventMask,
                &hswa);
        XMapWindow(dpy, t->holder);

        /* A spawn failure must not prevent locking: the screen still locks
         * with a blank (bgcolor) background on that monitor. */
        spawn_instance(t);
    }

    return 0;
}


static void module_free() {

    /* Kill all children before destroying windows so terminals do not briefly
     * reparent back to root and flash during teardown. */
    for (int i = 0; i < data.inst_n; i++)
        if (data.inst[i].child_pid > 0)
            kill(data.inst[i].child_pid, SIGTERM);

    const unsigned long deadline = alock_mtime() + 300;
    for (;;) {
        int alive = 0;
        for (int i = 0; i < data.inst_n; i++) {
            struct termInstance *t = &data.inst[i];
            if (t->child_pid > 0) {
                if (waitpid(t->child_pid, NULL, WNOHANG) == t->child_pid)
                    t->child_pid = -1;
                else
                    alive = 1;
            }
        }
        if (!alive || alock_mtime() >= deadline)
            break;
        usleep(10000);
    }
    for (int i = 0; i < data.inst_n; i++) {
        struct termInstance *t = &data.inst[i];
        if (t->child_pid > 0) {
            kill(t->child_pid, SIGKILL);
            waitpid(t->child_pid, NULL, 0);
            t->child_pid = -1;
        }
    }

    if (data.window != None) {
        /* destroys the holders and embedded terminals as descendants */
        XDestroyWindow(data.display, data.window);
        data.window = None;
    }

    free(data.inst);
    data.inst = NULL;
    data.inst_n = 0;

    for (int i = 0; i < data.cmds_n; i++)
        free(data.cmds[i]);
    free(data.cmds);
    data.cmds = NULL;
    data.cmds_n = 0;

    free(data.term);
    data.term = NULL;
    free(data.cmd);
    data.cmd = NULL;
    free(data.bgcolor);
    data.bgcolor = NULL;
}


static Window module_getwindow(int screen) {
    (void)screen;
    return data.window;
}


struct aModuleBackground alock_bg_term = {
    { "term",
        module_loadargs,
        module_init,
        module_free,
    },
    module_getwindow,
};
