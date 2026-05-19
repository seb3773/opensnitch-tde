#include "rules.h"
#include "database.h"
#include "config.h"

#include <ntqdatetime.h>

static inline TQString s2q(const std::string& s) { return TQString(s.c_str()); }

Rules* Rules::s_instance = 0;

Rules* Rules::instance()
{
    if (!s_instance)
        s_instance = new Rules();
    return s_instance;
}

void Rules::destroy()
{
    delete s_instance;
    s_instance = 0;
}

Rules::Rules()  : TQObject(0) {}
Rules::~Rules() {}

TQString Rules::makeKey(const TQString& node, const TQString& name) const
{
    return node + ":" + name;
}

void Rules::addRules(const TQString& addr, const google::protobuf::RepeatedPtrField<protocol::Rule>& rules)
{
    for (int i = 0; i < rules.size(); i++) {
        const protocol::Rule& r = rules.Get(i);
        TQString time = TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        add(time, addr, r);
    }
    emit updated();
}

void Rules::add(const TQString& time, const TQString& node, const protocol::Rule& rule)
{
    RuleRecord rec;
    rec.time = time;
    rec.node = node;
    rec.name = s2q(rule.name());
    rec.enabled = rule.enabled();
    rec.precedence = rule.precedence();
    rec.action = s2q(rule.action());
    rec.duration = s2q(rule.duration());
    rec.opType = s2q(rule.operator_().type());
    rec.opSensitive = rule.operator_().sensitive();
    rec.opOperand = s2q(rule.operator_().operand());
    rec.opData = s2q(rule.operator_().data());

    m_rules[makeKey(node, rec.name)] = rec;

    Database* db = Database::instance();
    if (db && db->isOpen()) {
        TQValueList<TQVariant> vals;
        vals << TQVariant(rec.time)
             << TQVariant(rec.node)
             << TQVariant(rec.name)
             << TQVariant(TQString(rec.enabled ? "True" : "False"))
             << TQVariant(TQString(rec.precedence ? "True" : "False"))
             << TQVariant(rec.action)
             << TQVariant(rec.duration)
             << TQVariant(rec.opType)
             << TQVariant(TQString(rec.opSensitive ? "True" : "False"))
             << TQVariant(rec.opOperand)
             << TQVariant(rec.opData);
        db->insert("rules",
                   "(time, node, name, enabled, precedence, action, duration, "
                   "operator_type, operator_sensitive, operator_operand, operator_data)",
                   vals, "REPLACE");
    }
}

void Rules::remove(const TQString& name, const TQString& node)
{
    TQString key = makeKey(node, name);
    m_rules.remove(key);

    Database* db = Database::instance();
    if (db && db->isOpen()) {
        TQValueList<TQVariant> vals;
        vals << TQVariant(name) << TQVariant(node);
        db->deleteRows("rules", "name=? AND node=?", vals);
    }
    emit updated();
}

void Rules::disable(const TQString& addr, const TQString& ruleName)
{
    TQString key = makeKey(addr, ruleName);
    if (m_rules.contains(key)) {
        m_rules[key].enabled = false;
        emit updated();
    }
}

void Rules::setEnabled(const TQString& name, const TQString& node, bool enabled)
{
    TQString key = makeKey(node, name);
    if (m_rules.contains(key))
        m_rules[key].enabled = enabled;

    Database* db = Database::instance();
    if (db && db->isOpen()) {
        TQValueList<TQVariant> vals;
        vals << TQVariant(TQString(enabled ? "True" : "False"))
             << TQVariant(name)
             << TQVariant(node);
        db->update("rules", "enabled=?", vals, "name=? AND node=?");
    }
    emit updated();
}

void Rules::updateTime(const TQString& time, const TQString& ruleName, const TQString& node)
{
    TQString key = makeKey(node, ruleName);
    if (m_rules.contains(key))
        m_rules[key].time = time;
}

TQValueList<Rules::RuleRecord> Rules::getByNode(const TQString& addr)
{
    TQValueList<RuleRecord> result;
    for (TQMap<TQString, RuleRecord>::ConstIterator it = m_rules.begin(); it != m_rules.end(); ++it) {
        if (it.data().node == addr)
            result.append(it.data());
    }
    return result;
}

Rules::RuleRecord* Rules::get(const TQString& name, const TQString& node)
{
    TQString key = makeKey(node, name);
    if (m_rules.contains(key))
        return &m_rules[key];
    return 0;
}

protocol::Rule Rules::toProtobuf(const RuleRecord& r)
{
    protocol::Rule rule;
    rule.set_name(r.name.utf8().data());
    rule.set_enabled(r.enabled);
    rule.set_precedence(r.precedence);
    rule.set_action(r.action.utf8().data());
    rule.set_duration(r.duration.utf8().data());

    protocol::Operator* op = rule.mutable_operator_();
    op->set_type(r.opType.utf8().data());
    op->set_sensitive(r.opSensitive);
    op->set_operand(r.opOperand.utf8().data());
    op->set_data(r.opData.utf8().data());

    return rule;
}
