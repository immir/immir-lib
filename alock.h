/*
 * alock - alock.h
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 - 2018 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#ifndef ALOCK_ALOCK_H_
#define ALOCK_ALOCK_H_

#include "config.h"

#include <stdio.h>
#include <X11/Xlib.h>


#if DEBUG
# define debug(M, ARGS ...) fprintf(stderr, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ## ARGS)
#else
# define debug(M, ARGS ...) do {} while (0)
#endif


/* possible states of the input module */
enum aInputState {
    AINPUT_STATE_NONE,
    AINPUT_STATE_INIT,
    AINPUT_STATE_CHECK,
    AINPUT_STATE_VALID,
    AINPUT_STATE_ERROR,
};


/* module base interface */
struct aModule {
    const char *name;
    void (*loadargs)(const char *args);
    int (*init)(Display *display);
    void (*free)();
};

struct aModuleAuth {
    struct aModule m;
    int (*authenticate)(const char *pass);
};

struct aModuleBackground {
    struct aModule m;
    Window (*getwindow)(int screen);
};

struct aModuleInput {
    struct aModule m;
    Window (*getwindow)(int screen);
    KeySym (*keypress)(KeySym key);
    void (*setstate)(enum aInputState state);
    void (*setprogress)(unsigned int length, unsigned int position);
    void (*setcapslock)(Bool active);
};


/* module container structure */
struct aModules {
    struct aModuleAuth *auth;
    struct aModuleBackground *background;
    struct aModuleInput *input;
};


/* authentication modules */
extern struct aModuleAuth alock_auth_plaintext;
#if ENABLE_PAM
extern struct aModuleAuth alock_auth_pam;
#endif

/* background modules */
extern struct aModuleBackground alock_bg_none;
extern struct aModuleBackground alock_bg_blank;
extern struct aModuleBackground alock_bg_image;
#if ENABLE_XRENDER
extern struct aModuleBackground alock_bg_shade;
#endif
#if ENABLE_WEBKIT
extern struct aModuleBackground alock_bg_web;
void alock_bg_web_process_events(void);
#endif
extern struct aModuleBackground alock_bg_term;
void alock_bg_term_process_events(void);

/* input modules */
extern struct aModuleInput alock_input_none;
extern struct aModuleInput alock_input_dots;

/* input_dots timeout accessors */
unsigned long alock_input_dots_get_error_timeout(void);
unsigned long alock_input_dots_get_success_timeout(void);


/* dummy functions for module interface */
void module_dummy_loadargs(const char *args);
int module_dummy_init(Display *display);
void module_dummy_free(void);
void module_dummy_setcapslock(Bool active);


/* helper functions defined in utils.c */
unsigned long alock_mtime(void);
int alock_alloc_color(Display *display,
        Colormap colormap,
        const char *color_name,
        const char *fallback_name,
        XColor *result);
int alock_check_xrender(Display *display);
int alock_shade_pixmap(Display *display,
        Visual *visual,
        const Pixmap src_pm,
        Pixmap dst_pm,
        unsigned char shade,
        int src_x, int src_y,
        int dst_x, int dst_y,
        unsigned int width,
        unsigned int height);
int alock_grayscale_image(XImage *image,
        int x, int y,
        unsigned int width,
        unsigned int height);

/* Configuration file support */
struct aConfig {
    char auth_module[128];
    char auth_args[1024];
    char bg_module[128];
    char bg_args[1024];
    char input_module[128];
    char input_args[1024];
};

int alock_load_config(const char *config_path, struct aConfig *config);
char *alock_get_config_path(void);

#endif /* ALOCK_ALOCK_H_ */
