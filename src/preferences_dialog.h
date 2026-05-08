#ifndef OPENSNITCH_PREFERENCES_DIALOG_H
#define OPENSNITCH_PREFERENCES_DIALOG_H

#include <ntqdialog.h>
#include <ntqstring.h>

class TQTabWidget;
class TQCheckBox;
class TQComboBox;
class TQSpinBox;
class TQToolButton;
class TQPushButton;
class TQLineEdit;
class TQButtonGroup;
class TQRadioButton;
class TQLabel;
class TQShowEvent;

class MainWindow;

// Complexity: O(1) per setting load/save, O(n) for column checkbox list
// Dependencies: TQt3 widgets, Config
// Alignment: none required
// CPU-bound: no
class PreferencesDialog : public TQDialog
{
    TQ_OBJECT

public:
    PreferencesDialog(MainWindow* mainWin, TQWidget* parent = 0, const char* name = 0);
    ~PreferencesDialog();

    void reject();

private slots:
    void onApplyClicked();
    void onSaveClicked();
    void onCloseClicked();
    void onPrefsTabChanged(TQWidget* w);
    void onDbTypeChanged(int idx);
    void onDbPurgeToggled(bool enabled);
    void onDbMemUsageRefreshClicked();
    void onSelectDbFile();

    void onNodeComboChanged(int idx);
    void onNodeNeedsUpdate();

private:
    void setupUi();
    void loadSettings();
    void applySettings(bool doSync);

protected:
    void showEvent(TQShowEvent* ev);

    MainWindow*  m_mainWin;

    TQTabWidget* m_tabs;

    // Dialogues tab
    TQCheckBox*  m_disablePopups;
    TQSpinBox*   m_defaultTimeout;
    TQComboBox*  m_defaultAction;
    TQComboBox*  m_defaultDuration;
    TQComboBox*  m_defaultTarget;
    TQComboBox*  m_defaultDialogPos;
    TQCheckBox*  m_showAdvanced;
    TQCheckBox*  m_advUid;
    TQCheckBox*  m_advDstPort;
    TQCheckBox*  m_advDstIP;

    // UI tab
    TQCheckBox*  m_ignoreRules;
    TQComboBox*  m_ignoreRulesDuration;
    TQCheckBox*  m_darkMode;
    TQComboBox*  m_toolbarIconSize;
    TQComboBox*  m_logsToolbarSize;
    TQSpinBox*   m_uiRefreshInterval;

    // Server / gRPC options (subset of Python UI)
    TQComboBox*  m_serverAddr;
    TQComboBox*  m_grpcMaxMsgSize;
    TQSpinBox*   m_grpcMaxWorkers;
    TQSpinBox*   m_grpcKeepalive;
    TQSpinBox*   m_grpcKeepaliveTimeout;
    TQLineEdit*  m_serverLogFile;

    TQCheckBox*  m_evtColTime;
    TQCheckBox*  m_evtColNode;
    TQCheckBox*  m_evtColAction;
    TQCheckBox*  m_evtColDest;
    TQCheckBox*  m_evtColProto;
    TQCheckBox*  m_evtColProc;
    TQCheckBox*  m_evtColRule;

    // Nodes tab
    TQComboBox*  m_nodesCombo;
    TQCheckBox*  m_applyToAllNodes;
    TQComboBox*  m_nodeAction;
    TQComboBox*  m_nodeDuration;
    TQComboBox*  m_nodeMonitorMethod;
    TQCheckBox*  m_nodeInterceptUnknown;
    TQComboBox*  m_nodeLogLevel;
    TQLabel*     m_nodeName;
    TQLabel*     m_nodeVersion;
    TQComboBox*  m_nodeAddress;
    TQComboBox*  m_nodeLogFile;

    bool         m_nodeNeedsUpdate;

    // Database tab
    TQComboBox*  m_dbType;
    TQLineEdit*  m_dbFile;
    TQToolButton* m_dbFileBtn;
    TQCheckBox*  m_dbWal;
    TQLabel*     m_dbMemHint;
    TQLabel*     m_dbMemUsage;
    TQToolButton* m_dbMemUsageRefresh;
    TQCheckBox*  m_dbPurgeEnable;
    TQSpinBox*   m_dbMaxDays;
    TQSpinBox*   m_dbPurgeInterval;

    // Bottom buttons
    TQPushButton* m_btnClose;
    TQPushButton* m_btnApply;
    TQPushButton* m_btnSave;
};

#endif // OPENSNITCH_PREFERENCES_DIALOG_H
