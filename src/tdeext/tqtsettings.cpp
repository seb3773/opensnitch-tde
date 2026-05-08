#include "tqtsettings.h"

#include "tqtstandardpaths.h"

#include <ntqcstring.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tqt_file_read_all(const TQString& path, TQByteArray* out) {
    if (!out) return -1;
    out->resize(0);

    const TQCString p = path.utf8();
    FILE* f = fopen(p.data(), "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    out->resize((int)sz);
    if (sz) {
        size_t rd = fread(out->data(), 1, (size_t)sz, f);
        if (rd != (size_t)sz) {
            fclose(f);
            out->resize(0);
            return -1;
        }
    }

    fclose(f);
    return 0;
}

static int tqt_file_write_all_atomic(const TQString& path, const TQByteArray& data) {
    const TQCString p = path.utf8();
    TQCString tmp = p + ".tmp";

    FILE* f = fopen(tmp.data(), "wb");
    if (!f) return -1;

    if (!data.isEmpty()) {
        size_t wr = fwrite(data.data(), 1, (size_t)data.size(), f);
        if (wr != (size_t)data.size()) {
            fclose(f);
            unlink(tmp.data());
            return -1;
        }
    }

    if (fflush(f) != 0) {
        fclose(f);
        unlink(tmp.data());
        return -1;
    }

    int fd = fileno(f);
    if (fd >= 0) {
        fsync(fd);
    }

    fclose(f);

    if (rename(tmp.data(), p.data()) != 0) {
        unlink(tmp.data());
        return -1;
    }

    return 0;
}

TQtSettings::TQtSettings(const TQString& organization, const TQString& application)
    : m_org(organization), m_app(application), m_groups(), m_loaded(0), m_kv(), m_dirty(0) {}

TQtSettings::~TQtSettings() {
    if (m_dirty) sync();
}

TQString TQtSettings::fileName() const {
    const TQString base = TQtStandardPaths::writableLocation(TQtStandardPaths::AppConfigLocation, m_org, m_app);
    return base + "/settings.ini";
}

void TQtSettings::beginGroup(const TQString& g) {
    if (!g.isEmpty()) m_groups << g;
}

void TQtSettings::endGroup() {
    if (!m_groups.isEmpty()) m_groups.pop_back();
}

TQString TQtSettings::fullKey(const TQString& key) const {
    if (m_groups.isEmpty()) return key;
    TQString p;
    for (TQStringList::ConstIterator it = m_groups.begin(); it != m_groups.end(); ++it) {
        if (!p.isEmpty()) p += "/";
        p += *it;
    }
    if (!p.isEmpty()) p += "/";
    return p + key;
}

void TQtSettings::loadIfNeeded() {
    if (m_loaded) return;
    load();
    m_loaded = 1;
}

static inline int tqt_is_space(unsigned char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n') ? 1 : 0;
}

static inline char* tqt_trim_left(char* s) {
    while (s && *s && tqt_is_space((unsigned char)*s)) ++s;
    return s;
}

static inline void tqt_trim_right(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n && tqt_is_space((unsigned char)s[n - 1])) {
        s[n - 1] = 0;
        --n;
    }
}

void TQtSettings::load() {
    m_kv.clear();

    TQByteArray b;
    if (tqt_file_read_all(fileName(), &b) != 0) return;

    if (b.isEmpty()) return;

    const char* p = b.data();
    const int n = b.size();

    int i = 0;
    TQString curGroup;

    while (i < n) {
        int lineStart = i;
        while (i < n && p[i] != '\n') ++i;
        int lineEnd = i;
        if (i < n && p[i] == '\n') ++i;

        int len = lineEnd - lineStart;
        if (len <= 0) continue;

        TQByteArray line;
        line.resize(len + 1);
        memcpy(line.data(), p + lineStart, (size_t)len);
        line.data()[len] = 0;

        char* s = line.data();
        s = tqt_trim_left(s);
        tqt_trim_right(s);

        if (!*s) continue;
        if (*s == ';' || *s == '#') continue;

        if (*s == '[') {
            char* e = strrchr(s, ']');
            if (!e) continue;
            *e = 0;
            curGroup = unescape(TQString::fromUtf8(s + 1));
            continue;
        }

        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* k = s;
        char* v = eq + 1;

        tqt_trim_right(k);
        v = tqt_trim_left(v);

        TQString ks = unescape(TQString::fromUtf8(k));
        TQString vs = unescape(TQString::fromUtf8(v));

        TQString fk;
        if (!curGroup.isEmpty()) fk = curGroup + "/" + ks;
        else fk = ks;

        m_kv[fk] = vs;
    }
}

TQString TQtSettings::escape(const TQString& s) {
    const TQCString u = s.utf8();
    const char* p = u.data();
    if (!p) return TQString();

    TQCString out;
    for (const char* c = p; *c; ++c) {
        if (*c == '\\') out += "\\\\";
        else if (*c == '\n') out += "\\n";
        else if (*c == '\r') out += "\\r";
        else if (*c == '\t') out += "\\t";
        else if (*c == '=') out += "\\=";
        else if (*c == '[') out += "\\[";
        else if (*c == ']') out += "\\]";
        else out += *c;
    }

    return TQString::fromUtf8(out.data());
}

TQString TQtSettings::unescape(const TQString& s) {
    const TQCString u = s.utf8();
    const char* p = u.data();
    if (!p) return TQString();

    TQCString out;
    for (const char* c = p; *c; ++c) {
        if (*c != '\\') {
            out += *c;
            continue;
        }
        ++c;
        if (!*c) break;
        if (*c == 'n') out += '\n';
        else if (*c == 'r') out += '\r';
        else if (*c == 't') out += '\t';
        else out += *c;
    }

    return TQString::fromUtf8(out.data());
}

TQString TQtSettings::variantToString(const TQVariant& v) {
    if (!v.isValid()) return TQString();

    if (v.type() == TQVariant::Bool) return v.toBool() ? TQString("true") : TQString("false");
    if (v.type() == TQVariant::Int) return TQString::number(v.toInt());
    if (v.type() == TQVariant::UInt) return TQString::number((unsigned int)v.toUInt());
    if (v.type() == TQVariant::Double) return TQString::number(v.toDouble(), 'g', 17);
    if (v.type() == TQVariant::StringList) return v.toStringList().join(";");

    return v.toString();
}

TQVariant TQtSettings::stringToVariant(const TQString& s) {
    if (s == "true") return TQVariant(1);
    if (s == "false") return TQVariant(0);

    bool ok = false;
    int iv = s.toInt(&ok);
    if (ok) return TQVariant(iv);

    double dv = s.toDouble(&ok);
    if (ok) return TQVariant(dv);

    return TQVariant(s);
}

TQVariant TQtSettings::value(const TQString& key, const TQVariant& def) const {
    ((TQtSettings*)this)->loadIfNeeded();

    const TQString fk = fullKey(key);
    TQMap<TQString, TQString>::ConstIterator it = m_kv.find(fk);
    if (it == m_kv.end()) return def;
    return stringToVariant(it.data());
}

void TQtSettings::setValue(const TQString& key, const TQVariant& v) {
    loadIfNeeded();

    const TQString fk = fullKey(key);
    m_kv[fk] = variantToString(v);
    m_dirty = 1;
}

void TQtSettings::remove(const TQString& key) {
    loadIfNeeded();

    const TQString fk = fullKey(key);
    m_kv.remove(fk);
    m_dirty = 1;
}

void TQtSettings::sync() {
    loadIfNeeded();

    TQMap<TQString, TQMap<TQString, TQString> > groups;

    for (TQMap<TQString, TQString>::ConstIterator it = m_kv.begin(); it != m_kv.end(); ++it) {
        const TQString k = it.key();
        const TQString v = it.data();

        int slash = k.findRev('/');
        if (slash < 0) {
            groups[TQString()][k] = v;
        } else {
            const TQString g = k.left(slash);
            const TQString kk = k.mid(slash + 1);
            groups[g][kk] = v;
        }
    }

    TQCString out;

    {
        TQMap<TQString, TQMap<TQString, TQString> >::ConstIterator itg = groups.find(TQString());
        if (itg != groups.end()) {
            const TQMap<TQString, TQString>& kv = itg.data();
            for (TQMap<TQString, TQString>::ConstIterator it = kv.begin(); it != kv.end(); ++it) {
                out += escape(it.key()).utf8();
                out += "=";
                out += escape(it.data()).utf8();
                out += "\n";
            }
        }
    }

    for (TQMap<TQString, TQMap<TQString, TQString> >::ConstIterator itg = groups.begin(); itg != groups.end(); ++itg) {
        if (itg.key().isEmpty()) continue;
        out += "\n[";
        out += escape(itg.key()).utf8();
        out += "]\n";
        const TQMap<TQString, TQString>& kv = itg.data();
        for (TQMap<TQString, TQString>::ConstIterator it = kv.begin(); it != kv.end(); ++it) {
            out += escape(it.key()).utf8();
            out += "=";
            out += escape(it.data()).utf8();
            out += "\n";
        }
    }

    TQByteArray data;
    data.duplicate(out.data(), (uint)out.length());

    tqt_file_write_all_atomic(fileName(), data);
    m_dirty = 0;
}
