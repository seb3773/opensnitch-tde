#include "rule_dialog.h"

#include <ntqlabel.h>
#include <ntqlayout.h>
#include <ntqlineedit.h>
#include <ntqcombobox.h>
#include <ntqcheckbox.h>
#include <ntqbuttongroup.h>
#include <ntqradiobutton.h>
#include <ntqtabwidget.h>
#include <ntqtoolbutton.h>
#include <ntqpushbutton.h>
#include <ntqframe.h>
#include <ntqimage.h>
#include <ntqiconset.h>
#include <ntqfiledialog.h>
#include <ntqdir.h>

#include <sys/stat.h>

#include <kiconloader.h>
#include <tdeglobal.h>

#include "embedded_icons.h"
#include "icon_theme.h"
#include "main_window.h"
#include "rules.h"
#include "nodes.h"
#include "config.h"

#include <ntqmessagebox.h>
#include <ntqdatetime.h>


static inline TQPixmap loadThemeIcon(const char* name, int size)
{
    return TDEGlobal::iconLoader()->loadIcon(name, TDEIcon::Desktop, size,
                                             TDEIcon::DefaultState, 0, true);
}

static inline TQString slugify_like_python(const TQString& in)
{
    // Complexity: O(n)
    // Dependencies: none
    // Assumptions: ASCII-ish input; converts to lowercase and keeps [a-z0-9], '-' only
    // Notes: This is a minimal replacement of python-slugify() for rule names.
    TQString s = in.lower();
    TQString out;
    out.reserve((int)s.length());

    int prevDash = 0;
    for (int i = 0; i < (int)s.length(); ++i) {
        const unsigned short uc = s[(int)i].unicode();
        const int isAlnum = ((uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9'));
        if (isAlnum) {
            out += TQChar(uc);
            prevDash = 0;
        } else {
            if (!prevDash) {
                out += '-';
                prevDash = 1;
            }
        }
    }
    while (out.startsWith("-")) out.remove(0, 1);
    while (out.endsWith("-")) out.truncate(out.length() - 1);
    return out;
}

static inline TQString basename_like_path(const TQString& p)
{
    if (p.isEmpty())
        return TQString();
    int lastSlash = p.findRev('/');
    if (lastSlash < 0)
        return p;
    if (lastSlash + 1 >= (int)p.length())
        return p;
    return p.mid(lastSlash + 1);
}

static inline int looksLikeRegexp(const TQString& s)
{
    // Match Python Debian RulesEditor _is_regex(): charset="\\*{[|^?$"
    if (s.find("\\") >= 0) return 1;
    if (s.find("*") >= 0) return 1;
    if (s.find("{") >= 0) return 1;
    if (s.find("[") >= 0) return 1;
    if (s.find("|") >= 0) return 1;
    if (s.find("^") >= 0) return 1;
    if (s.find("?") >= 0) return 1;
    if (s.find("$") >= 0) return 1;
    return 0;
}

static inline int isDirPath(const TQString& path)
{
    // Complexity: O(1)
    // Dependencies: POSIX stat
    // Semantics: strict match for Python os.path.isdir
    if (path.isEmpty())
        return 0;
    struct stat st;
    if (::stat(path.latin1(), &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static inline void applyEmbeddedIcon16(TQToolButton* b, const unsigned char* data, int len)
{
    if (!b || !data || !len) return;
    TQPixmap pm = IconTheme::loadEmbeddedPixmap(data, len, 22);
    if (pm.isNull()) return;
    b->setIconSet(TQIconSet(pm, TQIconSet::Small));
    b->setUsesBigPixmap(false);
}

static inline TQString jsonEscape(const TQString& in)
{
    // Complexity: O(n)
    // Dependencies: none
    // Notes: minimal JSON escaping for strings.
    TQString out;
    out.reserve((int)in.length() + 8);
    for (int i = 0; i < (int)in.length(); ++i) {
        const unsigned short uc = in[i].unicode();
        if (uc == '"') out += "\\\"";
        else if (uc == '\\') out += "\\\\";
        else if (uc == '\n') out += "\\n";
        else if (uc == '\r') out += "\\r";
        else if (uc == '\t') out += "\\t";
        else out += TQChar(uc);
    }
    return out;
}

struct OpItem {
    TQString type;
    TQString operand;
    TQString data;
    int sensitive;
};

static inline TQString jsonGetStringValue(const TQString& s, int keyPos)
{
    // keyPos points at the first quote of the key name.
    int colon = s.find(':', keyPos);
    if (colon < 0) return TQString();
    int q1 = s.find('"', colon);
    if (q1 < 0) return TQString();
    int q2 = s.find('"', q1 + 1);
    if (q2 < 0) return TQString();
    return s.mid(q1 + 1, q2 - q1 - 1);
}

static inline int jsonGetBoolValue(const TQString& s, int keyPos)
{
    int colon = s.find(':', keyPos);
    if (colon < 0) return 0;
    int i = colon + 1;
    while (i < (int)s.length() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
    if (s.mid(i, 4) == "true") return 1;
    return 0;
}

static inline void parseOperatorsJson(const TQString& json, TQValueList<OpItem>& out)
{
    // Minimal parser for the JSON we generate/consume:
    // [{"type":"...","operand":"...","data":"...","sensitive":true}, ...]
    // Complexity: O(n)
    out.clear();
    int pos = 0;
    while (1) {
        int objStart = json.find('{', pos);
        if (objStart < 0) break;
        int objEnd = json.find('}', objStart);
        if (objEnd < 0) break;
        TQString obj = json.mid(objStart, objEnd - objStart + 1);

        OpItem it;
        it.sensitive = 0;

        int kt = obj.find("\"type\"");
        if (kt >= 0) it.type = jsonGetStringValue(obj, kt);
        int ko = obj.find("\"operand\"");
        if (ko >= 0) it.operand = jsonGetStringValue(obj, ko);
        int kd = obj.find("\"data\"");
        if (kd >= 0) it.data = jsonGetStringValue(obj, kd);
        int ks = obj.find("\"sensitive\"");
        if (ks >= 0) it.sensitive = jsonGetBoolValue(obj, ks);

        if (!it.operand.isEmpty())
            out.append(it);

        pos = objEnd + 1;
    }
}

static inline TQString encodeOperatorsAsJson(const TQValueList<OpItem>& ops)
{
    // Complexity: O(total string size)
    // Dependencies: none
    TQString out = "[";
    int first = 1;
    for (TQValueList<OpItem>::ConstIterator it = ops.begin(); it != ops.end(); ++it) {
        const OpItem& o = *it;
        if (!first) out += ",";
        first = 0;
        out += "{";
        out += "\"type\":\"" + jsonEscape(o.type) + "\",";
        out += "\"operand\":\"" + jsonEscape(o.operand) + "\",";
        out += "\"data\":\"" + jsonEscape(o.data) + "\",";
        out += "\"sensitive\":" + TQString(o.sensitive ? "true" : "false");
        out += "}";
    }
    out += "]";
    return out;
}

RuleDialog::RuleDialog(TQWidget* parent, const char* name)
    : TQDialog(parent, name)
{
    setupUi();
}

void RuleDialog::onApplyClicked()
{
    protocol::Rule baseRule;

    // Basic fields
    baseRule.set_enabled(m_enabledCheck && m_enabledCheck->isChecked());
    baseRule.set_precedence(m_precedenceCheck && m_precedenceCheck->isChecked());

    // Action
    const char* action = Config::ACTION_DENY;
    if (m_actionAllow && m_actionAllow->isChecked()) action = Config::ACTION_ALLOW;
    else if (m_actionReject && m_actionReject->isChecked()) action = Config::ACTION_REJECT;
    baseRule.set_action(action);

    // Duration
    TQString dur = (m_durationCombo ? m_durationCombo->currentText() : TQString("once"));
    dur = dur.stripWhiteSpace();
    if (dur.isEmpty()) dur = "once";
    baseRule.set_duration(dur.utf8().data());

    // Operators: match Python Debian RulesEditor behavior.
    // - Collect all selected fields into a list.
    // - If >=2 operators -> operator.type="list", operand="list", data=json.dumps(list)
    // - If 1 operator -> use it directly.
    const int sensitive = (m_caseSensitive && m_caseSensitive->isChecked()) ? 1 : 0;
    TQValueList<OpItem> opItems;

    const int procSelected = (m_appByExe && m_appByExe->isChecked()) ? 1 : 0;
    const int cmdSelected = (m_appByCmd && m_appByCmd->isChecked()) ? 1 : 0;

    if (m_appByExe && m_appByExe->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_PROCESS_PATH;
        it.data = m_appExeEdit ? m_appExeEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Select at least one field and fill its value.");
            return;
        }
        it.type = (m_appExeRegex && m_appExeRegex->isChecked()) ? Config::RULE_TYPE_REGEXP : Config::RULE_TYPE_SIMPLE;
        it.sensitive = sensitive;
        opItems.append(it);
    }
    if (m_appByCmd && m_appByCmd->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_PROCESS_COMMAND;
        it.data = m_appCmdEdit ? m_appCmdEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Select at least one field and fill its value.");
            return;
        }
        it.type = (m_appCmdRegex && m_appCmdRegex->isChecked()) ? Config::RULE_TYPE_REGEXP : Config::RULE_TYPE_SIMPLE;
        it.sensitive = sensitive;
        opItems.append(it);
    }
    if (m_appByUser && m_appByUser->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_USER_ID;
        it.data = m_appUserEdit ? m_appUserEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Select at least one field and fill its value.");
            return;
        }
        it.type = Config::RULE_TYPE_SIMPLE;
        it.sensitive = sensitive;
        opItems.append(it);
    }
    if (m_appByPid && m_appByPid->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_PROCESS_ID;
        it.data = m_appPidEdit ? m_appPidEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Select at least one field and fill its value.");
            return;
        }
        it.type = Config::RULE_TYPE_SIMPLE;
        it.sensitive = sensitive;
        opItems.append(it);
    }

    if (m_netByPort && m_netByPort->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_DEST_PORT;
        it.data = m_netPortEdit ? m_netPortEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Select at least one field and fill its value.");
            return;
        }
        it.type = Config::RULE_TYPE_SIMPLE;
        it.sensitive = sensitive;
        opItems.append(it);
    }
    if (m_netByProto && m_netByProto->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_PROTOCOL;
        it.data = m_netProtoCombo ? m_netProtoCombo->currentText().stripWhiteSpace().lower() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "protocol can not be empty, or uncheck it");
            return;
        }
        it.type = Config::RULE_TYPE_SIMPLE;
        it.sensitive = sensitive;
        opItems.append(it);

        if (looksLikeRegexp(it.data))
            opItems.last().type = Config::RULE_TYPE_REGEXP;
    }
    if (m_netByHost && m_netByHost->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_DEST_HOST;
        it.data = m_netHostEdit ? m_netHostEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Select at least one field and fill its value.");
            return;
        }
        it.type = looksLikeRegexp(it.data) ? Config::RULE_TYPE_REGEXP : Config::RULE_TYPE_SIMPLE;
        it.sensitive = sensitive;
        opItems.append(it);
    }
    if (m_netByAddr && m_netByAddr->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_DEST_IP;
        it.data = m_netAddrCombo ? m_netAddrCombo->currentText().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Select at least one field and fill its value.");
            return;
        }
        it.type = Config::RULE_TYPE_NETWORK;
        it.sensitive = sensitive;
        opItems.append(it);
    }

    // Lists (Python uses RULE_TYPE_LISTS and operands lists.*)
    if (m_listDomains && m_listDomains->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_LIST_DOMAINS;
        it.data = m_listDomainsEdit ? m_listDomainsEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Lists field cannot be empty");
            return;
        }
        it.type = Config::RULE_TYPE_LISTS;
        it.sensitive = sensitive;
        opItems.append(it);
    }
    if (m_listDomainsRegex && m_listDomainsRegex->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_LIST_DOMAINS_REGEXP;
        it.data = m_listDomainsRegexEdit ? m_listDomainsRegexEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Lists field cannot be empty");
            return;
        }
        it.type = Config::RULE_TYPE_LISTS;
        it.sensitive = sensitive;
        opItems.append(it);
    }
    if (m_listIps && m_listIps->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_LIST_IPS;
        it.data = m_listIpsEdit ? m_listIpsEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Lists field cannot be empty");
            return;
        }
        it.type = Config::RULE_TYPE_LISTS;
        it.sensitive = sensitive;
        opItems.append(it);
    }
    if (m_listRanges && m_listRanges->isChecked()) {
        OpItem it;
        it.operand = Config::OPERAND_LIST_NETS;
        it.data = m_listRangesEdit ? m_listRangesEdit->text().stripWhiteSpace() : TQString();
        if (it.data.isEmpty()) {
            TQMessageBox::warning(this, "Rule", "Lists field cannot be empty");
            return;
        }
        it.type = Config::RULE_TYPE_LISTS;
        it.sensitive = sensitive;
        opItems.append(it);
    }

    if (opItems.count() <= 0) {
        TQMessageBox::warning(this, "Rule", "Select at least one field and fill its value.");
        return;
    }

    // Validate list directories like Python RulesEditor (os.path.isdir)
    for (TQValueList<OpItem>::ConstIterator it = opItems.begin(); it != opItems.end(); ++it) {
        const OpItem& o = *it;
        if (o.type == Config::RULE_TYPE_LISTS) {
            if (!isDirPath(o.data)) {
                TQMessageBox::warning(this, "Rule", "Lists field must be a directory");
                return;
            }
        }
    }

    TQString opType;
    TQString opOperand;
    TQString opData;

    if (opItems.count() >= 2) {
        opType = Config::RULE_TYPE_LIST;
        opOperand = Config::RULE_TYPE_LIST;
        opData = encodeOperatorsAsJson(opItems);
    } else {
        const OpItem& o = *(opItems.begin());
        opType = o.type;
        opOperand = o.operand;
        opData = o.data;

        // Replicate Python Debian RulesEditor regexp heuristic:
        // if no process/cmdline criteria selected and _is_regex(data) => regexp
        if (!procSelected && !cmdSelected && looksLikeRegexp(opData))
            opType = Config::RULE_TYPE_REGEXP;
    }

    protocol::Operator* op = baseRule.mutable_operator_();
    op->set_operand(opOperand.utf8().data());
    op->set_data(opData.utf8().data());
    op->set_type(opType.utf8().data());
    op->set_sensitive((opItems.count() >= 2) ? false : (sensitive ? true : false));

    // Name: match Python Debian RulesEditor: slugify("<action> <operator.type> <operator.data>")
    TQString name = m_nameEdit ? m_nameEdit->text().stripWhiteSpace() : TQString();
    if (name.isEmpty()) {
        TQString nameHint;
        if (opType == Config::RULE_TYPE_LIST && opOperand == Config::RULE_TYPE_LIST) {
            TQValueList<OpItem> ops;
            parseOperatorsJson(opData, ops);
            for (TQValueList<OpItem>::ConstIterator it = ops.begin(); it != ops.end(); ++it) {
                const OpItem& o = *it;
                if (o.operand == Config::OPERAND_PROCESS_PATH) {
                    nameHint = basename_like_path(o.data);
                    break;
                }
            }
            if (nameHint.isEmpty() && ops.count() > 0) {
                const OpItem& o = *(ops.begin());
                nameHint = o.operand + " " + o.data;
            }
        }
        if (nameHint.isEmpty())
            nameHint = opData;
        name = slugify_like_python(TQString("%1 %2").arg(action).arg(nameHint));
        if (name.isEmpty())
            name = slugify_like_python(TQString("%1-%2").arg(action).arg((uint64_t)time(0)));
        if ((int)name.length() > 128)
            name = name.left(128);
        if (m_nameEdit)
            m_nameEdit->setText(name);
    }
    baseRule.set_name(name.utf8().data());

    // Node targets
    TQStringList targets;
    if (m_applyAllNodes && m_applyAllNodes->isChecked()) {
        const TQMap<TQString, Nodes::NodeData>& nm = Nodes::instance()->nodes();
        for (TQMap<TQString, Nodes::NodeData>::ConstIterator it = nm.begin(); it != nm.end(); ++it)
            targets << it.key();
    } else {
        TQString node = m_nodeCombo ? m_nodeCombo->currentText().stripWhiteSpace() : TQString();
        if (node.isEmpty())
            node = "unix:/local";
        targets << node;
    }

    MainWindow* mw = 0;
    TQWidget* pw = parentWidget();
    if (pw && pw->inherits("MainWindow"))
        mw = (MainWindow*)pw;
    GRpcServer* srv = mw ? mw->grpcServer() : 0;

    const TQString now = TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    for (TQStringList::ConstIterator it = targets.begin(); it != targets.end(); ++it) {
        const TQString node = (*it).stripWhiteSpace();
        if (node.isEmpty())
            continue;

        protocol::Rule rule = baseRule;

        // Save to local store/DB (one row per node, like Python applies per-node)
        Rules::instance()->add(now, node, rule);

        // Send to daemon
        if (srv) {
            protocol::Notification notif;
            notif.set_id((uint64_t)time(0));
            notif.set_type(protocol::CHANGE_RULE);
            protocol::Rule* r = notif.add_rules();
            if (r) r->CopyFrom(rule);
            srv->sendNotification(node, notif);
        }
    }
}

void RuleDialog::onAppCriteriaToggled(bool on)
{
    (void)on;
    updateApplicationsUi();
}

void RuleDialog::onHelpClicked()
{
    // Match Python: QtGui.QDesktopServices.openUrl(QUrl(Config.HELP_URL))
    Config::openUrl(Config::HELP_URL);
}

void RuleDialog::onNetCriteriaToggled(bool on)
{
    (void)on;
    updateNetworkUi();
}

void RuleDialog::onListCriteriaToggled(bool on)
{
    (void)on;
    updateListUi();
}

void RuleDialog::editRule(const protocol::Rule& rule, const TQString& node)
{
    onResetClicked();

    if (m_nameEdit)
        m_nameEdit->setText(TQString(rule.name().c_str()));
    if (m_nodeCombo)
        m_nodeCombo->setCurrentText(node);
    if (m_enabledCheck)
        m_enabledCheck->setChecked(rule.enabled());
    if (m_precedenceCheck)
        m_precedenceCheck->setChecked(rule.precedence());

    const TQString action = TQString(rule.action().c_str());
    if (m_actionAllow && action == Config::ACTION_ALLOW) m_actionAllow->setChecked(true);
    else if (m_actionReject && action == Config::ACTION_REJECT) m_actionReject->setChecked(true);
    else if (m_actionDeny) m_actionDeny->setChecked(true);

    if (m_durationCombo) {
        const TQString dur = TQString(rule.duration().c_str());
        if (!dur.isEmpty())
            m_durationCombo->setCurrentText(dur);
    }

    TQValueList<OpItem> ops;
    const TQString opType = TQString(rule.operator_().type().c_str());
    const TQString opOperand = TQString(rule.operator_().operand().c_str());
    const TQString opData = TQString(rule.operator_().data().c_str());
    const int opSensitive = rule.operator_().sensitive() ? 1 : 0;

    if (opType == Config::RULE_TYPE_LIST && opOperand == Config::RULE_TYPE_LIST) {
        parseOperatorsJson(opData, ops);
    } else {
        OpItem it;
        it.type = opType;
        it.operand = opOperand;
        it.data = opData;
        it.sensitive = opSensitive;
        ops.append(it);
    }

    for (TQValueList<OpItem>::ConstIterator it = ops.begin(); it != ops.end(); ++it) {
        const OpItem& o = *it;
        const int isRe = (o.type == Config::RULE_TYPE_REGEXP) ? 1 : 0;

        if (o.operand == Config::OPERAND_PROCESS_PATH) {
            if (m_appByExe) m_appByExe->setChecked(true);
            if (m_appExeEdit) m_appExeEdit->setText(o.data);
            if (m_appExeRegex) m_appExeRegex->setChecked(isRe ? true : false);
        } else if (o.operand == Config::OPERAND_PROCESS_COMMAND) {
            if (m_appByCmd) m_appByCmd->setChecked(true);
            if (m_appCmdEdit) m_appCmdEdit->setText(o.data);
            if (m_appCmdRegex) m_appCmdRegex->setChecked(isRe ? true : false);
        } else if (o.operand == Config::OPERAND_USER_ID) {
            if (m_appByUser) m_appByUser->setChecked(true);
            if (m_appUserEdit) m_appUserEdit->setText(o.data);
        } else if (o.operand == Config::OPERAND_PROCESS_ID) {
            if (m_appByPid) m_appByPid->setChecked(true);
            if (m_appPidEdit) m_appPidEdit->setText(o.data);
        } else if (o.operand == Config::OPERAND_DEST_PORT) {
            if (m_netByPort) m_netByPort->setChecked(true);
            if (m_netPortEdit) m_netPortEdit->setText(o.data);
        } else if (o.operand == Config::OPERAND_PROTOCOL) {
            if (m_netByProto) m_netByProto->setChecked(true);
            if (m_netProtoCombo) m_netProtoCombo->setCurrentText(o.data.upper());
        } else if (o.operand == Config::OPERAND_DEST_HOST) {
            if (m_netByHost) m_netByHost->setChecked(true);
            if (m_netHostEdit) m_netHostEdit->setText(o.data);
        } else if (o.operand == Config::OPERAND_DEST_IP || o.operand == "dest.network") {
            if (m_netByAddr) m_netByAddr->setChecked(true);
            if (m_netAddrCombo) m_netAddrCombo->setCurrentText(o.data);
        } else if (o.operand == Config::OPERAND_LIST_DOMAINS) {
            if (m_listDomains) m_listDomains->setChecked(true);
            if (m_listDomainsEdit) m_listDomainsEdit->setText(o.data);
        } else if (o.operand == Config::OPERAND_LIST_DOMAINS_REGEXP) {
            if (m_listDomainsRegex) m_listDomainsRegex->setChecked(true);
            if (m_listDomainsRegexEdit) m_listDomainsRegexEdit->setText(o.data);
        } else if (o.operand == Config::OPERAND_LIST_IPS) {
            if (m_listIps) m_listIps->setChecked(true);
            if (m_listIpsEdit) m_listIpsEdit->setText(o.data);
        } else if (o.operand == Config::OPERAND_LIST_NETS) {
            if (m_listRanges) m_listRanges->setChecked(true);
            if (m_listRangesEdit) m_listRangesEdit->setText(o.data);
        }
    }

    updateApplicationsUi();
    updateNetworkUi();
    updateListUi();

    show();
    raise();
}

void RuleDialog::updateApplicationsUi()
{
    const int exeOn = (m_appByExe && m_appByExe->isChecked());
    const int cmdOn = (m_appByCmd && m_appByCmd->isChecked());
    const int userOn = (m_appByUser && m_appByUser->isChecked());
    const int pidOn = (m_appByPid && m_appByPid->isChecked());

    if (m_appExeEdit) m_appExeEdit->setEnabled(exeOn);
    if (m_appExeRegex) m_appExeRegex->setEnabled(exeOn);

    if (m_appCmdEdit) m_appCmdEdit->setEnabled(cmdOn);
    if (m_appCmdRegex) m_appCmdRegex->setEnabled(cmdOn);

    if (m_appUserEdit) m_appUserEdit->setEnabled(userOn);
    if (m_appPidEdit) m_appPidEdit->setEnabled(pidOn);
}

void RuleDialog::updateNetworkUi()
{
    const int portOn = (m_netByPort && m_netByPort->isChecked());
    const int protoOn = (m_netByProto && m_netByProto->isChecked());
    const int hostOn = (m_netByHost && m_netByHost->isChecked());
    const int addrOn = (m_netByAddr && m_netByAddr->isChecked());

    if (m_netPortEdit) m_netPortEdit->setEnabled(portOn);
    if (m_netProtoCombo) m_netProtoCombo->setEnabled(protoOn);
    if (m_netHostEdit) m_netHostEdit->setEnabled(hostOn);
    if (m_netAddrCombo) m_netAddrCombo->setEnabled(addrOn);
}

void RuleDialog::updateListUi()
{
    const int domainsOn = (m_listDomains && m_listDomains->isChecked());
    const int domainsRegexOn = (m_listDomainsRegex && m_listDomainsRegex->isChecked());
    const int ipsOn = (m_listIps && m_listIps->isChecked());
    const int rangesOn = (m_listRanges && m_listRanges->isChecked());

    if (m_listDomainsBtn) m_listDomainsBtn->setEnabled(domainsOn);
    if (m_listDomainsEdit) m_listDomainsEdit->setEnabled(domainsOn);

    if (m_listDomainsRegexBtn) m_listDomainsRegexBtn->setEnabled(domainsRegexOn);
    if (m_listDomainsRegexEdit) m_listDomainsRegexEdit->setEnabled(domainsRegexOn);

    if (m_listIpsBtn) m_listIpsBtn->setEnabled(ipsOn);
    if (m_listIpsEdit) m_listIpsEdit->setEnabled(ipsOn);

    if (m_listRangesBtn) m_listRangesBtn->setEnabled(rangesOn);
    if (m_listRangesEdit) m_listRangesEdit->setEnabled(rangesOn);
}

static inline void setFileFromDialog(TQWidget* parent, TQLineEdit* edit)
{
    if (!edit)
        return;
    TQString filename = TQFileDialog::getOpenFileName(TQString(), "All Files (*)", parent, 0, "Open");
    filename = filename.stripWhiteSpace();
    if (filename.isEmpty())
        return;
    edit->setText(filename);
}

static inline void setDirFromDialog(TQWidget* parent, TQLineEdit* edit)
{
    if (!edit)
        return;
    TQString dir = TQFileDialog::getExistingDirectory(TQString(), parent, 0, "Select Directory", true);
    dir = dir.stripWhiteSpace();
    if (dir.isEmpty())
        return;
    edit->setText(dir);
}

void RuleDialog::onSelectDomainsFile()
{
    setDirFromDialog(this, m_listDomainsEdit);
}

void RuleDialog::onSelectDomainsRegexFile()
{
    setDirFromDialog(this, m_listDomainsRegexEdit);
}

void RuleDialog::onSelectIpsFile()
{
    setDirFromDialog(this, m_listIpsEdit);
}

void RuleDialog::onSelectRangesFile()
{
    setDirFromDialog(this, m_listRangesEdit);
}

RuleDialog::~RuleDialog()
{
}

void RuleDialog::setupUi()
{
    setCaption("Rule");
    setModal(false);
    setMinimumSize(560, 520);

    TQVBoxLayout* mainLay = new TQVBoxLayout(this, 8, 8);

    // Header row: Name
    {
        TQGridLayout* g = new TQGridLayout(3, 2, 4);
        g->addWidget(new TQLabel("Name", this), 0, 0);
        m_nameEdit = new TQLineEdit(this);
        m_nameEdit->clear();
        g->addWidget(m_nameEdit, 0, 1);

        g->addWidget(new TQLabel("Leave empty to auto-generate", this), 1, 1);

        g->addWidget(new TQLabel("Node", this), 2, 0);
        m_nodeCombo = new TQComboBox(this);
        // Populate from runtime nodes list (Python behavior)
        {
            const TQMap<TQString, Nodes::NodeData>& nm = Nodes::instance()->nodes();
            for (TQMap<TQString, Nodes::NodeData>::ConstIterator it = nm.begin(); it != nm.end(); ++it)
                m_nodeCombo->insertItem(it.key());
        }
        if (m_nodeCombo->count() == 0)
            m_nodeCombo->insertItem("unix:/local");
        g->addWidget(m_nodeCombo, 2, 1);

        mainLay->addLayout(g);
    }

    m_applyAllNodes = new TQCheckBox("Apply rule to all nodes", this);
    mainLay->addWidget(m_applyAllNodes);

    // Top checkboxes
    m_enabledCheck = new TQCheckBox("Enabled", this);
    mainLay->addWidget(m_enabledCheck);

    m_precedenceCheck = new TQCheckBox("Rule precedence", this);
    mainLay->addWidget(m_precedenceCheck);

    m_caseSensitive = new TQCheckBox("Case-sensitive", this);
    mainLay->addWidget(m_caseSensitive);
    mainLay->addSpacing(4);

    // Action/Duration frame
    {
        TQFrame* frame = new TQFrame(this);
        frame->setFrameShape(TQFrame::StyledPanel);
        frame->setFrameShadow(TQFrame::Raised);

        TQGridLayout* g = new TQGridLayout(frame, 2, 2, 12, 6);
        g->setColStretch(1, 1);
        g->setColSpacing(0, 120);
        g->setRowSpacing(0, 8);

        g->addWidget(new TQLabel("Action", frame), 0, 0);

        TQWidget* actBox = new TQWidget(frame);
        TQHBoxLayout* actLay = new TQHBoxLayout(actBox, 0, 0);
        actLay->setSpacing(10);

        m_actionGroup = new TQButtonGroup(frame);
        m_actionGroup->hide();
        m_actionGroup->setFlat(true);

        TQPixmap pmDeny   = loadThemeIcon("emblem-important", 16);
        TQPixmap pmReject = loadThemeIcon("window-close", 16);
        TQPixmap pmAllow  = loadThemeIcon("emblem-default", 16);

        TQWidget* denyBox = new TQWidget(actBox);
        TQHBoxLayout* denyLay = new TQHBoxLayout(denyBox, 0, 0);
        denyLay->setSpacing(4);
        m_actionDeny = new TQRadioButton(denyBox);
        m_actionDeny->setText(TQString());
        denyLay->addWidget(m_actionDeny);
        if (!pmDeny.isNull()) {
            TQLabel* icon = new TQLabel(denyBox);
            icon->setPixmap(pmDeny);
            denyLay->addWidget(icon);
        }
        denyLay->addWidget(new TQLabel("Deny", denyBox));

        TQWidget* rejectBox = new TQWidget(actBox);
        TQHBoxLayout* rejectLay = new TQHBoxLayout(rejectBox, 0, 0);
        rejectLay->setSpacing(4);
        m_actionReject = new TQRadioButton(rejectBox);
        m_actionReject->setText(TQString());
        rejectLay->addWidget(m_actionReject);
        if (!pmReject.isNull()) {
            TQLabel* icon = new TQLabel(rejectBox);
            icon->setPixmap(pmReject);
            rejectLay->addWidget(icon);
        }
        rejectLay->addWidget(new TQLabel("Reject", rejectBox));

        TQWidget* allowBox = new TQWidget(actBox);
        TQHBoxLayout* allowLay = new TQHBoxLayout(allowBox, 0, 0);
        allowLay->setSpacing(4);
        m_actionAllow = new TQRadioButton(allowBox);
        m_actionAllow->setText(TQString());
        allowLay->addWidget(m_actionAllow);
        if (!pmAllow.isNull()) {
            TQLabel* icon = new TQLabel(allowBox);
            icon->setPixmap(pmAllow);
            allowLay->addWidget(icon);
        }
        allowLay->addWidget(new TQLabel("Allow", allowBox));

        m_actionGroup->insert(m_actionDeny);
        m_actionGroup->insert(m_actionReject);
        m_actionGroup->insert(m_actionAllow);

        actLay->addWidget(denyBox);
        actLay->addWidget(rejectBox);
        actLay->addWidget(allowBox);
        actLay->addStretch(1);
        g->addWidget(actBox, 0, 1);

        g->addWidget(new TQLabel("Duration", frame), 1, 0);
        m_durationCombo = new TQComboBox(frame);
        m_durationCombo->setSizePolicy(TQSizePolicy(TQSizePolicy::Fixed, TQSizePolicy::Fixed));
        m_durationCombo->setMinimumWidth(220);
        m_durationCombo->insertItem("once");
        m_durationCombo->insertItem("30s");
        m_durationCombo->insertItem("5m");
        m_durationCombo->insertItem("15m");
        m_durationCombo->insertItem("30m");
        m_durationCombo->insertItem("1h");
        m_durationCombo->insertItem("12h");
        m_durationCombo->insertItem("until restart");
        m_durationCombo->insertItem("always");

        g->addWidget(m_durationCombo, 1, 1);

        mainLay->addWidget(frame);
    }

    // Tabs
    m_tabs = new TQTabWidget(this);

    // Applications tab
    {
        TQWidget* tab = new TQWidget(m_tabs);
        TQVBoxLayout* lay = new TQVBoxLayout(tab, 8, 8);

        TQGridLayout* g = new TQGridLayout(4, 3, 6);
        g->setColStretch(2, 1);

        m_appByExe = new TQCheckBox("From this executable", tab);
        g->addWidget(m_appByExe, 0, 0);
        m_appExeEdit = new TQLineEdit(tab);
        g->addWidget(m_appExeEdit, 0, 2);
        m_appExeRegex = new TQCheckBox("Is regular expression", tab);
        g->addWidget(m_appExeRegex, 1, 2);

        m_appByCmd = new TQCheckBox("From this command line", tab);
        g->addWidget(m_appByCmd, 2, 0);
        m_appCmdEdit = new TQLineEdit(tab);
        g->addWidget(m_appCmdEdit, 2, 2);
        m_appCmdRegex = new TQCheckBox("Is regular expression", tab);
        g->addWidget(m_appCmdRegex, 3, 2);

        lay->addLayout(g);

        TQGridLayout* g2 = new TQGridLayout(2, 2, 6);
        g2->setColStretch(1, 1);

        m_appByUser = new TQCheckBox("From this user (ID)", tab);
        g2->addWidget(m_appByUser, 0, 0);
        m_appUserEdit = new TQLineEdit(tab);
        g2->addWidget(m_appUserEdit, 0, 1);

        m_appByPid = new TQCheckBox("From this PID", tab);
        g2->addWidget(m_appByPid, 1, 0);
        m_appPidEdit = new TQLineEdit(tab);
        g2->addWidget(m_appPidEdit, 1, 1);

        lay->addLayout(g2);
        lay->addStretch(1);

        m_tabs->addTab(tab, "Applications");

        connect(m_appByExe, SIGNAL(toggled(bool)), this, SLOT(onAppCriteriaToggled(bool)));
        connect(m_appByCmd, SIGNAL(toggled(bool)), this, SLOT(onAppCriteriaToggled(bool)));
        connect(m_appByUser, SIGNAL(toggled(bool)), this, SLOT(onAppCriteriaToggled(bool)));
        connect(m_appByPid, SIGNAL(toggled(bool)), this, SLOT(onAppCriteriaToggled(bool)));
    }

    // Network tab
    {
        TQWidget* tab = new TQWidget(m_tabs);
        TQVBoxLayout* lay = new TQVBoxLayout(tab, 8, 8);

        TQGridLayout* g = new TQGridLayout(4, 2, 6);
        g->setColStretch(1, 1);

        m_netByPort = new TQCheckBox("To this port", tab);
        g->addWidget(m_netByPort, 0, 0);
        m_netPortEdit = new TQLineEdit(tab);
        g->addWidget(m_netPortEdit, 0, 1);

        m_netByProto = new TQCheckBox("Protocol", tab);
        g->addWidget(m_netByProto, 1, 0);
        m_netProtoCombo = new TQComboBox(tab);
        m_netProtoCombo->setEditable(true);
        m_netProtoCombo->insertItem("tcp");
        m_netProtoCombo->insertItem("udp");
        g->addWidget(m_netProtoCombo, 1, 1);

        m_netByHost = new TQCheckBox("To this host", tab);
        g->addWidget(m_netByHost, 2, 0);
        m_netHostEdit = new TQLineEdit(tab);
        m_netHostEdit->setText("www.domain.org, .*\\.domain.org");
        g->addWidget(m_netHostEdit, 2, 1);

        m_netByAddr = new TQCheckBox("To this IP / network", tab);
        g->addWidget(m_netByAddr, 3, 0);
        m_netAddrCombo = new TQComboBox(tab);
        g->addWidget(m_netAddrCombo, 3, 1);

        lay->addLayout(g);
        lay->addStretch(1);
        m_tabs->addTab(tab, "Network");

        connect(m_netByPort, SIGNAL(toggled(bool)), this, SLOT(onNetCriteriaToggled(bool)));
        connect(m_netByProto, SIGNAL(toggled(bool)), this, SLOT(onNetCriteriaToggled(bool)));
        connect(m_netByHost, SIGNAL(toggled(bool)), this, SLOT(onNetCriteriaToggled(bool)));
        connect(m_netByAddr, SIGNAL(toggled(bool)), this, SLOT(onNetCriteriaToggled(bool)));
    }

    // List of domains/IPs tab
    {
        TQWidget* tab = new TQWidget(m_tabs);
        TQVBoxLayout* lay = new TQVBoxLayout(tab, 8, 8);

        TQGridLayout* g = new TQGridLayout(4, 3, 6);
        g->setColStretch(2, 1);

        m_listDomains = new TQCheckBox("To this list of domains", tab);
        g->addWidget(m_listDomains, 0, 0);
        m_listDomainsBtn = new TQToolButton(tab);
        applyEmbeddedIcon16(m_listDomainsBtn, document_open_png, (int)document_open_png_len);
        m_listDomainsBtn->setUsesTextLabel(false);
        m_listDomainsBtn->setAutoRaise(true);
        m_listDomainsBtn->setFixedSize(22, 22);
        g->addWidget(m_listDomainsBtn, 0, 1);
        m_listDomainsEdit = new TQLineEdit(tab);
        g->addWidget(m_listDomainsEdit, 0, 2);

        m_listDomainsRegex = new TQCheckBox("To this list of domains\n(regular expressions)", tab);
        g->addWidget(m_listDomainsRegex, 1, 0);
        m_listDomainsRegexBtn = new TQToolButton(tab);
        applyEmbeddedIcon16(m_listDomainsRegexBtn, document_open_png, (int)document_open_png_len);
        m_listDomainsRegexBtn->setUsesTextLabel(false);
        m_listDomainsRegexBtn->setAutoRaise(true);
        m_listDomainsRegexBtn->setFixedSize(22, 22);
        g->addWidget(m_listDomainsRegexBtn, 1, 1);
        m_listDomainsRegexEdit = new TQLineEdit(tab);
        g->addWidget(m_listDomainsRegexEdit, 1, 2);

        m_listIps = new TQCheckBox("To this list of IPs", tab);
        g->addWidget(m_listIps, 2, 0);
        m_listIpsBtn = new TQToolButton(tab);
        applyEmbeddedIcon16(m_listIpsBtn, document_open_png, (int)document_open_png_len);
        m_listIpsBtn->setUsesTextLabel(false);
        m_listIpsBtn->setAutoRaise(true);
        m_listIpsBtn->setFixedSize(22, 22);
        g->addWidget(m_listIpsBtn, 2, 1);
        m_listIpsEdit = new TQLineEdit(tab);
        g->addWidget(m_listIpsEdit, 2, 2);

        m_listRanges = new TQCheckBox("To the list of network ranges", tab);
        g->addWidget(m_listRanges, 3, 0);
        m_listRangesBtn = new TQToolButton(tab);
        applyEmbeddedIcon16(m_listRangesBtn, document_open_png, (int)document_open_png_len);
        m_listRangesBtn->setUsesTextLabel(false);
        m_listRangesBtn->setAutoRaise(true);
        m_listRangesBtn->setFixedSize(22, 22);
        g->addWidget(m_listRangesBtn, 3, 1);
        m_listRangesEdit = new TQLineEdit(tab);
        g->addWidget(m_listRangesEdit, 3, 2);

        lay->addLayout(g);
        lay->addStretch(1);
        m_tabs->addTab(tab, "List of domains/IPs");

        connect(m_listDomains, SIGNAL(toggled(bool)), this, SLOT(onListCriteriaToggled(bool)));
        connect(m_listDomainsRegex, SIGNAL(toggled(bool)), this, SLOT(onListCriteriaToggled(bool)));
        connect(m_listIps, SIGNAL(toggled(bool)), this, SLOT(onListCriteriaToggled(bool)));
        connect(m_listRanges, SIGNAL(toggled(bool)), this, SLOT(onListCriteriaToggled(bool)));

        connect(m_listDomainsBtn, SIGNAL(clicked()), this, SLOT(onSelectDomainsFile()));
        connect(m_listDomainsRegexBtn, SIGNAL(clicked()), this, SLOT(onSelectDomainsRegexFile()));
        connect(m_listIpsBtn, SIGNAL(clicked()), this, SLOT(onSelectIpsFile()));
        connect(m_listRangesBtn, SIGNAL(clicked()), this, SLOT(onSelectRangesFile()));
    }

    mainLay->addWidget(m_tabs, 1);

    // Separator
    {
        TQFrame* sep = new TQFrame(this);
        sep->setFrameShape(TQFrame::HLine);
        sep->setFrameShadow(TQFrame::Sunken);
        mainLay->addWidget(sep);
    }

    // Bottom buttons
    {
        TQHBoxLayout* b = new TQHBoxLayout(0, 0, 6);
        b->addStretch(1);

        m_resetBtn = new TQPushButton("Reset", this);
        m_resetBtn->setMinimumSize(80, 26);
        connect(m_resetBtn, SIGNAL(clicked()), this, SLOT(onResetClicked()));
        b->addWidget(m_resetBtn);

        m_closeBtn = new TQPushButton("Close", this);
        m_closeBtn->setMinimumSize(80, 26);
        connect(m_closeBtn, SIGNAL(clicked()), this, SLOT(reject()));
        b->addWidget(m_closeBtn);

        m_applyBtn = new TQPushButton("Apply", this);
        m_applyBtn->setMinimumSize(80, 26);
        connect(m_applyBtn, SIGNAL(clicked()), this, SLOT(onApplyClicked()));
        b->addWidget(m_applyBtn);

        m_helpBtn = new TQPushButton("Help", this);
        m_helpBtn->setMinimumSize(80, 26);
        connect(m_helpBtn, SIGNAL(clicked()), this, SLOT(onHelpClicked()));
        b->addWidget(m_helpBtn);

        mainLay->addLayout(b);
    }

    // Defaults
    m_actionDeny->setChecked(true);

    updateApplicationsUi();
    updateNetworkUi();
    updateListUi();
}

void RuleDialog::onResetClicked()
{
    m_nameEdit->clear();
    m_nodeCombo->setCurrentItem(0);
    m_enabledCheck->setChecked(false);
    m_precedenceCheck->setChecked(false);
    m_actionDeny->setChecked(true);
    m_durationCombo->setCurrentItem(0);

    m_appByExe->setChecked(false);
    m_appExeEdit->clear();
    m_appExeRegex->setChecked(false);

    m_appByCmd->setChecked(false);
    m_appCmdEdit->clear();
    m_appCmdRegex->setChecked(false);

    m_appByUser->setChecked(false);
    m_appUserEdit->clear();

    m_appByPid->setChecked(false);
    m_appPidEdit->clear();

    m_netByPort->setChecked(false);
    m_netPortEdit->clear();

    m_netByProto->setChecked(false);
    m_netProtoCombo->setCurrentItem(0);

    m_netByHost->setChecked(false);
    m_netHostEdit->clear();

    m_netByAddr->setChecked(false);
    m_netAddrCombo->clearEdit();

    m_listDomains->setChecked(false);
    m_listDomainsEdit->clear();

    m_listDomainsRegex->setChecked(false);
    m_listDomainsRegexEdit->clear();

    m_listIps->setChecked(false);
    m_listIpsEdit->clear();

    m_listRanges->setChecked(false);
    m_listRangesEdit->clear();

    updateApplicationsUi();
    updateNetworkUi();
    updateListUi();
}
