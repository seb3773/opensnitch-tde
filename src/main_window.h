#ifndef OPENSNITCH_MAIN_WINDOW_H
#define OPENSNITCH_MAIN_WINDOW_H

#include <ntqwidget.h>
#include <ntqtabwidget.h>
#include <ntqtable.h>
#include <ntqpushbutton.h>
#include <ntqlabel.h>
#include <ntqlayout.h>
#include <ntqcombobox.h>
#include <ntqlineedit.h>
#include <ntqtimer.h>
#include <ntqframe.h>
#include <ntqtoolbutton.h>
#include <ntqlistview.h>

#include "config.h"
#include "nodes.h"
#include "rules.h"
#include "database.h"
#include "grpc_server.h"
#include "tqtmvctableview.h"
#include "tqtliststore.h"

// Complexity: O(n) for table updates, O(1) for single row ops
// Dependencies: TQt3 widgets, Database, Nodes, Rules, KIconLoader
// Alignment: none required
// CPU-bound: no, mostly I/O for DB queries

class RuleDialog;
class TQCustomEvent;

class MainWindow : public TQWidget {
    TQ_OBJECT

public:
    MainWindow(TQWidget* parent = 0, const char* name = 0);
    ~MainWindow();

    void setGrpcServer(GRpcServer* server) { m_server = server; }
    GRpcServer* grpcServer() const { return m_server; }

    void refreshEvents();
    void refreshRules();
    void refreshNodes();
    void refreshHosts();
    void refreshHostConnections(const TQString& host);
    void refreshProcs();
    void refreshAddrs();
    void refreshAddrConnections(const TQString& addr);
    void updateAddrNetworkNameRow(const TQString& ip, const TQString& hostname, const TQString& provider);
    void refreshPorts();
    void refreshPortConnections(const TQString& port);
    void refreshUsers();
    void refreshUserConnections(const TQString& user);

    void setRefreshIntervalSeconds(int seconds);

    void applyEventsColumnsFromConfig();

    void setDaemonConnected(bool connected);
    void setInterceptionEnabled(bool enabled);
    void addEvent(const TQString& time, const TQString& node,
                  const TQString& action, const TQString& procPath,
                  const TQString& dstHost, const TQString& dstPort,
                  const TQString& proto);

    void updateStats(int connections, int dropped, const TQString& uptime, int rules);

signals:
    void firewallRequested();
    void preferencesRequested();
    void interceptionToggled(bool enable);
    void iconsThemeChanged();

public slots:
    void reloadIcons();

protected:
    void customEvent(TQCustomEvent* ev);

private slots:
    void onTabChanged(TQWidget* w);
    void onRefreshTimer();
    void onDeleteRuleClicked();
    void onToggleRuleClicked();
    void onRulesUpdated();
    void onRulesTreeSelectionChanged(TQListViewItem* it);
    void onNodesUpdated(int total);
    void onNodeRowDoubleClicked(int row, int col, int button, const TQPoint& p);
    void onNodeDetailBackClicked();
    void onRuleRowDoubleClicked(int modelRow, int col);
    void onRuleRowDoubleClicked4(int row, int col, int button, const TQPoint& p);
    void onRuleDetailBackClicked();
    void onRuleDetailEditClicked();
    void onRuleDetailDeleteClicked();
    void onRulesContextMenu(int modelRow, int column, const TQPoint& globalPos);
    void onHostRowDoubleClicked(int modelRow, int col);
    void onHostRowDoubleClicked4(int row, int col, int button, const TQPoint& p);
    void onHostDetailBackClicked();

    void onProcRowDoubleClicked(int modelRow, int col);
    void onProcRowDoubleClicked4(int row, int col, int button, const TQPoint& p);
    void onProcDetailBackClicked();
    void onProcDetailsClicked();
    void onProcConnSelectionChanged(int modelRow);
    void onProcConnClicked4(int row, int col, int button, const TQPoint& p);

    void onAddrRowDoubleClicked(int modelRow, int col);
    void onAddrRowDoubleClicked4(int row, int col, int button, const TQPoint& p);
    void onAddrDetailBackClicked();

    void onPortRowDoubleClicked(int modelRow, int col);
    void onPortRowDoubleClicked4(int row, int col, int button, const TQPoint& p);
    void onPortDetailBackClicked();

    void onUserRowDoubleClicked(int modelRow, int col);
    void onUserRowDoubleClicked4(int row, int col, int button, const TQPoint& p);
    void onUserDetailBackClicked();

    void onEventRowDoubleClicked(int modelRow, int col);
    void onEventRowDoubleClicked4(int row, int col, int button, const TQPoint& p);

    void onNotificationReply(const TQString& peer, unsigned long long id, int code, const TQString& data);
    void onInterceptionToggled();
    void onPreferencesClicked();
    void onSaveClicked();
    void onNewClicked();
    void onFilterTextChanged(const TQString& text);
    void onActionComboChanged(int idx);
    void onLimitComboChanged(int idx);
    void onCleanStatsClicked();
    void onFreezeLogsToggled();
    void onHelpClicked();
    void onStatsUpdated(bool needRefresh, int connections, int dropped, int rules, const TQString& uptime);
    void onDaemonSubscribed(const TQString& peer, bool firewallRunning);
    void onTrayToggle();
    void onQuitRequested();
    void onAboutClicked();
    void onQuitClicked();

private:
    void setupUi();
    void setupToolBar();
    void setupTabWidget();
    void setupEventsTab();
    void setupRulesTab();
    void setupHostsTab();
    void setupProcsTab();
    void setupAddrsTab();
    void setupPortsTab();
    void setupUsersTab();
    void setupNodesTab();
    void setupGenericTab(TQWidget** tab, TQTable** table, int cols,
                         const char** headers, const int* widths);
    void setupFilterBar();
    void setupStatusBar();
    void fullRefreshEvents();
    void applyEventsFilter();

    void setNodesDetailView(const TQString& node, int on);
    void refreshNodeConnections(const TQString& node);

    void ensureInternalDnsRule(const TQString& node);
    void removeInternalDnsRule();

    void setRulesDetailView(const TQString& node, const TQString& ruleName, int on);
    void refreshRuleConnections(const TQString& ruleName, const TQString& node);
    void updateRulesTreeNodes();

    void refreshProcConnections(const TQString& procPath);

    // Main layout
    TQVBoxLayout* m_mainLay;

    // Toolbar
    TQFrame*      m_toolBar;
    TQToolButton* m_saveBtn;
    TQToolButton* m_refreshBtn;
    TQToolButton* m_aboutBtn;
    TQToolButton* m_quitBtn;
    TQToolButton* m_newBtn;
    TQLabel*      m_stateLabel;
    TQLabel*      m_stateValueLabel;
    TQToolButton* m_pauseBtn;

    // Tab widget
    TQTabWidget*  m_tabs;
    TQWidget*     m_eventsTab;
    TQWidget*     m_rulesTab;
    TQWidget*     m_nodesTab;
    TQWidget*     m_hostsTab;
    TQWidget*     m_procsTab;
    TQWidget*     m_addrsTab;
    TQWidget*     m_portsTab;
    TQWidget*     m_usersTab;

    // Events tab (MVC)
    TQtMvcTableView* m_eventsTable;
    TQtListStore*    m_eventsModel;

    // Rules tab
    TQtMvcTableView* m_rulesTable;
    TQtListStore*    m_rulesModel;
    TQListView*      m_rulesTree;
    TQPushButton* m_delRuleBtn;
    TQPushButton* m_toggleRuleBtn;
    TQPushButton* m_newRuleBtn;
    TQComboBox*   m_rulesFilterCombo;

    // Rules detail view (matches Python: double-click opens grouped connections list)
    TQWidget*        m_rulesDetailBar;
    TQPushButton*    m_rulesBackBtn;
    TQPushButton*    m_rulesEditBtn;
    TQPushButton*    m_rulesDeleteBtn;
    TQLabel*         m_rulesDetailLabel;
    TQtMvcTableView* m_rulesConnView;
    TQtListStore*    m_rulesConnModel;
    int              m_rulesInDetail;
    TQString         m_rulesDetailNode;
    TQString         m_rulesDetailName;

    // Nodes tab
    TQtMvcTableView* m_nodesTable;
    TQtListStore*    m_nodesModel;
    TQWidget*        m_nodesDetailBar;
    TQPushButton*    m_nodesBackBtn;
    TQLabel*         m_nodesDetailLabel;
    TQtMvcTableView* m_nodesConnView;
    TQtListStore*    m_nodesConnModel;
    int              m_nodesInDetail;
    TQString         m_nodesDetailNode;

    // Generic tabs
    // Hosts tab (MVC)
    TQtMvcTableView* m_hostsTable;
    TQtListStore*    m_hostsModel;
    TQWidget*        m_hostsDetailBar;
    TQPushButton*    m_hostsBackBtn;
    TQLabel*         m_hostsDetailLabel;
    TQtMvcTableView* m_hostsConnView;
    TQtListStore*    m_hostsConnModel;
    int              m_hostsInDetail;
    TQString         m_hostsDetailWhat;

    // Applications tab (MVC)
    TQtMvcTableView* m_procsTable;
    TQtListStore*    m_procsModel;
    TQWidget*        m_procsDetailBar;
    TQPushButton*    m_procsBackBtn;
    TQPushButton*    m_procsDetailsBtn;
    TQLabel*         m_procsDetailLabel;
    TQtMvcTableView* m_procsConnView;
    TQtListStore*    m_procsConnModel;
    int              m_procsInDetail;
    TQString         m_procsDetailWhat;

    // Addresses tab (MVC)
    TQtMvcTableView* m_addrsTable;
    TQtListStore*    m_addrsModel;
    TQWidget*        m_addrsDetailBar;
    TQPushButton*    m_addrsBackBtn;
    TQLabel*         m_addrsDetailLabel;
    TQtMvcTableView* m_addrsConnView;
    TQtListStore*    m_addrsConnModel;
    int              m_addrsInDetail;
    TQString         m_addrsDetailWhat;

    // Ports tab (MVC)
    TQtMvcTableView* m_portsTable;
    TQtListStore*    m_portsModel;
    TQWidget*        m_portsDetailBar;
    TQPushButton*    m_portsBackBtn;
    TQLabel*         m_portsDetailLabel;
    TQtMvcTableView* m_portsConnView;
    TQtListStore*    m_portsConnModel;
    int              m_portsInDetail;
    TQString         m_portsDetailWhat;

    // Users tab (MVC)
    TQtMvcTableView* m_usersTableMvc;
    TQtListStore*    m_usersModel;
    TQWidget*        m_usersDetailBar;
    TQPushButton*    m_usersBackBtn;
    TQLabel*         m_usersDetailLabel;
    TQtMvcTableView* m_usersConnView;
    TQtListStore*    m_usersConnModel;
    int              m_usersInDetail;
    TQString         m_usersDetailWhat;

    class ProcessDetailsDialog* m_procDetailsDlg;

    // Other generic tabs (legacy)
    TQTable*      m_usersTable;

    // Filter bar
    TQFrame*      m_filterBar;
    TQComboBox*   m_actionCombo;
    TQLineEdit*   m_filterEdit;
    TQToolButton* m_clearFilterBtn;
    TQComboBox*   m_limitCombo;
    TQToolButton* m_clearStatsBtn;
    TQToolButton* m_freezeLogsBtn;
    TQToolButton* m_helpBtn;

    bool          m_logsFrozen;
    int           m_eventsViewportPalSaved;
    TQPalette     m_eventsViewportPal;

    // Status bar
    TQLabel*      m_statsLabel;

    int           m_internalDnsRuleCreated;
    TQString      m_internalDnsRuleNode;
    TQLabel*      m_versionLabel;

    // Refresh timer
    TQTimer*      m_refreshTimer;

    // gRPC server for sending notifications
    GRpcServer*   m_server;

    bool          m_daemonConnected;
    bool          m_interceptionEnabled;
    int           m_connectionsCount;
    int           m_droppedCount;
    int           m_rulesCount;
    TQString       m_uptimeStr;

    TQDialog*      m_prefsDlg;
    RuleDialog*    m_ruleDlg;
};

#endif // OPENSNITCH_MAIN_WINDOW_H
