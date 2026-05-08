#ifndef TQTJSON_H
#define TQTJSON_H

#include <stddef.h>

#include <ntqstring.h>
#include <ntqstringlist.h>
#include <ntqvaluelist.h>
#include <ntqmap.h>
#include <ntqvariant.h>

struct json_value_t;
struct json_object_t;
struct json_array_t;

typedef struct json_value_t JSON_Value;
typedef struct json_object_t JSON_Object;
typedef struct json_array_t JSON_Array;

enum TQtJsonType {
    TQTJSON_NULL = 0,
    TQTJSON_BOOL,
    TQTJSON_DOUBLE,
    TQTJSON_STRING,
    TQTJSON_OBJECT,
    TQTJSON_ARRAY,
    TQTJSON_UNDEFINED
};

class TQtJsonObject;
class TQtJsonArray;

class TQtJsonValue {
public:
    TQtJsonValue();
    TQtJsonValue(const TQtJsonValue& other);
    TQtJsonValue& operator=(const TQtJsonValue& other);
    ~TQtJsonValue();

    static TQtJsonValue nullValue();
    static TQtJsonValue fromBool(bool v);
    static TQtJsonValue fromDouble(double v);
    static TQtJsonValue fromString(const TQString& v);
    static TQtJsonValue fromVariant(const TQVariant& v);

    TQtJsonType type() const;

    bool isNull() const;
    bool isBool() const;
    bool isDouble() const;
    bool isString() const;
    bool isObject() const;
    bool isArray() const;

    bool toBool(bool def = false) const;
    double toDouble(double def = 0.0) const;
    TQString toString(const TQString& def = TQString()) const;

    TQtJsonObject toObject() const;
    TQtJsonArray toArray() const;

    TQVariant toVariant() const;

private:
    struct Shared;
    Shared* d;

    explicit TQtJsonValue(JSON_Value* v);
    JSON_Value* valuePtr() const;

    void detach();

    friend class TQtJsonDocument;
    friend class TQtJsonObject;
    friend class TQtJsonArray;
};

class TQtJsonObject {
public:
    TQtJsonObject();
    TQtJsonObject(const TQtJsonObject& other);
    TQtJsonObject& operator=(const TQtJsonObject& other);
    ~TQtJsonObject();

    static TQtJsonObject empty();

    bool isValid() const;
    int size() const;

    bool contains(const TQString& key) const;
    TQtJsonValue value(const TQString& key) const;

    TQStringList keys() const;

    bool insert(const TQString& key, const TQtJsonValue& v);
    bool remove(const TQString& key);

    TQMap<TQString, TQVariant> toVariantMap() const;

private:
    TQtJsonValue root;

    explicit TQtJsonObject(const TQtJsonValue& rootValue);
    JSON_Object* objectPtr() const;

    friend class TQtJsonValue;
    friend class TQtJsonDocument;
};

class TQtJsonArray {
public:
    TQtJsonArray();
    TQtJsonArray(const TQtJsonArray& other);
    TQtJsonArray& operator=(const TQtJsonArray& other);
    ~TQtJsonArray();

    static TQtJsonArray empty();

    bool isValid() const;
    int size() const;

    TQtJsonValue at(int index) const;
    bool append(const TQtJsonValue& v);
    bool removeAt(int index);

    TQValueList<TQVariant> toVariantList() const;

private:
    TQtJsonValue root;

    explicit TQtJsonArray(const TQtJsonValue& rootValue);
    JSON_Array* arrayPtr() const;

    friend class TQtJsonValue;
    friend class TQtJsonDocument;
};

class TQtJsonParseError {
public:
    TQtJsonParseError() : offset(-1) {}

    int offset;
    TQString error;
};

class TQtJsonDocument {
public:
    TQtJsonDocument();
    TQtJsonDocument(const TQtJsonDocument& other);
    TQtJsonDocument& operator=(const TQtJsonDocument& other);
    ~TQtJsonDocument();

    static TQtJsonDocument fromJson(const TQString& json, TQtJsonParseError* err = 0);
    static TQtJsonDocument fromValue(const TQtJsonValue& v);

    bool isNull() const;
    bool isObject() const;
    bool isArray() const;

    TQtJsonObject object() const;
    TQtJsonArray array() const;
    TQtJsonValue value() const;

    void setValue(const TQtJsonValue& v);

    TQString toJson(bool pretty = false) const;

private:
    TQtJsonValue root;
};

#endif
