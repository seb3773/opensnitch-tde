#ifndef RULE_DIALOG_H
#define RULE_DIALOG_H

#include <ntqdialog.h>
#include <ntqstring.h>

#include "ui.pb.h"

class TQLineEdit;
class TQComboBox;
class TQCheckBox;
class TQButtonGroup;
class TQRadioButton;
class TQTabWidget;
class TQToolButton;
class TQPushButton;

class RuleDialog : public TQDialog
{
    TQ_OBJECT

public:
    RuleDialog(TQWidget* parent = 0, const char* name = 0);
    ~RuleDialog();

    void editRule(const protocol::Rule& rule, const TQString& node);

private slots:
    void onResetClicked();
    void onApplyClicked();
    void onHelpClicked();
    void onAppCriteriaToggled(bool on);
    void onNetCriteriaToggled(bool on);
    void onListCriteriaToggled(bool on);
    void onSelectDomainsFile();
    void onSelectDomainsRegexFile();
    void onSelectIpsFile();
    void onSelectRangesFile();

private:
    void setupUi();
    void updateApplicationsUi();
    void updateNetworkUi();
    void updateListUi();

private:
    // Header
    TQLineEdit* m_nameEdit;
    TQComboBox* m_nodeCombo;
    TQCheckBox* m_applyAllNodes;
    TQCheckBox* m_enabledCheck;
    TQCheckBox* m_precedenceCheck;

    // Action / Duration
    TQButtonGroup* m_actionGroup;
    TQRadioButton* m_actionDeny;
    TQRadioButton* m_actionReject;
    TQRadioButton* m_actionAllow;
    TQComboBox* m_durationCombo;

    // Tabs
    TQTabWidget* m_tabs;

    // Applications tab
    TQCheckBox* m_appByExe;
    TQLineEdit* m_appExeEdit;
    TQCheckBox* m_appExeRegex;

    TQCheckBox* m_appByCmd;
    TQLineEdit* m_appCmdEdit;
    TQCheckBox* m_appCmdRegex;

    TQCheckBox* m_appByUser;
    TQLineEdit* m_appUserEdit;

    TQCheckBox* m_appByPid;
    TQLineEdit* m_appPidEdit;

    // Network tab
    TQCheckBox* m_netByPort;
    TQLineEdit* m_netPortEdit;

    TQCheckBox* m_netByProto;
    TQComboBox* m_netProtoCombo;

    TQCheckBox* m_netByHost;
    TQLineEdit* m_netHostEdit;

    TQCheckBox* m_netByAddr;
    TQComboBox* m_netAddrCombo;

    // Domains/IPs tab
    TQCheckBox* m_listDomains;
    TQToolButton* m_listDomainsBtn;
    TQLineEdit* m_listDomainsEdit;

    TQCheckBox* m_listDomainsRegex;
    TQToolButton* m_listDomainsRegexBtn;
    TQLineEdit* m_listDomainsRegexEdit;

    TQCheckBox* m_listIps;
    TQToolButton* m_listIpsBtn;
    TQLineEdit* m_listIpsEdit;

    TQCheckBox* m_listRanges;
    TQToolButton* m_listRangesBtn;
    TQLineEdit* m_listRangesEdit;

    // More tab
    TQCheckBox* m_caseSensitive;

    // Buttons
    TQPushButton* m_resetBtn;
    TQPushButton* m_closeBtn;
    TQPushButton* m_applyBtn;
    TQPushButton* m_helpBtn;
};

#endif // RULE_DIALOG_H
