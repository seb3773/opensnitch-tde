#include "preferences_dialog.h"

#include "config.h"
#include "icon_theme.h"
#include "main_window.h"
#include "nodes.h"
#include "grpc_server.h"
#include "tqtjson.h"
#include "database.h"

#include <ntqtabwidget.h>
#include <ntqcheckbox.h>
#include <ntqcombobox.h>
#include <ntqspinbox.h>
#include <ntqtoolbutton.h>
#include <ntqpushbutton.h>
#include <ntqlayout.h>
#include <ntqgroupbox.h>
#include <ntqlabel.h>
#include <ntqwidget.h>
#include <ntqlineedit.h>
#include <ntqstringlist.h>
#include <ntqfiledialog.h>

#include <ntqmessagebox.h>

#include <time.h>

static inline void set_enabled_recursive(TQWidget* w, bool en)
{
    if (!w) return;
    w->setEnabled(en);
}

static inline int tqt_idx_from_text(TQComboBox* c, const TQString& txt)
{
    if (!c)
        return 0;
    for (int i = 0; i < c->count(); ++i) {
        if (c->text(i) == txt)
            return i;
    }
    return 0;
}

static inline TQString fmtBytes(unsigned long long bytes)
{
    if (bytes >= (1024ULL * 1024ULL * 1024ULL))
        return TQString("%1 GiB").arg((double)bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    if (bytes >= (1024ULL * 1024ULL))
        return TQString("%1 MiB").arg((double)bytes / (1024.0 * 1024.0), 0, 'f', 2);
    if (bytes >= 1024ULL)
        return TQString("%1 KiB").arg((double)bytes / 1024.0, 0, 'f', 2);
    return TQString("%1 B").arg((unsigned long)bytes);
}

static inline unsigned long long queryPragmaU64(Database* db, const char* pragma)
{
    if (!db || !pragma)
        return 0;
    TQSqlQuery q = db->query(TQString("PRAGMA %1").arg(pragma));
    if (q.next())
        return (unsigned long long)q.value(0).toULongLong();
    return 0;
}

static inline TQString db_mem_usage_html()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return "<b>Current RAM usage:</b> N/A";

    unsigned long long pageCount = queryPragmaU64(db, "page_count");
    unsigned long long pageSize  = queryPragmaU64(db, "page_size");
    unsigned long long freeList  = queryPragmaU64(db, "freelist_count");
    if (pageCount == 0 || pageSize == 0)
        return "<b>Current RAM usage:</b> N/A";

    unsigned long long usedPages = (pageCount > freeList) ? (pageCount - freeList) : pageCount;
    unsigned long long usedBytes = usedPages * pageSize;
    unsigned long long totalBytes = pageCount * pageSize;
    return TQString("<b>Current RAM usage:</b> %1 / %2")
        .arg(fmtBytes(usedBytes))
        .arg(fmtBytes(totalBytes));
}

static inline void pref_load_node_config_to_ui(TQComboBox* action,
                                               TQComboBox* duration,
                                               TQComboBox* monitor,
                                               TQCheckBox* interceptUnknown,
                                               TQComboBox* logLevel,
                                               TQComboBox* nodeAddr,
                                               TQComboBox* nodeLogFile,
                                               const TQString& json)
{
    if (!action || !duration || !monitor || !interceptUnknown || !logLevel)
        return;

    TQtJsonParseError err;
    TQtJsonDocument doc = TQtJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject())
        return;
    TQtJsonObject o = doc.object();

    action->setCurrentItem(tqt_idx_from_text(action, o.value("DefaultAction").toString("deny")));
    duration->setCurrentItem(tqt_idx_from_text(duration, o.value("DefaultDuration").toString("once")));
    monitor->setCurrentItem(tqt_idx_from_text(monitor, o.value("ProcMonitorMethod").toString("proc")));
    interceptUnknown->setChecked(o.value("InterceptUnknown").toBool(false));
    logLevel->setCurrentItem((int)o.value("LogLevel").toDouble(0.0));

    if (nodeAddr && nodeLogFile) {
        TQtJsonValue sv = o.value("Server");
        if (sv.isObject()) {
            TQtJsonObject so = sv.toObject();
            nodeAddr->setEnabled(true);
            nodeLogFile->setEnabled(true);
            nodeAddr->setCurrentText(so.value("Address").toString(TQString()));
            nodeLogFile->setCurrentText(so.value("LogFile").toString(TQString()));
        } else {
            nodeAddr->setEnabled(false);
            nodeLogFile->setEnabled(false);
        }
    }
}

static inline int pref_apply_node_config(TQString* outJson,
                                        const TQString& origJson,
                                        const TQString& defaultAction,
                                        const TQString& defaultDuration,
                                        const TQString& monitorMethod,
                                        int interceptUnknown,
                                        int logLevel,
                                        const TQString& serverAddr,
                                        const TQString& serverLogFile,
                                        int allowServerEdit)
{
    if (!outJson)
        return 0;
    *outJson = TQString();

    TQtJsonParseError err;
    TQtJsonDocument doc = TQtJsonDocument::fromJson(origJson, &err);
    if (doc.isNull() || !doc.isObject())
        return 0;
    TQtJsonObject o = doc.object();

    o.insert("DefaultAction", TQtJsonValue::fromString(defaultAction));
    o.insert("DefaultDuration", TQtJsonValue::fromString(defaultDuration));
    o.insert("ProcMonitorMethod", TQtJsonValue::fromString(monitorMethod));
    o.insert("InterceptUnknown", TQtJsonValue::fromBool(interceptUnknown ? true : false));
    o.insert("LogLevel", TQtJsonValue::fromDouble((double)logLevel));

    if (allowServerEdit) {
        TQtJsonValue sv = o.value("Server");
        if (sv.isObject()) {
            TQtJsonObject so = sv.toObject();
            if (!serverAddr.isEmpty())
                so.insert("Address", TQtJsonValue::fromString(serverAddr));
            so.insert("LogFile", TQtJsonValue::fromString(serverLogFile));
            o.insert("Server", TQtJsonValue::fromVariant(so.toVariantMap()));
        }
    }

    TQtJsonDocument out = TQtJsonDocument::fromValue(TQtJsonValue::fromVariant(o.toVariantMap()));
    *outJson = out.toJson(true);
    return 1;
}

static inline int loglevel_to_index(int lvl)
{
    // Python uses logging.* constants. We keep a small compatible subset.
    // NOTSET=0, DEBUG=10, INFO=20, WARNING=30, ERROR=40
    if (lvl <= 0) return 0;
    if (lvl <= 10) return 1;
    if (lvl <= 20) return 2;
    if (lvl <= 30) return 3;
    return 4;
}

static inline int index_to_loglevel(int idx)
{
    switch (idx) {
    case 0: return 0;
    case 1: return 10;
    case 2: return 20;
    case 3: return 30;
    default: return 40;
    }
}

PreferencesDialog::PreferencesDialog(MainWindow* mainWin, TQWidget* parent, const char* name)
    : TQDialog(parent, name, true),
      m_mainWin(mainWin),
      m_tabs(0),
      m_disablePopups(0),
      m_defaultTimeout(0),
      m_defaultAction(0),
      m_defaultDuration(0),
      m_defaultTarget(0),
      m_defaultDialogPos(0),
      m_showAdvanced(0),
      m_advUid(0),
      m_advDstPort(0),
      m_advDstIP(0),
      m_ignoreRules(0),
      m_ignoreRulesDuration(0),
      m_darkMode(0),
      m_toolbarIconSize(0),
      m_uiRefreshInterval(0),
      m_serverAddr(0),
      m_grpcMaxMsgSize(0),
      m_grpcMaxWorkers(0),
      m_grpcKeepalive(0),
      m_grpcKeepaliveTimeout(0),
      m_serverLogFile(0),
      m_nodesCombo(0),
      m_applyToAllNodes(0),
      m_nodeAction(0),
      m_nodeDuration(0),
      m_nodeMonitorMethod(0),
      m_nodeInterceptUnknown(0),
      m_nodeLogLevel(0),
      m_nodeName(0),
      m_nodeVersion(0),
      m_nodeAddress(0),
      m_nodeLogFile(0),
      m_nodeNeedsUpdate(false),
      m_evtColTime(0),
      m_evtColNode(0),
      m_evtColAction(0),
      m_evtColDest(0),
      m_evtColProto(0),
      m_evtColProc(0),
      m_evtColRule(0),
      m_dbType(0),
      m_dbFile(0),
      m_dbFileBtn(0),
      m_dbWal(0),
      m_dbPurgeEnable(0),
      m_dbMaxDays(0),
      m_dbPurgeInterval(0),
      m_btnClose(0),
      m_btnApply(0),
      m_btnSave(0)
{
    setCaption("Preferences");
    setupUi();
    loadSettings();
}

void PreferencesDialog::showEvent(TQShowEvent* ev)
{
    TQDialog::showEvent(ev);
    if (m_tabs)
        onPrefsTabChanged(m_tabs->currentPage());
}

PreferencesDialog::~PreferencesDialog()
{
}

void PreferencesDialog::reject()
{
    // Discard non-applied changes: this dialog instance is reused by MainWindow.
    loadSettings();
    TQDialog::reject();
}

void PreferencesDialog::setupUi()
{
    TQVBoxLayout* mainLay = new TQVBoxLayout(this, 8, 8);

    m_tabs = new TQTabWidget(this);
    mainLay->addWidget(m_tabs, 1);
    connect(m_tabs, SIGNAL(currentChanged(TQWidget*)), this, SLOT(onPrefsTabChanged(TQWidget*)));

    // Dialogues
    {
        TQWidget* tab = new TQWidget(m_tabs);
        TQVBoxLayout* lay = new TQVBoxLayout(tab, 8, 8);

        m_disablePopups = new TQCheckBox("Disable pop-ups, only display a notification", tab);
        lay->addWidget(m_disablePopups);

        TQGroupBox* grp = new TQGroupBox("Default options", tab);
        grp->setColumnLayout(0, TQt::Vertical);
        TQGridLayout* g = new TQGridLayout(grp->layout(), 5, 2, 6);
        g->setMargin(8);

        g->addWidget(new TQLabel("Action", grp), 0, 0);
        m_defaultAction = new TQComboBox(grp);
        m_defaultAction->insertItem("Deny");
        m_defaultAction->insertItem("Allow");
        m_defaultAction->insertItem("Reject");
        g->addWidget(m_defaultAction, 0, 1);

        g->addWidget(new TQLabel("Duration", grp), 1, 0);
        m_defaultDuration = new TQComboBox(grp);
        m_defaultDuration->insertItem("once");
        m_defaultDuration->insertItem("30s");
        m_defaultDuration->insertItem("5m");
        m_defaultDuration->insertItem("15m");
        m_defaultDuration->insertItem("30m");
        m_defaultDuration->insertItem("1h");
        m_defaultDuration->insertItem("12h");
        m_defaultDuration->insertItem("until restart");
        m_defaultDuration->insertItem("always");
        g->addWidget(m_defaultDuration, 1, 1);

        g->addWidget(new TQLabel("Default target", grp), 2, 0);
        m_defaultTarget = new TQComboBox(grp);
        m_defaultTarget->insertItem("by executable");
        m_defaultTarget->insertItem("by destination");
        g->addWidget(m_defaultTarget, 2, 1);

        g->addWidget(new TQLabel("Default position on screen", grp), 3, 0);
        m_defaultDialogPos = new TQComboBox(grp);
        m_defaultDialogPos->insertItem("center");
        m_defaultDialogPos->insertItem("top-left");
        m_defaultDialogPos->insertItem("top-right");
        m_defaultDialogPos->insertItem("bottom-left");
        m_defaultDialogPos->insertItem("bottom-right");
        g->addWidget(m_defaultDialogPos, 3, 1);

        g->addWidget(new TQLabel("Default timeout", grp), 4, 0);
        m_defaultTimeout = new TQSpinBox(0, 999999, 1, grp);
        g->addWidget(m_defaultTimeout, 4, 1);

        lay->addWidget(grp);

        m_showAdvanced = new TQCheckBox("Show detailed view by default", tab);
        lay->addWidget(m_showAdvanced);

        TQGroupBox* advGrp = new TQGroupBox("Filter also connections by:", tab);
        advGrp->setColumnLayout(0, TQt::Vertical);
        TQGridLayout* ag = new TQGridLayout(advGrp->layout(), 1, 3, 6);
        ag->setMargin(8);
        m_advUid = new TQCheckBox("User ID", advGrp);
        m_advDstPort = new TQCheckBox("Destination (port)", advGrp);
        m_advDstIP = new TQCheckBox("Destination IP", advGrp);
        ag->addWidget(m_advUid, 0, 0);
        ag->addWidget(m_advDstPort, 0, 1);
        ag->addWidget(m_advDstIP, 0, 2);
        lay->addWidget(advGrp);

        lay->addStretch(1);
        m_tabs->addTab(tab, "Dialogues");
    }

    // UI
    {
        TQWidget* tab = new TQWidget(m_tabs);
        TQVBoxLayout* lay = new TQVBoxLayout(tab, 8, 8);

        TQHBoxLayout* rlay = new TQHBoxLayout(6);
        m_ignoreRules = new TQCheckBox("Do not save rules of this duration:", tab);
        rlay->addWidget(m_ignoreRules);
        m_ignoreRulesDuration = new TQComboBox(tab);
        m_ignoreRulesDuration->insertItem("all temporary rules");
        m_ignoreRulesDuration->insertItem("once");
        m_ignoreRulesDuration->insertItem("30s");
        m_ignoreRulesDuration->insertItem("5m");
        m_ignoreRulesDuration->insertItem("15m");
        m_ignoreRulesDuration->insertItem("30m");
        m_ignoreRulesDuration->insertItem("1h");
        m_ignoreRulesDuration->insertItem("12h");
        m_ignoreRulesDuration->insertItem("until restart");
        rlay->addWidget(m_ignoreRulesDuration, 1);
        lay->addLayout(rlay);

        m_darkMode = new TQCheckBox("Dark mode", tab);
        lay->addWidget(m_darkMode);

        {
            TQHBoxLayout* r = new TQHBoxLayout(6);
            r->addWidget(new TQLabel("toolbar icon size:", tab));
            m_toolbarIconSize = new TQComboBox(tab);
            m_toolbarIconSize->insertItem("small");
            m_toolbarIconSize->insertItem("normal");
            r->addWidget(m_toolbarIconSize);
            r->addStretch(1);
            lay->addLayout(r);
        }

        {
            TQHBoxLayout* r = new TQHBoxLayout(6);
            r->addWidget(new TQLabel("logs toolbar size:", tab));
            m_logsToolbarSize = new TQComboBox(tab);
            m_logsToolbarSize->insertItem("small");
            m_logsToolbarSize->insertItem("normal");
            r->addWidget(m_logsToolbarSize);
            r->addStretch(1);
            lay->addLayout(r);
        }

        {
            TQHBoxLayout* r = new TQHBoxLayout(6);
            r->addWidget(new TQLabel("Refresh interval (seconds)", tab));
            m_uiRefreshInterval = new TQSpinBox(1, 999999, 1, tab);
            r->addWidget(m_uiRefreshInterval);
            r->addStretch(1);
            lay->addLayout(r);
        }

        TQGroupBox* colsGrp = new TQGroupBox("Events columns", tab);
        colsGrp->setColumnLayout(0, TQt::Vertical);
        TQGridLayout* cg = new TQGridLayout(colsGrp->layout(), 4, 2, 6);
        cg->setMargin(8);
        m_evtColTime = new TQCheckBox("Time", colsGrp);
        m_evtColNode = new TQCheckBox("Node", colsGrp);
        m_evtColAction = new TQCheckBox("Action", colsGrp);
        m_evtColDest = new TQCheckBox("Destination", colsGrp);
        m_evtColProto = new TQCheckBox("Protocol", colsGrp);
        m_evtColProc = new TQCheckBox("Process", colsGrp);
        m_evtColRule = new TQCheckBox("Rule", colsGrp);

        cg->addWidget(m_evtColTime, 0, 0);
        cg->addWidget(m_evtColNode, 0, 1);
        cg->addWidget(m_evtColAction, 1, 0);
        cg->addWidget(m_evtColDest, 1, 1);
        cg->addWidget(m_evtColProto, 2, 0);
        cg->addWidget(m_evtColProc, 2, 1);
        cg->addWidget(m_evtColRule, 3, 0);

        lay->addWidget(colsGrp);
        lay->addStretch(1);
        m_tabs->addTab(tab, "UI");
    }

    // Nodes
    {
        TQWidget* tab = new TQWidget(m_tabs);
        TQVBoxLayout* lay = new TQVBoxLayout(tab, 8, 8);

        // Node selection
        {
            TQHBoxLayout* hl = new TQHBoxLayout(6);
            hl->addWidget(new TQLabel("Node", tab));
            m_nodesCombo = new TQComboBox(tab);
            m_nodesCombo->setEditable(false);
            hl->addWidget(m_nodesCombo, 1);
            lay->addLayout(hl);
        }

        m_applyToAllNodes = new TQCheckBox("Apply to all nodes", tab);
        lay->addWidget(m_applyToAllNodes);

        // Node info
        {
            TQGroupBox* grp = new TQGroupBox("Info", tab);
            grp->setColumnLayout(0, TQt::Vertical);
            TQGridLayout* g = new TQGridLayout(grp->layout(), 2, 2, 6);
            g->setMargin(8);
            g->setColStretch(1, 1);
            g->addWidget(new TQLabel("Name", grp), 0, 0);
            m_nodeName = new TQLabel("", grp);
            g->addWidget(m_nodeName, 0, 1);
            g->addWidget(new TQLabel("Version", grp), 1, 0);
            m_nodeVersion = new TQLabel("", grp);
            g->addWidget(m_nodeVersion, 1, 1);
            lay->addWidget(grp);
        }

        // Node config
        {
            TQGroupBox* grp = new TQGroupBox("Configuration", tab);
            grp->setColumnLayout(0, TQt::Vertical);
            TQGridLayout* g = new TQGridLayout(grp->layout(), 6, 2, 6);
            g->setMargin(8);
            g->setColStretch(1, 1);

            g->addWidget(new TQLabel("Default action", grp), 0, 0);
            m_nodeAction = new TQComboBox(grp);
            m_nodeAction->insertItem("deny");
            m_nodeAction->insertItem("allow");
            g->addWidget(m_nodeAction, 0, 1);

            g->addWidget(new TQLabel("Default duration", grp), 1, 0);
            m_nodeDuration = new TQComboBox(grp);
            m_nodeDuration->insertItem("once");
            m_nodeDuration->insertItem("until restart");
            m_nodeDuration->insertItem("always");
            g->addWidget(m_nodeDuration, 1, 1);

            g->addWidget(new TQLabel("Proc monitor method", grp), 2, 0);
            m_nodeMonitorMethod = new TQComboBox(grp);
            m_nodeMonitorMethod->insertItem("proc");
            m_nodeMonitorMethod->insertItem("ebpf");
            m_nodeMonitorMethod->insertItem("audit");
            g->addWidget(m_nodeMonitorMethod, 2, 1);

            g->addWidget(new TQLabel("Intercept unknown", grp), 3, 0);
            m_nodeInterceptUnknown = new TQCheckBox("", grp);
            g->addWidget(m_nodeInterceptUnknown, 3, 1);

            g->addWidget(new TQLabel("Log level", grp), 4, 0);
            m_nodeLogLevel = new TQComboBox(grp);
            m_nodeLogLevel->insertItem("DEBUG");
            m_nodeLogLevel->insertItem("INFO");
            m_nodeLogLevel->insertItem("IMPORTANT");
            m_nodeLogLevel->insertItem("WARNING");
            m_nodeLogLevel->insertItem("ERROR");
            m_nodeLogLevel->insertItem("FATAL");
            g->addWidget(m_nodeLogLevel, 4, 1);

            lay->addWidget(grp);
        }

        // Optional server options (only if node_config has Server)
        {
            TQGroupBox* grp = new TQGroupBox("Server", tab);
            grp->setColumnLayout(0, TQt::Vertical);
            TQGridLayout* g = new TQGridLayout(grp->layout(), 2, 2, 6);
            g->setMargin(8);
            g->setColStretch(1, 1);

            g->addWidget(new TQLabel("Address", grp), 0, 0);
            m_nodeAddress = new TQComboBox(grp);
            m_nodeAddress->setEditable(true);
            m_nodeAddress->insertItem("unix:///tmp/osui.sock");
            g->addWidget(m_nodeAddress, 0, 1);

            g->addWidget(new TQLabel("Log file", grp), 1, 0);
            m_nodeLogFile = new TQComboBox(grp);
            m_nodeLogFile->setEditable(true);
            m_nodeLogFile->insertItem("/var/log/opensnitchd.log");
            m_nodeLogFile->insertItem("/dev/stdout");
            g->addWidget(m_nodeLogFile, 1, 1);

            lay->addWidget(grp);
        }

        lay->addStretch(1);
        m_tabs->addTab(tab, "Nodes");

        connect(m_nodesCombo, SIGNAL(activated(int)), this, SLOT(onNodeComboChanged(int)));
        connect(m_applyToAllNodes, SIGNAL(clicked()), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeAction, SIGNAL(activated(int)), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeDuration, SIGNAL(activated(int)), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeMonitorMethod, SIGNAL(activated(int)), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeInterceptUnknown, SIGNAL(clicked()), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeLogLevel, SIGNAL(activated(int)), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeAddress, SIGNAL(activated(int)), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeAddress, SIGNAL(textChanged(const TQString&)), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeLogFile, SIGNAL(activated(int)), this, SLOT(onNodeNeedsUpdate()));
        connect(m_nodeLogFile, SIGNAL(textChanged(const TQString&)), this, SLOT(onNodeNeedsUpdate()));
    }

    // Database
    {
        TQWidget* tab = new TQWidget(m_tabs);
        TQVBoxLayout* lay = new TQVBoxLayout(tab, 8, 8);

        TQHBoxLayout* tl = new TQHBoxLayout(6);
        tl->addWidget(new TQLabel("Database type", tab));
        m_dbType = new TQComboBox(tab);
        m_dbType->insertItem("In memory");
        m_dbType->insertItem("File");
        tl->addWidget(m_dbType, 1);
        lay->addLayout(tl);

        {
            TQHBoxLayout* fl = new TQHBoxLayout(6);
            fl->addWidget(new TQLabel("Database file", tab));
            m_dbFile = new TQLineEdit(tab);
            fl->addWidget(m_dbFile, 1);
            m_dbFileBtn = new TQToolButton(tab);
            m_dbFileBtn->setTextLabel("Select database file");
            m_dbFileBtn->setUsesTextLabel(true);
            fl->addWidget(m_dbFileBtn);
            lay->addLayout(fl);
            connect(m_dbFileBtn, SIGNAL(clicked()), this, SLOT(onSelectDbFile()));
        }

        m_dbWal = new TQCheckBox("Use WAL journal mode", tab);
        lay->addWidget(m_dbWal);

        m_dbMemHint = new TQLabel("<i>In-memory mode stores events in RAM. Enabling periodic purging prevents unbounded memory usage while keeping a limited history.</i>", tab);
        m_dbMemHint->setTextFormat(TQt::RichText);
        lay->addWidget(m_dbMemHint);

        {
            TQHBoxLayout* mu = new TQHBoxLayout(6);
            m_dbMemUsage = new TQLabel(tab);
            m_dbMemUsage->setTextFormat(TQt::RichText);
            mu->addWidget(m_dbMemUsage, 1);

            m_dbMemUsageRefresh = new TQToolButton(tab);
            m_dbMemUsageRefresh->setTextLabel("Refresh");
            m_dbMemUsageRefresh->setUsesTextLabel(true);
            mu->addWidget(m_dbMemUsageRefresh);

            lay->addLayout(mu);
            connect(m_dbMemUsageRefresh, SIGNAL(clicked()), this, SLOT(onDbMemUsageRefreshClicked()));
        }

        m_dbPurgeEnable = new TQCheckBox("Maximum days of events to keep", tab);
        lay->addWidget(m_dbPurgeEnable);

        TQGridLayout* gl = new TQGridLayout(2, 3, 8);
        gl->addWidget(new TQLabel("Days", tab), 0, 0);
        m_dbMaxDays = new TQSpinBox(1, 999999, 1, tab);
        gl->addWidget(m_dbMaxDays, 0, 1);
        gl->addWidget(new TQLabel("days", tab), 0, 2);

        gl->addWidget(new TQLabel("Minutes between events purges", tab), 1, 0);
        m_dbPurgeInterval = new TQSpinBox(1, 999999, 1, tab);
        gl->addWidget(m_dbPurgeInterval, 1, 1);
        gl->addWidget(new TQLabel("minutes", tab), 1, 2);

        lay->addLayout(gl);
        lay->addStretch(1);
        m_tabs->addTab(tab, "Database");

        connect(m_dbType, SIGNAL(activated(int)), this, SLOT(onDbTypeChanged(int)));
        connect(m_dbPurgeEnable, SIGNAL(toggled(bool)), this, SLOT(onDbPurgeToggled(bool)));
    }

    // Server / gRPC
    {
        TQWidget* tab = new TQWidget(m_tabs);
        TQVBoxLayout* lay = new TQVBoxLayout(tab, 8, 8);

        TQLabel* info = new TQLabel("<i>Changes in this tab require restarting the UI to take effect.</i>", tab);
        info->setTextFormat(TQt::RichText);
        lay->addWidget(info);

        // Server address
        {
            TQHBoxLayout* hl = new TQHBoxLayout(6);
            hl->addWidget(new TQLabel("Server address", tab));
            m_serverAddr = new TQComboBox(tab);
            m_serverAddr->setEditable(true);
            m_serverAddr->insertItem("unix:///tmp/osui.sock");
            hl->addWidget(m_serverAddr, 1);
            lay->addLayout(hl);
        }

        // gRPC options
        {
            TQGroupBox* grp = new TQGroupBox("gRPC", tab);
            grp->setColumnLayout(0, TQt::Vertical);
            TQGridLayout* g = new TQGridLayout(grp->layout(), 5, 2, 6);
            g->setMargin(8);
            g->setColStretch(1, 1);

            g->addWidget(new TQLabel("Max message size", grp), 0, 0);
            m_grpcMaxMsgSize = new TQComboBox(grp);
            m_grpcMaxMsgSize->insertItem("4MiB");
            m_grpcMaxMsgSize->insertItem("8MiB");
            m_grpcMaxMsgSize->insertItem("16MiB");
            g->addWidget(m_grpcMaxMsgSize, 0, 1);

            g->addWidget(new TQLabel("Max workers", grp), 1, 0);
            m_grpcMaxWorkers = new TQSpinBox(1, 999999, 1, grp);
            g->addWidget(m_grpcMaxWorkers, 1, 1);

            g->addWidget(new TQLabel("Keepalive (ms)", grp), 2, 0);
            m_grpcKeepalive = new TQSpinBox(0, 999999999, 100, grp);
            g->addWidget(m_grpcKeepalive, 2, 1);

            g->addWidget(new TQLabel("Keepalive timeout (ms)", grp), 3, 0);
            m_grpcKeepaliveTimeout = new TQSpinBox(0, 999999999, 100, grp);
            g->addWidget(m_grpcKeepaliveTimeout, 3, 1);

            lay->addWidget(grp);
        }

        // Log file
        {
            TQGroupBox* grp = new TQGroupBox("Log", tab);
            grp->setColumnLayout(0, TQt::Vertical);
            TQGridLayout* g = new TQGridLayout(grp->layout(), 1, 2, 6);
            g->setMargin(8);
            g->setColStretch(1, 1);

            g->addWidget(new TQLabel("Log file", grp), 0, 0);
            m_serverLogFile = new TQLineEdit(grp);
            g->addWidget(m_serverLogFile, 0, 1);

            lay->addWidget(grp);
        }

        lay->addStretch(1);
        m_tabs->addTab(tab, "Server");
    }

    connect(m_disablePopups, SIGNAL(toggled(bool)), m_defaultTimeout, SLOT(setDisabled(bool)));
    connect(m_disablePopups, SIGNAL(toggled(bool)), m_defaultDialogPos, SLOT(setDisabled(bool)));
    connect(m_ignoreRules, SIGNAL(toggled(bool)), m_ignoreRulesDuration, SLOT(setEnabled(bool)));

    // bottom buttons
    {
        TQHBoxLayout* bl = new TQHBoxLayout(6);
        bl->addStretch(1);

        m_btnClose = new TQPushButton("Close", this);
        m_btnApply = new TQPushButton("Apply", this);
        m_btnSave = new TQPushButton("Save", this);

        connect(m_btnClose, SIGNAL(clicked()), this, SLOT(onCloseClicked()));
        connect(m_btnApply, SIGNAL(clicked()), this, SLOT(onApplyClicked()));
        connect(m_btnSave, SIGNAL(clicked()), this, SLOT(onSaveClicked()));

        bl->addWidget(m_btnClose);
        bl->addWidget(m_btnApply);
        bl->addWidget(m_btnSave);

        mainLay->addLayout(bl);
    }
}

void PreferencesDialog::loadSettings()
{
    Config* cfg = Config::get();
    if (!cfg) return;

    m_disablePopups->setChecked(cfg->getBool(Config::KEY_DISABLE_POPUPS, false));
    m_defaultTimeout->setValue(cfg->getInt(Config::KEY_DEFAULT_TIMEOUT, Config::DEFAULT_TIMEOUT));
    m_defaultAction->setCurrentItem(cfg->getInt(Config::KEY_DEFAULT_ACTION, (int)Config::ACTION_DROP_IDX));
    m_defaultDuration->setCurrentItem(cfg->getInt(Config::KEY_DEFAULT_DURATION, (int)Config::DEFAULT_DURATION_IDX));
    m_defaultTarget->setCurrentItem(cfg->getInt(Config::KEY_DEFAULT_TARGET, 0));
    m_defaultDialogPos->setCurrentItem(cfg->getInt(Config::KEY_POPUP_POSITION, 0));

    m_showAdvanced->setChecked(cfg->getBool(Config::KEY_POPUP_ADVANCED, false));
    m_advUid->setChecked(cfg->getBool(Config::KEY_POPUP_ADVANCED_UID, false));
    m_advDstPort->setChecked(cfg->getBool(Config::KEY_POPUP_ADVANCED_DSTPORT, false));
    m_advDstIP->setChecked(cfg->getBool(Config::KEY_POPUP_ADVANCED_DSTIP, false));

    m_ignoreRules->setChecked(cfg->getBool(Config::KEY_IGNORE_RULES, false));
    m_ignoreRulesDuration->setCurrentItem(cfg->getInt(Config::KEY_IGNORE_TEMPORARY_RULES, 0));
    m_ignoreRulesDuration->setEnabled(m_ignoreRules->isChecked());

    m_defaultTimeout->setEnabled(!m_disablePopups->isChecked());
    m_defaultDialogPos->setEnabled(!m_disablePopups->isChecked());

    m_darkMode->setChecked(cfg->getBool(Config::KEY_UI_DARK_MODE, false));

    if (m_toolbarIconSize)
        m_toolbarIconSize->setCurrentItem(cfg->getInt(Config::KEY_UI_TOOLBAR_ICON_SIZE, 0));

    if (m_logsToolbarSize)
        m_logsToolbarSize->setCurrentItem(cfg->getInt(Config::KEY_UI_LOGS_TOOLBAR_ICON_SIZE, 0));

    if (m_uiRefreshInterval)
        m_uiRefreshInterval->setValue(cfg->getInt(Config::KEY_STATS_REFRESH_INTERVAL, 2));

    if (m_serverAddr)
        m_serverAddr->setCurrentText(cfg->getString(Config::KEY_SERVER_ADDR, "unix:///tmp/osui.sock"));
    if (m_grpcMaxMsgSize)
        m_grpcMaxMsgSize->setCurrentText(cfg->getString(Config::KEY_SERVER_MAX_MESSAGE_LENGTH, "4MiB"));
    if (m_grpcMaxWorkers)
        m_grpcMaxWorkers->setValue(cfg->getInt(Config::KEY_SERVER_MAX_WORKERS, 20));
    if (m_grpcKeepalive)
        m_grpcKeepalive->setValue(cfg->getInt(Config::KEY_SERVER_KEEPALIVE, 5000));
    if (m_grpcKeepaliveTimeout)
        m_grpcKeepaliveTimeout->setValue(cfg->getInt(Config::KEY_SERVER_KEEPALIVE_TIMEOUT, 20000));
    if (m_serverLogFile)
        m_serverLogFile->setText(cfg->getString(Config::KEY_SERVER_LOG_FILE, TQString()));

    m_dbType->setCurrentItem(cfg->getInt(Config::KEY_DB_TYPE, (int)Config::DB_TYPE_MEMORY));
    if (m_dbFile)
        m_dbFile->setText(cfg->getString(Config::KEY_DB_FILE, ":memory:"));
    if (m_dbWal)
        m_dbWal->setChecked(cfg->getBool(Config::KEY_DB_JRNL_WAL, false));

    bool purge = cfg->getBool(Config::KEY_DB_PURGE_OLDEST, false);
    if (m_dbType->currentItem() == (int)Config::DB_TYPE_MEMORY &&
        !cfg->hasKey(Config::KEY_DB_PURGE_OLDEST)) {
        purge = true;
    }
    m_dbPurgeEnable->setChecked(purge);

    int maxDays = cfg->getInt(Config::KEY_DB_MAX_DAYS, 1);
    if (m_dbType->currentItem() == (int)Config::DB_TYPE_MEMORY &&
        !cfg->hasKey(Config::KEY_DB_MAX_DAYS)) {
        maxDays = 1;
    }
    m_dbMaxDays->setValue(maxDays);

    int purgeInterval = cfg->getInt(Config::KEY_DB_PURGE_INTERVAL, 5);
    if (m_dbType->currentItem() == (int)Config::DB_TYPE_MEMORY &&
        !cfg->hasKey(Config::KEY_DB_PURGE_INTERVAL)) {
        purgeInterval = 5;
    }
    m_dbPurgeInterval->setValue(purgeInterval);

    onDbTypeChanged(m_dbType->currentItem());
    onDbPurgeToggled(purge);

    // Events columns (matches Python: statsDialog/show_columns_connections stores hidden columns)
    m_evtColTime->setChecked(true);
    m_evtColNode->setChecked(true);
    m_evtColAction->setChecked(true);
    m_evtColDest->setChecked(true);
    m_evtColProto->setChecked(true);
    m_evtColProc->setChecked(true);
    m_evtColRule->setChecked(true);
    {
        TQVariant v = cfg->getSetting(Config::KEY_STATS_SHOW_COLUMNS_CONNECTIONS, TQVariant());
        TQStringList hidden;
        if (v.isValid()) {
            TQString s = v.toString().stripWhiteSpace();
            if (!s.isEmpty())
                hidden = TQStringList::split(",", s, false);
            else
                hidden = v.toStringList();
        }
        for (TQStringList::ConstIterator it = hidden.begin(); it != hidden.end(); ++it) {
            bool ok = false;
            int col = (*it).toInt(&ok);
            if (!ok) continue;
            if (col == 0 && m_evtColTime) m_evtColTime->setChecked(false);
            if (col == 1 && m_evtColNode) m_evtColNode->setChecked(false);
            if (col == 2 && m_evtColAction) m_evtColAction->setChecked(false);
            if (col == 3 && m_evtColDest) m_evtColDest->setChecked(false);
            if (col == 4 && m_evtColProto) m_evtColProto->setChecked(false);
            if (col == 5 && m_evtColProc) m_evtColProc->setChecked(false);
            if (col == 6 && m_evtColRule) m_evtColRule->setChecked(false);
        }
    }

    // Nodes list (match Python: populate from runtime nodes list)
    {
        if (m_nodesCombo) {
            m_nodesCombo->clear();
            const TQMap<TQString, Nodes::NodeData>& nm = Nodes::instance()->nodes();
            for (TQMap<TQString, Nodes::NodeData>::ConstIterator it = nm.begin(); it != nm.end(); ++it)
                m_nodesCombo->insertItem(it.key());
        }
        onNodeComboChanged(m_nodesCombo ? m_nodesCombo->currentItem() : 0);
        m_nodeNeedsUpdate = false;
    }
}

void PreferencesDialog::applySettings(bool doSync)
{
    Config* cfg = Config::get();
    if (!cfg) return;

    cfg->setSetting(Config::KEY_DISABLE_POPUPS, (bool)m_disablePopups->isChecked());
    cfg->setSetting(Config::KEY_DEFAULT_TIMEOUT, (int)m_defaultTimeout->value());
    cfg->setSetting(Config::KEY_DEFAULT_ACTION, (int)m_defaultAction->currentItem());
    cfg->setSetting(Config::KEY_DEFAULT_DURATION, (int)m_defaultDuration->currentItem());
    cfg->setSetting(Config::KEY_DEFAULT_TARGET, (int)m_defaultTarget->currentItem());
    cfg->setSetting(Config::KEY_POPUP_POSITION, (int)m_defaultDialogPos->currentItem());

    cfg->setSetting(Config::KEY_POPUP_ADVANCED, (bool)m_showAdvanced->isChecked());
    cfg->setSetting(Config::KEY_POPUP_ADVANCED_UID, (bool)m_advUid->isChecked());
    cfg->setSetting(Config::KEY_POPUP_ADVANCED_DSTPORT, (bool)m_advDstPort->isChecked());
    cfg->setSetting(Config::KEY_POPUP_ADVANCED_DSTIP, (bool)m_advDstIP->isChecked());

    cfg->setSetting(Config::KEY_IGNORE_RULES, (bool)m_ignoreRules->isChecked());
    cfg->setSetting(Config::KEY_IGNORE_TEMPORARY_RULES, (int)m_ignoreRulesDuration->currentItem());

    if (m_toolbarIconSize)
        cfg->setSetting(Config::KEY_UI_TOOLBAR_ICON_SIZE, (int)m_toolbarIconSize->currentItem());

    if (m_logsToolbarSize)
        cfg->setSetting(Config::KEY_UI_LOGS_TOOLBAR_ICON_SIZE, (int)m_logsToolbarSize->currentItem());

    cfg->setSetting(Config::KEY_UI_DARK_MODE, (bool)m_darkMode->isChecked());
    IconTheme::clearCache();
    if (m_mainWin)
        m_mainWin->reloadIcons();

    if (m_uiRefreshInterval)
        cfg->setSetting(Config::KEY_STATS_REFRESH_INTERVAL, (int)m_uiRefreshInterval->value());

    if (m_serverAddr)
        cfg->setSetting(Config::KEY_SERVER_ADDR, m_serverAddr->currentText());
    if (m_grpcMaxMsgSize)
        cfg->setSetting(Config::KEY_SERVER_MAX_MESSAGE_LENGTH, m_grpcMaxMsgSize->currentText().stripWhiteSpace());
    if (m_grpcMaxWorkers)
        cfg->setSetting(Config::KEY_SERVER_MAX_WORKERS, (int)m_grpcMaxWorkers->value());
    if (m_grpcKeepalive)
        cfg->setSetting(Config::KEY_SERVER_KEEPALIVE, (int)m_grpcKeepalive->value());
    if (m_grpcKeepaliveTimeout)
        cfg->setSetting(Config::KEY_SERVER_KEEPALIVE_TIMEOUT, (int)m_grpcKeepaliveTimeout->value());
    if (m_serverLogFile)
        cfg->setSetting(Config::KEY_SERVER_LOG_FILE, m_serverLogFile->text().stripWhiteSpace());

    cfg->setSetting(Config::KEY_DB_TYPE, (int)m_dbType->currentItem());
    if (m_dbFile)
        cfg->setSetting(Config::KEY_DB_FILE, m_dbFile->text().stripWhiteSpace());
    if (m_dbWal)
        cfg->setSetting(Config::KEY_DB_JRNL_WAL, (bool)m_dbWal->isChecked());
    cfg->setSetting(Config::KEY_DB_PURGE_OLDEST, (bool)m_dbPurgeEnable->isChecked());
    cfg->setSetting(Config::KEY_DB_MAX_DAYS, (int)m_dbMaxDays->value());
    cfg->setSetting(Config::KEY_DB_PURGE_INTERVAL, (int)m_dbPurgeInterval->value());

    // Events columns: store hidden columns (as strings) like Python
    {
        TQStringList hidden;
        if (m_evtColTime && !m_evtColTime->isChecked()) hidden << "0";
        if (m_evtColNode && !m_evtColNode->isChecked()) hidden << "1";
        if (m_evtColAction && !m_evtColAction->isChecked()) hidden << "2";
        if (m_evtColDest && !m_evtColDest->isChecked()) hidden << "3";
        if (m_evtColProto && !m_evtColProto->isChecked()) hidden << "4";
        if (m_evtColProc && !m_evtColProc->isChecked()) hidden << "5";
        if (m_evtColRule && !m_evtColRule->isChecked()) hidden << "6";
        cfg->setSetting(Config::KEY_STATS_SHOW_COLUMNS_CONNECTIONS, hidden.join(","));
    }

    if (doSync)
        cfg->sync();

    if (m_mainWin) {
        // Apply UI refresh interval immediately (seconds → ms)
        if (m_uiRefreshInterval)
            m_mainWin->setRefreshIntervalSeconds(m_uiRefreshInterval->value());
        // Apply columns visibility immediately (no DB access needed)
        m_mainWin->applyEventsColumnsFromConfig();
        m_mainWin->refreshEvents();
    }

    // Nodes tab: apply node configuration like Python Debian
    if (m_tabs && m_tabs->currentPage() && m_tabs->tabLabel(m_tabs->currentPage()) == "Nodes") {
        if (!m_nodesCombo)
            return;

        const bool applyAll = (m_applyToAllNodes && m_applyToAllNodes->isChecked());
        if (!m_nodeNeedsUpdate && !applyAll)
            return;

        GRpcServer* srv = m_mainWin ? m_mainWin->grpcServer() : 0;
        if (!srv)
            return;

        protocol::Notification notif;
        notif.set_id((uint64_t)time(0));
        notif.set_type(protocol::CHANGE_CONFIG);

        TQStringList targets;
        if (applyAll) {
            const TQMap<TQString, Nodes::NodeData>& nm = Nodes::instance()->nodes();
            for (TQMap<TQString, Nodes::NodeData>::ConstIterator it = nm.begin(); it != nm.end(); ++it)
                targets << it.key();
        } else {
            targets << m_nodesCombo->currentText();
        }

        for (TQStringList::ConstIterator it = targets.begin(); it != targets.end(); ++it) {
            const TQString addr = (*it).stripWhiteSpace();
            if (addr.isEmpty())
                continue;

            Nodes::NodeData* nd = Nodes::instance()->node(addr);
            if (!nd || !nd->hasConfig)
                continue;

            const TQString origJson = TQString(nd->config.config().c_str());

            TQString outJson;
            const TQString act = m_nodeAction ? m_nodeAction->currentText().stripWhiteSpace() : TQString("deny");
            const TQString dur = m_nodeDuration ? m_nodeDuration->currentText().stripWhiteSpace() : TQString("once");
            const TQString mon = m_nodeMonitorMethod ? m_nodeMonitorMethod->currentText().stripWhiteSpace() : TQString("proc");
            const int intercept = (m_nodeInterceptUnknown && m_nodeInterceptUnknown->isChecked()) ? 1 : 0;
            const int lvl = m_nodeLogLevel ? m_nodeLogLevel->currentItem() : 0;
            const TQString saddr = m_nodeAddress ? m_nodeAddress->currentText().stripWhiteSpace() : TQString();
            const TQString slog = m_nodeLogFile ? m_nodeLogFile->currentText().stripWhiteSpace() : TQString();
            const int allowServerEdit = (m_nodeAddress && m_nodeAddress->isEnabled()) ? 1 : 0;

            if (allowServerEdit && saddr.isEmpty()) {
                TQMessageBox::warning(this, "Warning", "Server address can not be empty");
                return;
            }

            if (!pref_apply_node_config(&outJson, origJson, act, dur, mon, intercept, lvl, saddr, slog, allowServerEdit))
                continue;

            notif.set_data(outJson.latin1());
            srv->sendNotification(addr, notif);
            Nodes::instance()->saveConfig(addr, outJson);
        }

        m_nodeNeedsUpdate = false;
    }
}

void PreferencesDialog::onNodeComboChanged(int)
{
    if (!m_nodesCombo)
        return;

    const TQString addr = m_nodesCombo->currentText().stripWhiteSpace();
    Nodes::NodeData* nd = Nodes::instance()->node(addr);
    if (!nd || !nd->hasConfig) {
        if (m_nodeName) m_nodeName->setText(TQString());
        if (m_nodeVersion) m_nodeVersion->setText(TQString());
        return;
    }

    if (m_nodeName) m_nodeName->setText(TQString(nd->config.name().c_str()));
    if (m_nodeVersion) m_nodeVersion->setText(TQString(nd->config.version().c_str()));

    pref_load_node_config_to_ui(m_nodeAction,
                               m_nodeDuration,
                               m_nodeMonitorMethod,
                               m_nodeInterceptUnknown,
                               m_nodeLogLevel,
                               m_nodeAddress,
                               m_nodeLogFile,
                               TQString(nd->config.config().c_str()));

    m_nodeNeedsUpdate = false;
}

void PreferencesDialog::onNodeNeedsUpdate()
{
    m_nodeNeedsUpdate = true;
}

void PreferencesDialog::onSelectDbFile()
{
    if (!m_dbFile)
        return;
    TQString filename = TQFileDialog::getSaveFileName(TQString(), "All Files (*)", this, 0, "Database file");
    filename = filename.stripWhiteSpace();
    if (filename.isEmpty())
        return;
    m_dbFile->setText(filename);
}

void PreferencesDialog::onApplyClicked()
{
    applySettings(true);
}

void PreferencesDialog::onSaveClicked()
{
    applySettings(true);
    accept();
}

void PreferencesDialog::onCloseClicked()
{
    reject();
}

void PreferencesDialog::onDbTypeChanged(int idx)
{
    bool isMem = (idx == (int)Config::DB_TYPE_MEMORY);
    bool isFile = !isMem;

    if (m_dbMemHint)
        m_dbMemHint->setShown(isMem);

    if (m_dbMemUsage)
        m_dbMemUsage->setShown(isMem);

    if (m_dbMemUsageRefresh)
        m_dbMemUsageRefresh->setShown(isMem);

    set_enabled_recursive(m_dbFile, isFile);
    set_enabled_recursive(m_dbFileBtn, isFile);
    set_enabled_recursive(m_dbWal, isFile);

    set_enabled_recursive(m_dbPurgeEnable, isMem);
    set_enabled_recursive(m_dbMaxDays, isMem && m_dbPurgeEnable->isChecked());
    set_enabled_recursive(m_dbPurgeInterval, isMem && m_dbPurgeEnable->isChecked());

    if (isMem && m_dbMemUsage)
        m_dbMemUsage->setText(db_mem_usage_html());
}

void PreferencesDialog::onPrefsTabChanged(TQWidget* w)
{
    if (!w)
        return;
    if (!m_tabs)
        return;
    if (m_tabs->currentPage() != w)
        return;
    if (!m_dbType || !m_dbMemUsage)
        return;
    if (m_tabs->indexOf(w) < 0)
        return;

    const bool isMem = (m_dbType->currentItem() == (int)Config::DB_TYPE_MEMORY);
    if (isMem && m_dbMemUsage->isShown())
        m_dbMemUsage->setText(db_mem_usage_html());
}

void PreferencesDialog::onDbMemUsageRefreshClicked()
{
    if (!m_dbType || !m_dbMemUsage)
        return;
    if (m_dbType->currentItem() != (int)Config::DB_TYPE_MEMORY)
        return;
    m_dbMemUsage->setText(db_mem_usage_html());
}

void PreferencesDialog::onDbPurgeToggled(bool enabled)
{
    bool isMem = (m_dbType->currentItem() == (int)Config::DB_TYPE_MEMORY);
    set_enabled_recursive(m_dbMaxDays, isMem && enabled);
    set_enabled_recursive(m_dbPurgeInterval, isMem && enabled);
}
