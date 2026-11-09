/*
 * alock - config.h
 * This file contains build-time configuration options.
 */

#ifndef ALOCK_CONFIG_H_
#define ALOCK_CONFIG_H_

#define VERSION "2.5.1"
#define PACKAGE "alock"

#define ENABLE_PAM 1
#define ENABLE_XRENDER 1
/* Xrandr support for multi-monitor - set to 0 to disable */
#ifndef ENABLE_XRANDR
#define ENABLE_XRANDR 1
#endif
/* WebKitGTK support - set to 1 to enable, requires webkit2gtk-4.0 */
#ifndef ENABLE_WEBKIT
#define ENABLE_WEBKIT 0
#endif

#define PAM_DEFAULT_SERVICE "system-auth"

#endif /* ALOCK_CONFIG_H_ */
