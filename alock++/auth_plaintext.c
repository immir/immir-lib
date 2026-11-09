/*
 * alock - auth_plaintext.c
 * Copyright (c) 2025
 *
 * This file is a part of an alock.
 *
 * This project is licensed under the terms of the MIT license.
 *
 * WARNING: This authentication module stores passwords in plaintext.
 * It is intended for testing purposes only and should NEVER be used
 * in production or security-sensitive environments.
 *
 * This authentication module provides:
 *  -auth plaintext:password=<password>
 *
 */

#include "alock.h"

#include <stdlib.h>
#include <string.h>


static struct moduleData {
    char *password;
} data = { NULL };


static void module_loadargs(const char *args) {
    if (!args || strstr(args, "plaintext:") != args)
        return;

    char *arguments = strdup(&args[10]);
    char *arg;
    char *tmp;

    for (tmp = arguments; tmp; ) {
        arg = strsep(&tmp, ",");
        if (strstr(arg, "password=") == arg) {
            free(data.password);
            data.password = strdup(&arg[9]);
        }
    }

    free(arguments);
}


static void module_free(void) {
    free(data.password);
    data.password = NULL;
}


static int module_authenticate(const char *pass) {
    /* If no password configured, always fail */
    if (!data.password)
        return -1;

    /* Compare passwords */
    if (strcmp(pass, data.password) == 0)
        return 0;  /* Success */

    return -1;  /* Failed */
}


struct aModuleAuth alock_auth_plaintext = {
    { "plaintext",
        module_loadargs,
        module_dummy_init,
        module_free,
    },
    module_authenticate,
};
