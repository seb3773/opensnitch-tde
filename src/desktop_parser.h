#ifndef OPENSNITCH_DESKTOP_PARSER_H
#define OPENSNITCH_DESKTOP_PARSER_H

#include <ntqstring.h>
#include <ntqmap.h>
#include <ntqstringlist.h>
#include <pthread.h>

// Complexity: O(n) init scan, O(1) lookup
// Dependencies: TQt3, POSIX dirent
// Alignment: none required
// Thread safety: mutex-protected

struct AppInfo {
    TQString name;       // e.g. "Firefox Web Browser"
    TQString icon;       // e.g. "firefox" (icon theme name)
    TQString exec;       // e.g. "/usr/lib/firefox/firefox"
    TQString desktopPath;// e.g. "/usr/share/applications/firefox.desktop"
};

class DesktopParser {
public:
    static DesktopParser* instance();
    static void cleanup();

    // Lookup by process binary path, returns app info or NULL
    const AppInfo* lookup(const TQString& procPath) const;

    // Rescan directories (call on init or periodically)
    void rescan();

private:
    DesktopParser();
    ~DesktopParser();

    void scanDir(const TQString& dirPath);
    void parseDesktopFile(const TQString& filePath);
    TQString extractBinaryName(const TQString& execLine) const;

    static DesktopParser* s_instance;

    // Map: binary basename -> AppInfo (e.g. "firefox" -> info)
    TQMap<TQString, AppInfo> m_apps;
    // Map: full path -> AppInfo (e.g. "/usr/lib/firefox/firefox" -> info)
    TQMap<TQString, AppInfo> m_appsByPath;
    mutable pthread_mutex_t m_lock;
};

#endif // OPENSNITCH_DESKTOP_PARSER_H
