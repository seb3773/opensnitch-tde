#include "tqtjson.h"

#include <string.h>

extern "C" {
#include "parson.h"
}

static inline TQString tqtjson_from_utf8(const char* s) {
    if (!s) return TQString();
    return TQString::fromUtf8(s);
}

static inline TQCString tqtjson_to_utf8(const TQString& s) {
    return s.utf8();
}

struct TQtJsonValue::Shared {
    int ref;
    JSON_Value* v;
    Shared() : ref(1), v(0) {}
};

static JSON_Value* tqtjson_variant_to_value(const TQVariant& v);
static TQVariant tqtjson_value_to_variant(const JSON_Value* v);

TQtJsonValue::TQtJsonValue() : d(0) {}

TQtJsonValue::TQtJsonValue(JSON_Value* v) : d(0) {
    if (!v) return;
    d = new Shared;
    d->v = v;
}

TQtJsonValue::TQtJsonValue(const TQtJsonValue& other) : d(other.d) {
    if (d) ++d->ref;
}

TQtJsonValue& TQtJsonValue::operator=(const TQtJsonValue& other) {
    if (this == &other) return *this;
    if (d) {
        if (--d->ref == 0) {
            if (d->v) json_value_free(d->v);
            delete d;
        }
    }
    d = other.d;
    if (d) ++d->ref;
    return *this;
}

TQtJsonValue::~TQtJsonValue() {
    if (d) {
        if (--d->ref == 0) {
            if (d->v) json_value_free(d->v);
            delete d;
        }
    }
    d = 0;
}

void TQtJsonValue::detach() {
    if (!d || !d->v) return;
    if (d->ref == 1) return;

    JSON_Value* c = json_value_deep_copy(d->v);
    if (--d->ref == 0) {
        if (d->v) json_value_free(d->v);
        delete d;
    }
    d = new Shared;
    d->v = c;
}

TQtJsonValue TQtJsonValue::nullValue() {
    return TQtJsonValue(json_value_init_null());
}

TQtJsonValue TQtJsonValue::fromBool(bool v) {
    return TQtJsonValue(json_value_init_boolean(v ? 1 : 0));
}

TQtJsonValue TQtJsonValue::fromDouble(double v) {
    return TQtJsonValue(json_value_init_number(v));
}

TQtJsonValue TQtJsonValue::fromString(const TQString& v) {
    const TQCString u = tqtjson_to_utf8(v);
    return TQtJsonValue(json_value_init_string_with_len(u.data(), (size_t)u.length()));
}

TQtJsonValue TQtJsonValue::fromVariant(const TQVariant& v) {
    return TQtJsonValue(tqtjson_variant_to_value(v));
}

JSON_Value* TQtJsonValue::valuePtr() const {
    return d ? d->v : 0;
}

TQtJsonType TQtJsonValue::type() const {
    if (!d || !d->v) return TQTJSON_UNDEFINED;

    const JSON_Value_Type t = json_value_get_type(d->v);
    switch (t) {
        case JSONNull: return TQTJSON_NULL;
        case JSONBoolean: return TQTJSON_BOOL;
        case JSONNumber: return TQTJSON_DOUBLE;
        case JSONString: return TQTJSON_STRING;
        case JSONObject: return TQTJSON_OBJECT;
        case JSONArray: return TQTJSON_ARRAY;
        default: break;
    }
    return TQTJSON_UNDEFINED;
}

bool TQtJsonValue::isNull() const { return type() == TQTJSON_NULL; }
bool TQtJsonValue::isBool() const { return type() == TQTJSON_BOOL; }
bool TQtJsonValue::isDouble() const { return type() == TQTJSON_DOUBLE; }
bool TQtJsonValue::isString() const { return type() == TQTJSON_STRING; }
bool TQtJsonValue::isObject() const { return type() == TQTJSON_OBJECT; }
bool TQtJsonValue::isArray() const { return type() == TQTJSON_ARRAY; }

bool TQtJsonValue::toBool(bool def) const {
    if (!d || !d->v) return def;
    if (json_value_get_type(d->v) != JSONBoolean) return def;
    return json_value_get_boolean(d->v) ? true : false;
}

double TQtJsonValue::toDouble(double def) const {
    if (!d || !d->v) return def;
    if (json_value_get_type(d->v) != JSONNumber) return def;
    return json_value_get_number(d->v);
}

TQString TQtJsonValue::toString(const TQString& def) const {
    if (!d || !d->v) return def;
    if (json_value_get_type(d->v) != JSONString) return def;
    return tqtjson_from_utf8(json_value_get_string(d->v));
}

TQtJsonObject TQtJsonValue::toObject() const {
    if (!isObject()) return TQtJsonObject();
    return TQtJsonObject(*this);
}

TQtJsonArray TQtJsonValue::toArray() const {
    if (!isArray()) return TQtJsonArray();
    return TQtJsonArray(*this);
}

TQVariant TQtJsonValue::toVariant() const {
    if (!d || !d->v) return TQVariant();
    return tqtjson_value_to_variant(d->v);
}

TQtJsonObject::TQtJsonObject() : root() {}

TQtJsonObject::TQtJsonObject(const TQtJsonObject& other) : root(other.root) {}

TQtJsonObject& TQtJsonObject::operator=(const TQtJsonObject& other) {
    if (this == &other) return *this;
    root = other.root;
    return *this;
}

TQtJsonObject::~TQtJsonObject() {}

TQtJsonObject TQtJsonObject::empty() {
    return TQtJsonObject(TQtJsonValue(json_value_init_object()));
}

TQtJsonObject::TQtJsonObject(const TQtJsonValue& rootValue) : root(rootValue) {}

JSON_Object* TQtJsonObject::objectPtr() const {
    JSON_Value* v = root.valuePtr();
    if (!v) return 0;
    if (json_value_get_type(v) != JSONObject) return 0;
    return json_value_get_object(v);
}

bool TQtJsonObject::isValid() const {
    return objectPtr() != 0;
}

int TQtJsonObject::size() const {
    JSON_Object* o = objectPtr();
    if (!o) return 0;
    return (int)json_object_get_count(o);
}

bool TQtJsonObject::contains(const TQString& key) const {
    JSON_Object* o = objectPtr();
    if (!o) return false;
    const TQCString k = tqtjson_to_utf8(key);
    return json_object_has_value(o, k.data()) ? true : false;
}

TQtJsonValue TQtJsonObject::value(const TQString& key) const {
    JSON_Object* o = objectPtr();
    if (!o) return TQtJsonValue();
    const TQCString k = tqtjson_to_utf8(key);
    JSON_Value* v = json_object_get_value(o, k.data());
    if (!v) return TQtJsonValue();
    return TQtJsonValue(json_value_deep_copy(v));
}

TQStringList TQtJsonObject::keys() const {
    TQStringList out;
    JSON_Object* o = objectPtr();
    if (!o) return out;

    const size_t n = json_object_get_count(o);
    for (size_t i = 0; i < n; ++i) {
        const char* name = json_object_get_name(o, i);
        if (!name) continue;
        out.append(tqtjson_from_utf8(name));
    }
    return out;
}

bool TQtJsonObject::insert(const TQString& key, const TQtJsonValue& v) {
    if (!root.isObject()) return false;
    root.detach();

    JSON_Object* o = objectPtr();
    if (!o) return false;

    const TQCString k = tqtjson_to_utf8(key);
    JSON_Value* src = v.valuePtr();
    if (!src) return json_object_set_null(o, k.data()) == JSONSuccess;

    JSON_Value* c = json_value_deep_copy(src);
    if (!c) return false;

    if (json_object_set_value(o, k.data(), c) != JSONSuccess) {
        json_value_free(c);
        return false;
    }
    return true;
}

bool TQtJsonObject::remove(const TQString& key) {
    if (!root.isObject()) return false;
    root.detach();

    JSON_Object* o = objectPtr();
    if (!o) return false;

    const TQCString k = tqtjson_to_utf8(key);
    return json_object_remove(o, k.data()) == JSONSuccess;
}

TQMap<TQString, TQVariant> TQtJsonObject::toVariantMap() const {
    TQMap<TQString, TQVariant> out;
    JSON_Object* o = objectPtr();
    if (!o) return out;

    const size_t n = json_object_get_count(o);
    for (size_t i = 0; i < n; ++i) {
        const char* name = json_object_get_name(o, i);
        JSON_Value* v = json_object_get_value_at(o, i);
        if (!name || !v) continue;
        out.insert(tqtjson_from_utf8(name), tqtjson_value_to_variant(v));
    }
    return out;
}

TQtJsonArray::TQtJsonArray() : root() {}

TQtJsonArray::TQtJsonArray(const TQtJsonArray& other) : root(other.root) {}

TQtJsonArray& TQtJsonArray::operator=(const TQtJsonArray& other) {
    if (this == &other) return *this;
    root = other.root;
    return *this;
}

TQtJsonArray::~TQtJsonArray() {}

TQtJsonArray TQtJsonArray::empty() {
    return TQtJsonArray(TQtJsonValue(json_value_init_array()));
}

TQtJsonArray::TQtJsonArray(const TQtJsonValue& rootValue) : root(rootValue) {}

JSON_Array* TQtJsonArray::arrayPtr() const {
    JSON_Value* v = root.valuePtr();
    if (!v) return 0;
    if (json_value_get_type(v) != JSONArray) return 0;
    return json_value_get_array(v);
}

bool TQtJsonArray::isValid() const {
    return arrayPtr() != 0;
}

int TQtJsonArray::size() const {
    JSON_Array* a = arrayPtr();
    if (!a) return 0;
    return (int)json_array_get_count(a);
}

TQtJsonValue TQtJsonArray::at(int index) const {
    JSON_Array* a = arrayPtr();
    if (!a) return TQtJsonValue();
    if (index < 0) return TQtJsonValue();

    const size_t i = (size_t)index;
    if (i >= json_array_get_count(a)) return TQtJsonValue();

    JSON_Value* v = json_array_get_value(a, i);
    if (!v) return TQtJsonValue();
    return TQtJsonValue(json_value_deep_copy(v));
}

bool TQtJsonArray::append(const TQtJsonValue& v) {
    if (!root.isArray()) return false;
    root.detach();

    JSON_Array* a = arrayPtr();
    if (!a) return false;

    JSON_Value* src = v.valuePtr();
    JSON_Value* c = src ? json_value_deep_copy(src) : json_value_init_null();
    if (!c) return false;

    if (json_array_append_value(a, c) != JSONSuccess) {
        json_value_free(c);
        return false;
    }
    return true;
}

bool TQtJsonArray::removeAt(int index) {
    if (!root.isArray()) return false;
    root.detach();

    JSON_Array* a = arrayPtr();
    if (!a) return false;
    if (index < 0) return false;

    const size_t i = (size_t)index;
    return json_array_remove(a, i) == JSONSuccess;
}

TQValueList<TQVariant> TQtJsonArray::toVariantList() const {
    TQValueList<TQVariant> out;
    JSON_Array* a = arrayPtr();
    if (!a) return out;

    const size_t n = json_array_get_count(a);
    for (size_t i = 0; i < n; ++i) {
        JSON_Value* v = json_array_get_value(a, i);
        if (!v) {
            out.append(TQVariant());
            continue;
        }
        out.append(tqtjson_value_to_variant(v));
    }
    return out;
}

TQtJsonDocument::TQtJsonDocument() : root() {}

TQtJsonDocument::TQtJsonDocument(const TQtJsonDocument& other) : root(other.root) {}

TQtJsonDocument& TQtJsonDocument::operator=(const TQtJsonDocument& other) {
    if (this == &other) return *this;
    root = other.root;
    return *this;
}

TQtJsonDocument::~TQtJsonDocument() {}

TQtJsonDocument TQtJsonDocument::fromJson(const TQString& json, TQtJsonParseError* err) {
    if (err) {
        err->offset = -1;
        err->error = TQString();
    }

    const TQCString u = tqtjson_to_utf8(json);

    JSON_Value* v = json_parse_string(u.data());
    if (!v) {
        if (err) err->error = "parse error";
        return TQtJsonDocument();
    }

    TQtJsonDocument doc;
    doc.root = TQtJsonValue(v);
    return doc;
}

TQtJsonDocument TQtJsonDocument::fromValue(const TQtJsonValue& v) {
    TQtJsonDocument doc;
    doc.root = v;
    return doc;
}

bool TQtJsonDocument::isNull() const { return root.type() == TQTJSON_UNDEFINED; }
bool TQtJsonDocument::isObject() const { return root.isObject(); }
bool TQtJsonDocument::isArray() const { return root.isArray(); }

TQtJsonObject TQtJsonDocument::object() const { return root.toObject(); }
TQtJsonArray TQtJsonDocument::array() const { return root.toArray(); }
TQtJsonValue TQtJsonDocument::value() const { return root; }

void TQtJsonDocument::setValue(const TQtJsonValue& v) {
    root = v;
}

TQString TQtJsonDocument::toJson(bool pretty) const {
    JSON_Value* v = root.valuePtr();
    if (!v) return TQString();

    char* s = pretty ? json_serialize_to_string_pretty(v) : json_serialize_to_string(v);
    if (!s) return TQString();

    TQString out = tqtjson_from_utf8(s);
    json_free_serialized_string(s);
    return out;
}

static JSON_Value* tqtjson_variant_list_to_array(const TQValueList<TQVariant>& list) {
    JSON_Value* v = json_value_init_array();
    if (!v) return 0;

    JSON_Array* a = json_value_get_array(v);
    if (!a) {
        json_value_free(v);
        return 0;
    }

    for (TQValueList<TQVariant>::ConstIterator it = list.begin(); it != list.end(); ++it) {
        JSON_Value* e = tqtjson_variant_to_value(*it);
        if (!e) e = json_value_init_null();
        if (json_array_append_value(a, e) != JSONSuccess) {
            json_value_free(e);
            json_value_free(v);
            return 0;
        }
    }

    return v;
}

static JSON_Value* tqtjson_variant_map_to_object(const TQMap<TQString, TQVariant>& map) {
    JSON_Value* v = json_value_init_object();
    if (!v) return 0;

    JSON_Object* o = json_value_get_object(v);
    if (!o) {
        json_value_free(v);
        return 0;
    }

    for (TQMap<TQString, TQVariant>::ConstIterator it = map.begin(); it != map.end(); ++it) {
        const TQCString k = tqtjson_to_utf8(it.key());
        JSON_Value* e = tqtjson_variant_to_value(it.data());
        if (!e) e = json_value_init_null();

        if (json_object_set_value(o, k.data(), e) != JSONSuccess) {
            json_value_free(e);
            json_value_free(v);
            return 0;
        }
    }

    return v;
}

static JSON_Value* tqtjson_variant_to_value(const TQVariant& v) {
    if (!v.isValid() || v.isNull()) return json_value_init_null();

    switch (v.type()) {
        case TQVariant::Bool:
            return json_value_init_boolean(v.toBool() ? 1 : 0);
        case TQVariant::Double:
            return json_value_init_number(v.toDouble());
        case TQVariant::Int:
            return json_value_init_number((double)v.toInt());
        case TQVariant::UInt:
            return json_value_init_number((double)v.toUInt());
        case TQVariant::LongLong:
            return json_value_init_number((double)v.toLongLong());
        case TQVariant::ULongLong:
            return json_value_init_number((double)v.toULongLong());
        case TQVariant::String: {
            const TQCString u = tqtjson_to_utf8(v.toString());
            return json_value_init_string_with_len(u.data(), (size_t)u.length());
        }
        case TQVariant::CString: {
            const TQCString c = v.toCString();
            return json_value_init_string_with_len(c.data(), (size_t)c.length());
        }
        case TQVariant::ByteArray: {
            const TQByteArray b = v.toByteArray();
            if (b.isEmpty()) return json_value_init_string_with_len("", 0);
            return json_value_init_string_with_len(b.data(), (size_t)b.size());
        }
        case TQVariant::List:
            return tqtjson_variant_list_to_array(v.toList());
        case TQVariant::Map:
            return tqtjson_variant_map_to_object(v.toMap());
        default:
            break;
    }

    return json_value_init_null();
}

static TQVariant tqtjson_object_to_variant_map(const JSON_Object* o) {
    TQMap<TQString, TQVariant> out;
    if (!o) return TQVariant(out);

    const size_t n = json_object_get_count(o);
    for (size_t i = 0; i < n; ++i) {
        const char* name = json_object_get_name(o, i);
        const JSON_Value* v = json_object_get_value_at(o, i);
        if (!name || !v) continue;
        out.insert(tqtjson_from_utf8(name), tqtjson_value_to_variant(v));
    }

    return TQVariant(out);
}

static TQVariant tqtjson_array_to_variant_list(const JSON_Array* a) {
    TQValueList<TQVariant> out;
    if (!a) return TQVariant(out);

    const size_t n = json_array_get_count(a);
    for (size_t i = 0; i < n; ++i) {
        const JSON_Value* v = json_array_get_value(a, i);
        if (!v) {
            out.append(TQVariant());
            continue;
        }
        out.append(tqtjson_value_to_variant(v));
    }

    return TQVariant(out);
}

static TQVariant tqtjson_value_to_variant(const JSON_Value* v) {
    if (!v) return TQVariant();

    const JSON_Value_Type t = json_value_get_type(v);
    switch (t) {
        case JSONNull:
            return TQVariant();
        case JSONBoolean:
            return TQVariant((bool)(json_value_get_boolean(v) ? true : false));
        case JSONNumber:
            return TQVariant(json_value_get_number(v));
        case JSONString:
            return TQVariant(tqtjson_from_utf8(json_value_get_string(v)));
        case JSONObject:
            return tqtjson_object_to_variant_map(json_value_get_object(v));
        case JSONArray:
            return tqtjson_array_to_variant_list(json_value_get_array(v));
        default:
            break;
    }

    return TQVariant();
}
