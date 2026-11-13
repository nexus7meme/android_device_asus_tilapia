/* SPDX-License-Identifier: Apache-2.0 */
/* Correct the Tegra driver's relative EGL library paths for the modern linker.
 * Based on the compatibility approach documented by Dmitry Grinberg:
 * https://dmitry.gr/?r=06.+Thoughts&proj=04.+Android+M+on+Grouper
 */
#include <limits.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdio.h>
#include <string.h>

extern int NvOsLibraryLoad(const char *name, void **handle);

int GrouperLibLoad_(const char *name, void **handle)
{
    char absolute[PATH_MAX];
    int length;

    const char *resolved = name;
    int result;

    if (name != NULL && strncmp(name, "egl/", 4) == 0) {
        /* NvOsLibraryLoad prefixes /vendor/lib and appends .so unless
         * the name already contains .so. Supply a complete absolute name. */
        length = snprintf(absolute, sizeof(absolute), "/vendor/lib/%s%s", name,
                          strstr(name, ".so") ? "" : ".so");
        if (length >= 0 && (size_t)length < sizeof(absolute))
            resolved = absolute;
    }
    result = NvOsLibraryLoad(resolved, handle);
    if (result != 0) {
        const char *error = dlerror();
        __android_log_print(ANDROID_LOG_ERROR, "TegraLoader", "%s: NvError %d (%s)",
                            resolved ? resolved : "(null)", result,
                            error ? error : "no linker error");
    }
    return result;
}
