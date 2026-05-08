#ifndef TQTSETTINGS_H
#define TQTSETTINGS_H

#include <ntqmap.h>
#include <ntqstring.h>
#include <ntqstringlist.h>
#include <ntqvariant.h>

class TQtSettings {
public:
    TQtSettings(const TQString& organization = TQString(),
                const TQString& application = TQString());

    ~TQtSettings();

    TQVariant value(const TQString& key, const TQVariant& def = TQVariant()) const;
    void setValue(const TQString& key, const TQVariant& v);
    void remove(const TQString& key);

    void beginGroup(const TQString& g);
    void endGroup();

    void sync();

    TQString fileName() const;

private:
    TQString fullKey(const TQString& key) const;

    void loadIfNeeded();
    void load();

    static TQString variantToString(const TQVariant& v);
    static TQVariant stringToVariant(const TQString& s);

    static TQString escape(const TQString& s);
    static TQString unescape(const TQString& s);

    TQString m_org;
    TQString m_app;
    TQStringList m_groups;

    mutable int m_loaded;
    mutable TQMap<TQString, TQString> m_kv;

    int m_dirty;
};

#endif
