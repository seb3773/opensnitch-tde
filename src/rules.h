#ifndef OPENSNITCH_RULES_H
#define OPENSNITCH_RULES_H

#include <ntqobject.h>
#include <ntqstring.h>
#include <ntqmap.h>
#include <ntqdatetime.h>
#include <ntqvaluelist.h>

#include "ui.pb.h"

// Complexity: O(1) lookup by name+node, O(n) for list/all
// Dependencies: protobuf, database
// Alignment: none required

class Rules : public TQObject {
    TQ_OBJECT

public:
    static Rules* instance();
    static void destroy();

    struct RuleRecord {
        TQString time;
        TQString node;
        TQString name;
        TQString description;
        bool enabled;
        bool precedence;
        bool nolog;
        TQString action;
        TQString duration;
        TQString opType;
        bool opSensitive;
        TQString opOperand;
        TQString opData;
        TQDateTime created;

        RuleRecord() : enabled(true), precedence(false), nolog(false), opSensitive(false) {}
    };

    void addRules(const TQString& addr, const google::protobuf::RepeatedPtrField<protocol::Rule>& rules);
    void add(const TQString& time, const TQString& node, const protocol::Rule& rule);
    void remove(const TQString& name, const TQString& node);
    void disable(const TQString& addr, const TQString& ruleName);
    void updateTime(const TQString& time, const TQString& ruleName, const TQString& node);
    void setEnabled(const TQString& name, const TQString& node, bool enabled);

    TQValueList<RuleRecord> getByNode(const TQString& addr);
    RuleRecord* get(const TQString& name, const TQString& node);
    const TQMap<TQString, RuleRecord>& rules() const { return m_rules; }

    static protocol::Rule toProtobuf(const RuleRecord& r);

signals:
    void updated();

private:
    Rules();
    ~Rules();

    static Rules* s_instance;
    TQMap<TQString, RuleRecord> m_rules; // key = node:name
    TQString makeKey(const TQString& node, const TQString& name) const;
};

#endif // OPENSNITCH_RULES_H
