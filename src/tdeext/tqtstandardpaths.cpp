#include "tqtstandardpaths.h"

#include <ntqapplication.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static inline int tqt_is_sep(const char c) { return (c == '/') ? 1 : 0; }

static int tqt_mkpath(const TQString& path) {
    if (path.isEmpty()) return -1;

    const TQCString p = path.utf8();
    const char* s = p.data();
    if (!s || !*s) return -1;

    size_t n = strlen(s);
    char* buf = (char*)malloc(n + 1);
    if (!buf) return -1;

    memcpy(buf, s, n + 1);

    for (size_t i = 1; i < n; ++i) {
        if (!tqt_is_sep(buf[i])) continue;
        buf[i] = 0;
        if (buf[0]) {
            if (mkdir(buf, 0700) != 0) {
                if (errno != EEXIST) {
                    buf[i] = '/';
                    free(buf);
                    return -1;
                }
            }
        }
        buf[i] = '/';
    }

    if (mkdir(buf, 0700) != 0) {
        if (errno != EEXIST) {
            free(buf);
            return -1;
        }
    }

    free(buf);
    return 0;
}

static TQString tqt_getenv(const char* name) {
    const char* v = getenv(name);
    if (!v || !*v) return TQString();
    return TQString::fromLocal8Bit(v);
}

static TQString tqt_home_dir() {
    TQString h = tqt_getenv("HOME");
    if (h.isEmpty()) h = TQString("/tmp");
    return h;
}

TQString TQtStandardPaths::appSubdir(const TQString& organization, const TQString& application) {
    TQString org = organization;
    TQString app = application;

    if (app.isEmpty()) {
        if (tqApp) {
            app = tqApp->name();
        }
    }

    if (app.isEmpty()) app = TQString("app");

    if (org.isEmpty()) return app;
    return org + "/" + app;
}

TQStringList TQtStandardPaths::standardLocations(StandardLocation type,
                                                const TQString& organization,
                                                const TQString& application) {
    TQStringList out;

    const TQString sub = appSubdir(organization, application);

    if (type == AppConfigLocation) {
        TQString base = tqt_getenv("XDG_CONFIG_HOME");
        if (base.isEmpty()) base = tqt_home_dir() + "/.config";
        out << (base + "/" + sub);
        return out;
    }

    if (type == AppDataLocation) {
        TQString base = tqt_getenv("XDG_DATA_HOME");
        if (base.isEmpty()) base = tqt_home_dir() + "/.local/share";
        out << (base + "/" + sub);
        return out;
    }

    if (type == CacheLocation) {
        TQString base = tqt_getenv("XDG_CACHE_HOME");
        if (base.isEmpty()) base = tqt_home_dir() + "/.cache";
        out << (base + "/" + sub);
        return out;
    }

    if (type == RuntimeLocation) {
        TQString base = tqt_getenv("XDG_RUNTIME_DIR");
        if (base.isEmpty()) base = TQString("/tmp");
        out << (base + "/" + sub);
        return out;
    }

    if (type == TempLocation) {
        TQString base = tqt_getenv("TMPDIR");
        if (base.isEmpty()) base = TQString("/tmp");
        out << base;
        return out;
    }

    return out;
}

TQString TQtStandardPaths::writableLocation(StandardLocation type,
                                           const TQString& organization,
                                           const TQString& application) {
    const TQStringList l = standardLocations(type, organization, application);
    if (l.isEmpty()) return TQString();

    const TQString p = l[0];
    if (type != TempLocation) {
        tqt_mkpath(p);
    }
    return p;
}
