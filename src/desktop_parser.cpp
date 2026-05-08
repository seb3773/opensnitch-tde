#include "desktop_parser.h"

#include <ntqfile.h>
#include <ntqtextstream.h>
#include <ntqdir.h>
#include <ntqregexp.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

DesktopParser* DesktopParser::s_instance = 0;

DesktopParser::DesktopParser()
{
    pthread_mutex_init(&m_lock, 0);
    rescan();
}

DesktopParser::~DesktopParser()
{
    pthread_mutex_destroy(&m_lock);
}

DesktopParser* DesktopParser::instance()
{
    if (!s_instance)
        s_instance = new DesktopParser();
    return s_instance;
}

void DesktopParser::cleanup()
{
    delete s_instance;
    s_instance = 0;
}

void DesktopParser::rescan()
{
    pthread_mutex_lock(&m_lock);
    m_apps.clear();
    m_appsByPath.clear();
    pthread_mutex_unlock(&m_lock);

    // Scan standard XDG directories
    static const char* dirs[] = {
        "/usr/share/applications/",
        "/usr/local/share/applications/",
        "/var/lib/flatpak/exports/share/applications/",
        "/home/.local/share/applications/",
        NULL
    };

    // Also scan HOME
    TQString homeDir = TQString(getenv("HOME")) + "/.local/share/applications/";

    for (int i = 0; dirs[i]; i++)
        scanDir(TQString(dirs[i]));
    scanDir(homeDir);
}

void DesktopParser::scanDir(const TQString& dirPath)
{
    DIR* d = opendir(dirPath.latin1());
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != 0) {
        TQString name(ent->d_name);
        if (!name.endsWith(".desktop")) continue;

        TQString fullPath = dirPath + name;
        parseDesktopFile(fullPath);
    }
    closedir(d);
}

void DesktopParser::parseDesktopFile(const TQString& filePath)
{
    // Only parse visible, non-hidden desktop files
    TQFile file(filePath);
    if (!file.open(IO_ReadOnly)) return;

    TQTextStream stream(&file);
    stream.setEncoding(TQTextStream::Locale);

    bool inDesktopEntry = false;
    TQString name, icon, exec;
    bool noDisplay = false;

    TQString line;
    while (!stream.atEnd()) {
        line = stream.readLine();

        if (line.startsWith("[Desktop Entry]")) {
            inDesktopEntry = true;
            continue;
        }
        if (line.startsWith("[") && inDesktopEntry) {
            // Left the Desktop Entry section
            break;
        }
        if (!inDesktopEntry) continue;

        if (line.startsWith("Name=")) {
            name = line.mid(5);
        } else if (line.startsWith("Icon=")) {
            icon = line.mid(5);
        } else if (line.startsWith("Exec=")) {
            exec = line.mid(5);
        } else if (line.startsWith("NoDisplay=")) {
            TQString val = line.mid(10).lower();
            noDisplay = (val == "true");
        }
    }

    file.close();

    if (noDisplay || exec.isEmpty()) return;

    // Extract the binary name from Exec line
    // Exec can be: /usr/bin/foo %U, env VAR /path/to/bin, /path/to/bin --arg
    TQString binName = extractBinaryName(exec);
    if (binName.isEmpty()) return;

    // If no Name found, use binary name
    if (name.isEmpty())
        name = binName;

    AppInfo info;
    info.name = name;
    info.icon = icon;
    info.exec = exec;
    info.desktopPath = filePath;

    pthread_mutex_lock(&m_lock);

    // Index by binary basename
    m_apps.insert(binName, info);

    // Also try to index by full path if exec contains one
    if (binName != exec && exec.startsWith("/")) {
        // Clean exec: remove %U, %f etc placeholders
        TQString cleanExec = exec;
        cleanExec.replace(TQRegExp(" %[fFuUdDnNickvm]+$"), "");
        cleanExec = cleanExec.stripWhiteSpace();
        if (cleanExec.startsWith("/"))
            m_appsByPath.insert(cleanExec, info);
    }

    pthread_mutex_unlock(&m_lock);
}

TQString DesktopParser::extractBinaryName(const TQString& execLine) const
{
    // Handle cases like:
    //   /usr/bin/foo
    //   /usr/bin/foo %U
    //   env VAR=/foo /usr/bin/bar
    //   sh -c "/path/to/script"
    //   /opt/app/bin/app --flag

    TQString cleaned = execLine.stripWhiteSpace();

    // Remove placeholder arguments (%U, %f, etc.)
    cleaned.replace(TQRegExp(" %[fFuUdDnNickvm]+"), "");

    // Handle "env ... /path/bin" pattern
    if (cleaned.startsWith("env ")) {
        // Find last path-like argument
        TQStringList parts = TQStringList::split(TQRegExp("\\s+"), cleaned);
        for (int i = parts.count() - 1; i >= 1; i--) {
            if (parts[i].contains('/')) {
                cleaned = parts[i];
                break;
            }
        }
        if (cleaned.startsWith("env "))
            return TQString();
    }

    // Handle "sh -c '...'" pattern
    if (cleaned.startsWith("sh "))
        return TQString();

    // Extract basename from path
    int lastSlash = cleaned.findRev('/');
    if (lastSlash >= 0)
        cleaned = cleaned.mid(lastSlash + 1);

    // Remove any remaining arguments after binary name
    int spacePos = cleaned.find(' ');
    if (spacePos > 0)
        cleaned = cleaned.left(spacePos);

    return cleaned;
}

const AppInfo* DesktopParser::lookup(const TQString& procPath) const
{
    pthread_mutex_lock(&m_lock);

    // Try by full path first
    TQMap<TQString, AppInfo>::ConstIterator it = m_appsByPath.find(procPath);
    if (it != m_appsByPath.end()) {
        pthread_mutex_unlock(&m_lock);
        return &it.data();
    }

    // Try by binary basename
    TQString binName = procPath;
    int lastSlash = binName.findRev('/');
    if (lastSlash >= 0)
        binName = binName.mid(lastSlash + 1);

    it = m_apps.find(binName);
    if (it != m_apps.end()) {
        pthread_mutex_unlock(&m_lock);
        return &it.data();
    }

    pthread_mutex_unlock(&m_lock);
    return 0;
}
