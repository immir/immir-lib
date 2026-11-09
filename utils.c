/*
 * alock - utils.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 - 2018 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "alock.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xutil.h>
#if ENABLE_XRENDER
# include <X11/extensions/Xrender.h>
#endif

/* Append "key=value," to a fixed-size args buffer. Returns 0 on success, or -1
 * (leaving the buffer unchanged) if the entry does not fit -- with a flushed
 * warning naming the offending option, so silent truncation can't happen. */
static int append_config_arg(char *args, size_t size, const char *section,
                             const char *key, const char *value) {

    char entry[1024];
    int n = snprintf(entry, sizeof(entry), "%s=%s,", key, value);
    assert(n >= 0);

    const size_t used = strlen(args);
    assert(used < size);

    if ((size_t)n >= sizeof(entry) || used + (size_t)n >= size) {
        fprintf(stderr, "alock: config option [%s] %s too long; ignored "
                "(max %zu bytes per section)\n", section, key, size - 1);
        fflush(stderr);
        return -1;
    }

    memcpy(args + used, entry, (size_t)n + 1);
    return 0;
}


/* Get system time-stamp in milliseconds without discontinuities. */
unsigned long alock_mtime() {
    struct timespec t;
#ifdef CLOCK_BOOTTIME
    clock_gettime(CLOCK_BOOTTIME, &t);
#else
#warning Suspend-aware monotonic clock not available!
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Allocate colormap entry by the given color name. When the color_name
 * parameter is NULL, then fallback value is used right away. */
int alock_alloc_color(Display *display,
        Colormap colormap,
        const char *color_name,
        const char *fallback_name,
        XColor *result) {

    if (!display || !colormap || !fallback_name || !result)
        return 0;

    XColor tmp;

    if (!color_name || XAllocNamedColor(display, colormap, color_name, &tmp, result) == 0)
        if (XAllocNamedColor(display, colormap, fallback_name, &tmp, result) == 0)
            return 0;
    return 1;
}

/* Check if the X server supports RENDER extension. */
int alock_check_xrender(Display *display) {
#if ENABLE_XRENDER
    static int checked = 0;
    static int available = 0;

    if (checked)
        return available;

    int tmp;
    checked = 1;

    if ((available = XRenderQueryExtension(display, &tmp, &tmp)) == False)
        fprintf(stderr, "alock: missing X Render Extension support\n");

    return available;
#else
    (void)display;
    return 0;
#endif /* ENABLE_XRENDER */
}

/* Shade given source pixmap by the amount specified by the shade parameter,
 * which should be in range [0, 100]. */
int alock_shade_pixmap(Display *display,
        Visual *visual,
        const Pixmap src_pm,
        Pixmap dst_pm,
        unsigned char shade,
        int src_x, int src_y,
        int dst_x, int dst_y,
        unsigned int width,
        unsigned int height) {
#if ENABLE_XRENDER
    Picture alpha_pic = None;
    XRenderPictFormat *format;

    {
        XRenderPictFormat alpha_format;
        alpha_format.type = PictTypeDirect;
        alpha_format.depth = 8;
        alpha_format.direct.alpha = 0;
        alpha_format.direct.alphaMask = 0xff;

        format = XRenderFindFormat(display,
              PictFormatType | PictFormatDepth | PictFormatAlpha | PictFormatAlphaMask,
              &alpha_format, 0);
    }

    if (!format) {
        fprintf(stderr, "alock: couldn't find valid format for alpha\n");
        XFreePixmap(display, dst_pm);
        XFreePixmap(display, src_pm);
        return 0;
    }

    { /* fill the alpha-picture */
        Pixmap pm;
        XRenderColor color;
        XRenderPictureAttributes pa;

        if (shade > 100)
            shade = 100;

        pa.repeat = True;
        color.alpha = 0xffff * (100 - shade) / 100;

        pm = XCreatePixmap(display, src_pm, 1, 1, 8);
        alpha_pic = XRenderCreatePicture(display, pm, format, CPRepeat, &pa);
        XRenderFillRectangle(display, PictOpSrc, alpha_pic, &color, 0, 0, 1, 1);

        XFreePixmap(display, pm);
    }

    { /* blend all together */
        Picture src_pic;
        Picture dst_pic;

        format = XRenderFindVisualFormat(display, visual);
        src_pic = XRenderCreatePicture(display, src_pm, format, 0, 0);
        dst_pic = XRenderCreatePicture(display, dst_pm, format, 0, 0);

        XRenderComposite(display, PictOpOver,
                         src_pic, alpha_pic, dst_pic,
                         src_x, src_y, 0, 0, dst_x, dst_y, width, height);
        XRenderFreePicture(display, src_pic);
        XRenderFreePicture(display, dst_pic);
    }

    XRenderFreePicture(display, alpha_pic);
    return 1;
#else
    (void)display;
    (void)visual;
    return 0;
#endif /* ENABLE_XRENDER */
}


/* Convert given color image to the grayscale intensity one. Note, that this
 * function performs in-place conversion. */
int alock_grayscale_image(XImage *image,
        int x, int y,
        unsigned int width,
        unsigned int height) {

    union {
        struct __attribute__ ((packed)) {
            uint16_t red   : 5;
            uint16_t green : 6;
            uint16_t blue  : 5;
        } v16;
        struct __attribute__ ((packed)) {
            uint8_t red;
            uint8_t green;
            uint8_t blue;
        } v24;
        unsigned long value;
    } pixel;

    int depth = image->depth;
    int _x, _y;

    if (depth != 16 && depth != 24) {
        fprintf(stderr, "alock: screen depth %d is not supported\n", depth);
        return 0;
    }

    /* NOTE: Color conversion is based on the colorimetric strategy. The
     *       principle is, that the luminance of the grayscale image should
     *       match the luminance of the original color image. */

    for (_x = x; _x < (signed)width; _x++)
        for (_y = y; _y < (signed)height; _y++) {
            pixel.value = XGetPixel(image, _x, _y);

            if (depth == 24)
                pixel.v24.red = pixel.v24.green = pixel.v24.blue =
                        0.2126 * pixel.v24.red +
                        0.7152 * pixel.v24.green +
                        0.0722 * pixel.v24.blue;
            else {
                pixel.v16.green =
                        0.2126 * (pixel.v16.red << 1) +
                        0.7152 * pixel.v16.green +
                        0.0722 * (pixel.v16.blue << 1);
                pixel.v16.red = pixel.v16.blue = pixel.v16.green >> 1;
            }

            XPutPixel(image, _x, _y, pixel.value);
        }

    return 1;
}

/* Dummy function for module interface. */
void module_dummy_loadargs(const char *args) {
    (void)args;
    debug("dummy loadargs: %s", args);
}

int module_dummy_init(Display *display) {
    (void)display;
    debug("dummy init");
    return 0;
}

void module_dummy_free(void) {
}

void module_dummy_setcapslock(Bool active) {
    (void)active;
}

/* Get configuration file path. */
char *alock_get_config_path(void) {
    static char path[512];

    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config) {
        snprintf(path, sizeof(path), "%s/alock/config", xdg_config);
        if (access(path, R_OK) == 0)
            return path;
    }

    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.alockrc", home);
        if (access(path, R_OK) == 0)
            return path;
    }

    return NULL;
}

/* Load configuration from INI file. */
int alock_load_config(const char *config_path, struct aConfig *config) {
    FILE *fp = fopen(config_path, "r");
    if (!fp)
        return -1;

    char line[1024];
    char section[64] = "";

    memset(config, 0, sizeof(struct aConfig));

    while (fgets(line, sizeof(line), fp)) {
        /* Detect (and skip) a line too long for the buffer rather than parsing
         * a silently truncated fragment. */
        if (!strchr(line, '\n') && !feof(fp)) {
            fprintf(stderr, "alock: config line exceeds %zu bytes; ignored\n",
                    sizeof(line) - 1);
            fflush(stderr);
            int c;
            while ((c = fgetc(fp)) != '\n' && c != EOF)
                ;
            continue;
        }

        /* Trim whitespace */
        char *start = line;
        while (isspace(*start)) start++;

        /* Skip comments and empty lines */
        if (*start == '#' || *start == '\0' || *start == '\n')
            continue;

        /* Remove trailing newline */
        char *end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';

        /* Parse section header [section] */
        if (*start == '[' && *end == ']') {
            *end = '\0';
            strncpy(section, start + 1, sizeof(section) - 1);
            continue;
        }

        /* Parse key = value */
        char *eq = strchr(start, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = start;
        char *value = eq + 1;

        /* Trim key and value */
        while (isspace(*value)) value++;
        char *key_end = eq - 1;
        while (key_end > key && isspace(*key_end)) *key_end-- = '\0';

        /* Store configuration based on section */
        if (strcmp(section, "auth") == 0) {
            if (strcmp(key, "module") == 0)
                strncpy(config->auth_module, value, sizeof(config->auth_module) - 1);
            else
                append_config_arg(config->auth_args, sizeof(config->auth_args),
                                  section, key, value);
        }
        else if (strcmp(section, "background") == 0) {
            if (strcmp(key, "module") == 0)
                strncpy(config->bg_module, value, sizeof(config->bg_module) - 1);
            else
                append_config_arg(config->bg_args, sizeof(config->bg_args),
                                  section, key, value);
        }
        else if (strcmp(section, "input") == 0) {
            if (strcmp(key, "module") == 0)
                strncpy(config->input_module, value, sizeof(config->input_module) - 1);
            else
                append_config_arg(config->input_args, sizeof(config->input_args),
                                  section, key, value);
        }
    }

    /* Remove trailing commas from args */
    if (strlen(config->auth_args) > 0)
        config->auth_args[strlen(config->auth_args) - 1] = '\0';
    if (strlen(config->bg_args) > 0)
        config->bg_args[strlen(config->bg_args) - 1] = '\0';
    if (strlen(config->input_args) > 0)
        config->input_args[strlen(config->input_args) - 1] = '\0';

    fclose(fp);
    return 0;
}
