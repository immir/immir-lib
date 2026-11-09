/*
 * alock - bg_image.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 - 2016 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 * This background module provides:
 *  -bg image:file=<file>,color=<color>,shade=<int>,scale,center,tiled
 *
 * Used resources:
 *  ALock.Background.Image.Color
 *  ALock.Background.Image.Shade
 *  ALock.Background.Image.Option
 *
 */

#include "alock.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* stb_image implementation */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#if ENABLE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif


enum aImageOption {
    AIMAGE_OPTION_NONE = 0,
    AIMAGE_OPTION_SCALE, /* Stretch to fill */
    AIMAGE_OPTION_CENTER,
    AIMAGE_OPTION_TILED,
    AIMAGE_OPTION_ZOOM,  /* Scale with aspect, crop to fill */
    AIMAGE_OPTION_FILL,  /* Scale with aspect, fit without crop */
};

static struct moduleData {
    Display *display;
    Pixmap pixmap;
    Window window;
    char *colorname;
    char *filename;
    unsigned int shade;
    enum aImageOption option;
    int per_monitor;  /* Render image independently on each monitor */
} data = { 0 };


static void module_loadargs(const char *args) {

    if (!args || strstr(args, "image:") != args)
        return;

    char *arguments = strdup(&args[6]);
    char *arg;
    char *tmp;

    for (tmp = arguments; tmp; ) {
        arg = strsep(&tmp, ",");
        if (strstr(arg, "file=") == arg) {
            free(data.filename);
            data.filename = strdup(&arg[5]);
        }
        else if (strstr(arg, "color=") == arg) {
            free(data.colorname);
            data.colorname = strdup(&arg[6]);
        }
        else if (strstr(arg, "shade=") == arg) {
            data.shade = strtol(&arg[6], NULL, 0);
            if (data.shade > 100)
                fprintf(stderr, "[shade]: shade not in range [0, 100]\n");
        }
        /* Boolean options - support both "scale" and "scale=true/false" formats */
        else if (strcmp(arg, "scale") == 0 ||
                 strcmp(arg, "stretch") == 0 ||
                 (strstr(arg, "scale=") == arg && strcmp(&arg[6], "true") == 0) ||
                 (strstr(arg, "scale=") == arg && strcmp(&arg[6], "stretch") == 0)) {
            data.option = AIMAGE_OPTION_SCALE;
        }
        else if ((strstr(arg, "scale=") == arg && strcmp(&arg[6], "zoom") == 0)) {
            data.option = AIMAGE_OPTION_ZOOM;
        }
        else if ((strstr(arg, "scale=") == arg && strcmp(&arg[6], "fill") == 0)) {
            data.option = AIMAGE_OPTION_FILL;
        }
        else if (strcmp(arg, "center") == 0 ||
                 (strstr(arg, "center=") == arg && strcmp(&arg[7], "true") == 0)) {
            data.option = AIMAGE_OPTION_CENTER;
        }
        else if (strcmp(arg, "tiled") == 0 ||
                 (strstr(arg, "tiled=") == arg && strcmp(&arg[6], "true") == 0)) {
            data.option = AIMAGE_OPTION_TILED;
        }
        else if (strcmp(arg, "per_monitor") == 0 ||
                 (strstr(arg, "per_monitor=") == arg && strcmp(&arg[12], "true") == 0)) {
            data.per_monitor = 1;
        }
    }

    free(arguments);
}

/* Convert RGBA image data to XImage format */
static XImage *rgba_to_ximage(Display *dpy, Visual *vis, int depth,
                               unsigned char *rgba, int w, int h) {
    XImage *ximg = XCreateImage(dpy, vis, depth, ZPixmap, 0, NULL, w, h, 32, 0);
    if (!ximg)
        return NULL;

    ximg->data = malloc(ximg->bytes_per_line * h);
    if (!ximg->data) {
        XDestroyImage(ximg);
        return NULL;
    }

    /* Convert RGBA to native pixel format */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int src_idx = (y * w + x) * 4;
            unsigned char r = rgba[src_idx + 0];
            unsigned char g = rgba[src_idx + 1];
            unsigned char b = rgba[src_idx + 2];
            /* Note: alpha is ignored for display */

            unsigned long pixel;
            if (depth == 24 || depth == 32) {
                pixel = ((unsigned long)r << 16) | ((unsigned long)g << 8) | b;
            } else if (depth == 16) {
                pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                /* Fallback for other depths - use closest color */
                pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
            XPutPixel(ximg, x, y, pixel);
        }
    }

    return ximg;
}

/* Scale pixmap using Xrender */
static void scale_pixmap_xrender(Display *dpy, Visual *vis,
                                  Pixmap src, Pixmap dst,
                                  int src_w, int src_h,
                                  int dst_x, int dst_y,
                                  int dst_w, int dst_h) {
    XRenderPictFormat *format = XRenderFindVisualFormat(dpy, vis);
    if (!format)
        return;

    Picture src_pic = XRenderCreatePicture(dpy, src, format, 0, NULL);
    Picture dst_pic = XRenderCreatePicture(dpy, dst, format, 0, NULL);

    /* Set up scaling transform */
    XTransform transform = {{
        { XDoubleToFixed((double)src_w / dst_w), 0, 0 },
        { 0, XDoubleToFixed((double)src_h / dst_h), 0 },
        { 0, 0, XDoubleToFixed(1.0) }
    }};

    XRenderSetPictureTransform(dpy, src_pic, &transform);
    XRenderSetPictureFilter(dpy, src_pic, FilterBilinear, NULL, 0);

    XRenderComposite(dpy, PictOpSrc, src_pic, None, dst_pic,
                     0, 0, 0, 0, dst_x, dst_y, dst_w, dst_h);

    XRenderFreePicture(dpy, src_pic);
    XRenderFreePicture(dpy, dst_pic);
}

/* Render image at specified position/size onto the destination pixmap */
static void render_image_region(Display *dpy, Visual *vis, Colormap colormap,
                                 unsigned char *img_data, int w_img, int h_img,
                                 int x_offset, int y_offset,
                                 int w_screen, int h_screen,
                                 int depth, XColor *color) {
    int render_x = x_offset;
    int render_y = y_offset;
    int render_w, render_h;

    switch (data.option) {
        case AIMAGE_OPTION_CENTER:
            render_x = x_offset + (w_screen - w_img) / 2;
            render_y = y_offset + (h_screen - h_img) / 2;
            render_w = w_img;
            render_h = h_img;
            break;
        case AIMAGE_OPTION_TILED:
            {
                /* Create tile pixmap from image */
                Pixmap tile = XCreatePixmap(dpy, data.pixmap, w_img, h_img, depth);
                XImage *ximg = rgba_to_ximage(dpy, vis, depth, img_data, w_img, h_img);
                if (ximg) {
                    GC gc = XCreateGC(dpy, tile, 0, NULL);
                    XPutImage(dpy, tile, gc, ximg, 0, 0, 0, 0, w_img, h_img);
                    XFreeGC(dpy, gc);
                    XDestroyImage(ximg);

                    /* Tile the image */
                    GC tgc = XCreateGC(dpy, data.pixmap, 0, NULL);
                    XSetFillStyle(dpy, tgc, FillTiled);
                    XSetTile(dpy, tgc, tile);
                    XSetTSOrigin(dpy, tgc, x_offset, y_offset);
                    XFillRectangle(dpy, data.pixmap, tgc, x_offset, y_offset, w_screen, h_screen);
                    XFreeGC(dpy, tgc);
                }
                XFreePixmap(dpy, tile);
            }
            return;
        case AIMAGE_OPTION_ZOOM:
        case AIMAGE_OPTION_FILL:
            {
                double w_ratio = (double)w_screen / w_img;
                double h_ratio = (double)h_screen / h_img;
                double scale = (data.option == AIMAGE_OPTION_ZOOM) ? fmax(w_ratio, h_ratio) : fmin(w_ratio, h_ratio);
                render_w = (int)(w_img * scale);
                render_h = (int)(h_img * scale);
                render_x = x_offset + (w_screen - render_w) / 2;
                render_y = y_offset + (h_screen - render_h) / 2;
            }
            break;
        default: /* AIMAGE_OPTION_SCALE (stretch) */
            render_w = w_screen;
            render_h = h_screen;
            break;
    }

    /* Create source pixmap from image data */
    Pixmap src_pm = XCreatePixmap(dpy, data.pixmap, w_img, h_img, depth);
    XImage *ximg = rgba_to_ximage(dpy, vis, depth, img_data, w_img, h_img);
    if (!ximg) {
        XFreePixmap(dpy, src_pm);
        return;
    }

    GC gc = XCreateGC(dpy, src_pm, 0, NULL);
    XPutImage(dpy, src_pm, gc, ximg, 0, 0, 0, 0, w_img, h_img);
    XFreeGC(dpy, gc);
    XDestroyImage(ximg);

    /* If shading is needed, apply it to source first */
    if (data.shade) {
        Pixmap shaded_pm = XCreatePixmap(dpy, data.pixmap, w_img, h_img, depth);
        GC sgc = XCreateGC(dpy, shaded_pm, 0, NULL);
        XSetForeground(dpy, sgc, color->pixel);
        XFillRectangle(dpy, shaded_pm, sgc, 0, 0, w_img, h_img);
        XFreeGC(dpy, sgc);

        alock_shade_pixmap(dpy, vis, src_pm, shaded_pm, data.shade, 0, 0, 0, 0, w_img, h_img);
        XFreePixmap(dpy, src_pm);
        src_pm = shaded_pm;
    }

    /* Render (with optional scaling) to destination */
    if (render_w == w_img && render_h == h_img) {
        /* No scaling needed, direct copy */
        GC cgc = XCreateGC(dpy, data.pixmap, 0, NULL);
        XCopyArea(dpy, src_pm, data.pixmap, cgc, 0, 0, w_img, h_img, render_x, render_y);
        XFreeGC(dpy, cgc);
    } else {
        /* Scale using Xrender */
        scale_pixmap_xrender(dpy, vis, src_pm, data.pixmap,
                            w_img, h_img, render_x, render_y, render_w, render_h);
    }

    XFreePixmap(dpy, src_pm);
}

static int module_init(Display *dpy) {

    if (!data.filename) {
        fprintf(stderr, "[image]: file name not specified\n");
        return -1;
    }

    if (!alock_check_xrender(dpy)) {
        fprintf(stderr, "[image]: XRender extension required\n");
        return -1;
    }

    data.display = dpy;

    Screen *screen = DefaultScreenOfDisplay(dpy);
    Visual *vis = DefaultVisualOfScreen(screen);
    Colormap colormap = DefaultColormapOfScreen(screen);
    Window root = RootWindowOfScreen(screen);
    const int depth = DefaultDepthOfScreen(screen);
    const int rwidth = WidthOfScreen(screen);
    const int rheight = HeightOfScreen(screen);

    XColor color;
    alock_alloc_color(dpy, colormap, data.colorname, "black", &color);

    /* Load image using stb_image */
    int w_img, h_img, channels;
    unsigned char *img_data = stbi_load(data.filename, &w_img, &h_img, &channels, 4);
    if (!img_data) {
        fprintf(stderr, "[image]: unable to load image from file: %s\n", stbi_failure_reason());
        return -1;
    }

    /* Create destination pixmap */
    data.pixmap = XCreatePixmap(dpy, root, rwidth, rheight, depth);

    /* Fill background with color if centered/shaded */
    if (data.shade || data.option == AIMAGE_OPTION_CENTER) {
        GC gc = XCreateGC(dpy, data.pixmap, 0, NULL);
        XSetForeground(dpy, gc, color.pixel);
        XFillRectangle(dpy, data.pixmap, gc, 0, 0, rwidth, rheight);
        XFreeGC(dpy, gc);
    }

#if ENABLE_XRANDR
    /* Per-monitor rendering using RandR */
    if (data.per_monitor) {
        int num_monitors = 0;
        XRRScreenResources *screen_res = XRRGetScreenResources(dpy, root);
        if (screen_res) {
            for (int i = 0; i < screen_res->noutput; i++) {
                XRROutputInfo *output_info = XRRGetOutputInfo(dpy, screen_res, screen_res->outputs[i]);
                if (output_info->connection == RR_Connected && output_info->crtc) {
                    XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(dpy, screen_res, output_info->crtc);
                    if (crtc_info) {
                        render_image_region(dpy, vis, colormap, img_data, w_img, h_img,
                                           crtc_info->x, crtc_info->y,
                                           crtc_info->width, crtc_info->height,
                                           depth, &color);
                        num_monitors++;
                        XRRFreeCrtcInfo(crtc_info);
                    }
                }
                XRRFreeOutputInfo(output_info);
            }
            XRRFreeScreenResources(screen_res);
        }

        if (num_monitors == 0) { /* Fallback for no monitors found */
            render_image_region(dpy, vis, colormap, img_data, w_img, h_img,
                               0, 0, rwidth, rheight, depth, &color);
        }
    } else
#endif
    {
        /* Normal rendering - whole screen */
        render_image_region(dpy, vis, colormap, img_data, w_img, h_img,
                           0, 0, rwidth, rheight, depth, &color);
    }

    stbi_image_free(img_data);

    /* Create window with the background pixmap */
    XSetWindowAttributes xswa = {
        .override_redirect = True,
        .colormap = colormap,
        .background_pixmap = data.pixmap,
    };

    data.window = XCreateWindow(dpy, root,
            0, 0, rwidth, rheight, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWColormap | CWBackPixmap,
            &xswa);

    XMapWindow(dpy, data.window);

    return 0;
}

static void module_free() {

    if (data.window != None) {
        XDestroyWindow(data.display, data.window);
        data.window = None;
    }
    if (data.pixmap != None) {
        XFreePixmap(data.display, data.pixmap);
        data.pixmap = None;
    }

    free(data.colorname);
    data.colorname = NULL;
    free(data.filename);
    data.filename = NULL;

}

static Window module_getwindow(int screen) {
    (void)screen;
    return data.window;
}


struct aModuleBackground alock_bg_image = {
    { "image",
        module_loadargs,
        module_init,
        module_free,
    },
    module_getwindow,
};
