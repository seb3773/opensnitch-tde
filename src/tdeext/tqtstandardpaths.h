#ifndef TQTSTANDARDPATHS_H
#define TQTSTANDARDPATHS_H

#include <ntqstring.h>
#include <ntqstringlist.h>

class TQtStandardPaths {
public:
    enum StandardLocation {
        AppConfigLocation,
        AppDataLocation,
        CacheLocation,
        TempLocation,
        RuntimeLocation
    };

    static TQString writableLocation(StandardLocation type,
                                    const TQString& organization = TQString(),
                                    const TQString& application = TQString());

    static TQStringList standardLocations(StandardLocation type,
                                         const TQString& organization = TQString(),
                                         const TQString& application = TQString());

private:
    static TQString appSubdir(const TQString& organization, const TQString& application);
};

#endif
