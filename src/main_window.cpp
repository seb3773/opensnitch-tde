#include "main_window.h"

#include <ntqapplication.h>
#include <ntqheader.h>
#include <ntqmessagebox.h>
#include <ntqcursor.h>
#include <ntqhbox.h>
#include <ntqdatetime.h>
#include <ntqlistview.h>
#include <ntqmenubar.h>
#include <ntqpopupmenu.h>
#include <ntqtoolbutton.h>
#include <ntqlabel.h>
#include <ntqhbox.h>
#include <ntqfiledialog.h>
#include <ntqfile.h>
#include <ntqtextstream.h>
#include <ntqstringlist.h>
#include <kiconloader.h>
#include <ntqframe.h>
#include <ntqpalette.h>
#include <ntqtable.h>
#include <ntqsqlquery.h>
#include <tdeglobal.h>
#include <ntqwhatsthis.h>
#include <ntqimage.h>
#include <ntqiconset.h>
 #include <ntqfontmetrics.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
 #include <pwd.h>
 #include <sys/types.h>
 #include <stdlib.h>

#include <ntqsplitter.h>

#include "embedded_icons.h"
#include "icon_theme.h"
#include "config.h"
#include "database.h"
#include "preferences_dialog.h"
#include "rule_dialog.h"
#include "about_dialog.h"
#include "process_details_dialog.h"
#include "prompt_dialog.h"
#include "net_identity_resolver.h"

static inline void deleteTableItems(TQTable* t)
{
    if (!t)
        return;
    int rows = t->numRows();
    int cols = t->numCols();
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            TQTableItem* it = t->item(r, c);
            if (!it)
                continue;
            t->setItem(r, c, 0);
            delete it;
        }
    }
}

static inline TQString formatUserUidDisplay(const TQString& uidTextIn)
{
    TQString uidText = uidTextIn.stripWhiteSpace();
    if (uidText.isEmpty())
        return uidText;

    int p1 = uidText.find('(');
    int p2 = uidText.find(')');
    if (p1 >= 0 && p2 > p1)
        return uidText;

    bool ok = false;
    unsigned long uidv = uidText.toULong(&ok);
    if (!ok)
        return uidText;

    static TQMap<unsigned long, TQString> s_cache;
    {
        TQMap<unsigned long, TQString>::ConstIterator it = s_cache.find(uidv);
        if (it != s_cache.end()) {
            if ((*it).isEmpty())
                return uidText;
            return *it;
        }
    }

    struct passwd pwd;
    struct passwd* result = 0;

    long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize < 1024)
        bufsize = 16384;

    char* buf = (char*)malloc((size_t)bufsize);
    if (!buf)
        return uidText;

    int rc = getpwuid_r((uid_t)uidv, &pwd, buf, (size_t)bufsize, &result);
    if (rc != 0 || !result || !pwd.pw_name || !pwd.pw_name[0]) {
        free(buf);
        s_cache.insert(uidv, TQString());
        return uidText;
    }

    TQString out = TQString::fromLocal8Bit(pwd.pw_name) + " (" + uidText + ")";
    free(buf);
    s_cache.insert(uidv, out);
    return out;
}

static inline int findExactRow(TQtListStore* model, int column, const TQString& value)
{
    if (!model)
        return -1;
    const TQString v = value.stripWhiteSpace();
    if (v.isEmpty())
        return -1;
    const int rows = model->rowCount();
    for (int i = 0; i < rows; ++i) {
        if (model->data(i, column).toString().stripWhiteSpace() == v)
            return i;
    }
    return -1;
}

static inline TQString jsonGetStringValue(const TQString& s, int keyPos)
{
    // Complexity: O(n)
    // Dependencies: none
    int colon = s.find(':', keyPos);
    if (colon < 0) return TQString();
    int q1 = s.find('"', colon + 1);
    if (q1 < 0) return TQString();
    int q2 = s.find('"', q1 + 1);
    if (q2 < 0) return TQString();
    return s.mid(q1 + 1, q2 - q1 - 1);
}

static inline void parseOperatorListJson(const TQString& json, TQStringList& out)
{
    // Complexity: O(n)
    // Dependencies: none
    out.clear();
    int pos = 0;
    while (1) {
        int objStart = json.find('{', pos);
        if (objStart < 0) break;
        int objEnd = json.find('}', objStart);
        if (objEnd < 0) break;
        TQString obj = json.mid(objStart, objEnd - objStart + 1);

        int ko = obj.find("\"operand\"");
        int kd = obj.find("\"data\"");
        if (ko >= 0) {
            TQString operand = jsonGetStringValue(obj, ko);
            if (!operand.isEmpty()) {
                TQString data;
                if (kd >= 0)
                    data = jsonGetStringValue(obj, kd);
                out << (data.isEmpty() ? operand : (operand + "=" + data));
            }
        }

        pos = objEnd + 1;
    }
}

static inline TQString pretty_operator_data(const TQString& opType, const TQString& opOperand, const TQString& opData)
{
    // Complexity: O(n)
    // Dependencies: none
    if (opType != "list" || opOperand != "list")
        return opData;

    TQStringList items;
    parseOperatorListJson(opData, items);
    if (items.isEmpty())
        return opData;
    return items.join("; ");
}



void MainWindow::updateAddrNetworkNameRow(const TQString& ip, const TQString& hostname, const TQString& provider)
{
    if (!m_addrsModel)
        return;
    if (ip.isEmpty())
        return;

    const TQString text = !provider.isEmpty() ? provider : hostname;

    const int rows = m_addrsModel->rowCount();
    for (int i = 0; i < rows; ++i) {
        const TQString rowIp = m_addrsModel->data(i, 0).toString().stripWhiteSpace();
        if (rowIp != ip)
            continue;
        m_addrsModel->setData(i, 2, text);
    }
}

void MainWindow::customEvent(TQCustomEvent* ev)
{
    if (!ev)
        return;

    if (ev->type() == NetIdentityResolvedEventId) {
        NetIdentityResolvedEvent* ne = static_cast<NetIdentityResolvedEvent*>(ev);
        updateAddrNetworkNameRow(ne->ip(), ne->hostname(), ne->provider());
        return;
    }

    TQWidget::customEvent(ev);
}

static inline int toolbarIconPx()
{
    Config* cfg = Config::get();
    if (!cfg)
        return 22;

    const int v = cfg->getInt(Config::KEY_UI_TOOLBAR_ICON_SIZE, 0);
    return (v == 1) ? 32 : 22;
}

static inline int logsToolbarIconPx()
{
    Config* cfg = Config::get();
    if (!cfg)
        return 22;

    const int v = cfg->getInt(Config::KEY_UI_LOGS_TOOLBAR_ICON_SIZE, 0);
    return (v == 1) ? 32 : 22;
}

static inline void applyEmbeddedIconPx(TQToolButton* b, const unsigned char* data, int len, int px)
{
    if (!b || !data || !len)
        return;
    TQPixmap pm = IconTheme::loadEmbeddedPixmap(data, len, px);
    if (pm.isNull())
        return;
    b->setIconSet(TQIconSet(pm, TQIconSet::Small));
    b->setPixmap(pm);
}

// Load embedded PNG data into a TQToolButton (matches tde-extended pattern)
static inline void applyEmbeddedIcon(TQToolButton* b, const unsigned char* data, int len) {
    applyEmbeddedIconPx(b, data, len, toolbarIconPx());
}

static inline void applyLogsToolbarSizing(TQFrame* bar, int px)
{
    if (!bar)
        return;
    if (px <= 0)
        px = 22;

    static TQMap<const TQFrame*, TQFont> s_baseFont;
    if (!s_baseFont.contains(bar))
        s_baseFont.insert(bar, bar->font());

    const TQFont base = s_baseFont[bar];

    // px=22 => small (default), px=32 => normal.
    TQFont f = base;
    if (px <= 22) {
        int s = base.pointSize();
        if (s > 0)
            f.setPointSize(s - 1);
    }
    bar->setFont(f);

    const int h = TQFontMetrics(f).height() + 10;
    bar->setMinimumHeight(h);
}

// Custom TQTableItem to display colored text in Action column
class ColoredTableItem : public TQTableItem {
public:
    ColoredTableItem(TQTable* table, EditType et, const TQString& text,
                     const TQColor& fg)
        : TQTableItem(table, et, text), m_fg(fg) {}

    void paint(TQPainter* p, const TQColorGroup& cg,
               const TQRect& cr, bool selected)
    {
        TQColorGroup mcg = cg;
        mcg.setColor(TQColorGroup::Text, m_fg);
        TQTableItem::paint(p, mcg, cr, selected);
    }

private:
    TQColor m_fg;
};

enum {
    RULEDET_COL_TIME = 0,
    RULEDET_COL_NODE,
    RULEDET_COL_HITS,
    RULEDET_COL_UID,
    RULEDET_COL_PROTOCOL,
    RULEDET_COL_DST_PORT,
    RULEDET_COL_DESTINATION,
    RULEDET_COL_PROCESS,
    RULEDET_COL_PROC_ARGS,
    RULEDET_COL_CWD,
    RULEDET_COL_COUNT
};

enum {
    EVT_COL_TIME = 0, EVT_COL_NODE, EVT_COL_ACTION,
    EVT_COL_DEST, EVT_COL_PROTO, EVT_COL_PROCESS, EVT_COL_RULE,
    EVT_COL_COUNT
};

enum {
    RULE_COL_NAME = 0, RULE_COL_ACTION, RULE_COL_DURATION,
    RULE_COL_ENABLED, RULE_COL_OP_TYPE, RULE_COL_OP_OPERAND,
    RULE_COL_OP_DATA, RULE_COL_NODE, RULE_COL_COUNT
};

enum {
    RULEMVC_COL_TIME = 0,
    RULEMVC_COL_NODE,
    RULEMVC_COL_NAME,
    RULEMVC_COL_ACTIVE,
    RULEMVC_COL_PRECEDENCE,
    RULEMVC_COL_ACTION,
    RULEMVC_COL_DURATION,
    RULEMVC_COL_OP_TYPE,
    RULEMVC_COL_OP_SENSITIVE,
    RULEMVC_COL_OP_OPERAND,
    RULEMVC_COL_OP_DATA,
    RULEMVC_COL_COUNT
};

enum {
    NODE_COL_LAST_CONN = 0, NODE_COL_ADDR, NODE_COL_STATUS,
    NODE_COL_HOSTNAME, NODE_COL_VERSION, NODE_COL_UPTIME,
    NODE_COL_RULES, NODE_COL_CONNECTIONS, NODE_COL_DROPPED,
    NODE_COL_KERNEL, NODE_COL_COUNT
};

enum {
    TAB_EVENTS = 0, TAB_NODES, TAB_RULES, TAB_HOSTS,
    TAB_PROCS, TAB_ADDRS, TAB_PORTS, TAB_USERS, TAB_COUNT
};

static TQPixmap loadIcon(const char* name, int size = 22)
{
    return TDEGlobal::iconLoader()->loadIcon(name, TDEIcon::Desktop, size,
                                              TDEIcon::DefaultState, 0, true);
}

static inline TQPixmap loadEmbeddedIcon(const unsigned char* data, int len, int size = 22)
{
    return IconTheme::loadEmbeddedPixmap(data, len, size);
}

MainWindow::MainWindow(TQWidget* parent, const char* name)
    : TQWidget(parent, name),
      m_uptimeStr(""),
      m_prefsDlg(0),
      m_ruleDlg(0),
      m_procsDetailsBtn(0),
      m_procDetailsDlg(0),
      m_addrsTable(0),
      m_addrsModel(0),
      m_addrsDetailBar(0),
      m_addrsBackBtn(0),
      m_addrsDetailLabel(0),
      m_addrsConnView(0),
      m_addrsConnModel(0),
      m_addrsInDetail(0),
      m_portsTable(0),
      m_portsModel(0),
      m_portsDetailBar(0),
      m_portsBackBtn(0),
      m_portsDetailLabel(0),
      m_portsConnView(0),
      m_portsConnModel(0),
      m_portsInDetail(0),
      m_usersTableMvc(0),
      m_usersModel(0),
      m_usersDetailBar(0),
      m_usersBackBtn(0),
      m_usersDetailLabel(0),
      m_usersConnView(0),
      m_usersConnModel(0),
      m_usersInDetail(0),
      m_daemonConnected(false),
      m_interceptionEnabled(true),
      m_connectionsCount(0),
      m_droppedCount(0),
      m_rulesCount(0),
      m_filterBar(0),
      m_actionCombo(0),
      m_filterEdit(0),
      m_clearFilterBtn(0),
      m_limitCombo(0),
      m_clearStatsBtn(0),
      m_freezeLogsBtn(0),
      m_helpBtn(0),
      m_logsFrozen(false),
      m_eventsViewportPalSaved(0),
      m_internalDnsRuleCreated(0)
{
    setupUi();

    m_refreshTimer = new TQTimer(this);
    connect(m_refreshTimer, SIGNAL(timeout()), this, SLOT(onRefreshTimer()));
    {
        Config* cfg = Config::get();
        int sec = 2;
        if (cfg)
            sec = cfg->getInt(Config::KEY_STATS_REFRESH_INTERVAL, 2);
        setRefreshIntervalSeconds(sec);
    }

    Nodes* nodes = Nodes::instance();
    Rules* rules = Rules::instance();
    connect(nodes, SIGNAL(nodesUpdated(int)), this, SLOT(onNodesUpdated(int)));
    connect(rules, SIGNAL(updated()), this, SLOT(onRulesUpdated()));
}

MainWindow::~MainWindow()
{
    m_refreshTimer->stop();
}

void MainWindow::onQuitRequested()
{
    removeInternalDnsRule();

    // Stop background gRPC threads before leaving the event loop.
    GRpcServer* srv = grpcServer();
    if (srv)
        srv->stop();

    // Prevent processing of queued GUI-thread custom events after shutdown.
    TQApplication::removePostedEvents(this);

    if (tqApp)
        tqApp->quit();
}

static inline TQString internalDnsRuleName()
{
    return "__ui_internal__dns_opensnitch_tqt";
}

static inline int isInternalUiRuleName(const TQString& name)
{
    return name.startsWith("__ui_internal__");
}

static inline TQString jsonEscapeLite(const TQString& s)
{
    TQString out;
    out.reserve((int)s.length() + 8);
    for (int i = 0; i < (int)s.length(); ++i) {
        const TQChar ch = s.at(i);
        if (ch == '\\') out += "\\\\";
        else if (ch == '"') out += "\\\"";
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else out += ch;
    }
    return out;
}

static inline TQString regexEscapeLite(const TQString& s)
{
    TQString out;
    out.reserve((int)s.length() * 2);
    for (int i = 0; i < (int)s.length(); ++i) {
        const TQChar ch = s.at(i);
        if (ch == '\\' || ch == '.' || ch == '+' || ch == '*' || ch == '?' || ch == '[' || ch == ']' ||
            ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == '^' || ch == '$' || ch == '|') {
            out += "\\";
        }
        out += ch;
    }
    return out;
}

static inline TQString buildOperatorListJson(const TQString& procPath)
{
    TQString base = procPath;
    int slash = base.findRev('/');
    if (slash >= 0)
        base = base.mid(slash + 1);
    base = base.stripWhiteSpace();
    if (base.isEmpty())
        base = "opensnitch-tqt";
    return "(.*/)?" + regexEscapeLite(base) + "$";
}

static inline TQString selfExePath()
{
    char buf[4096];
    buf[0] = '\0';
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return TQString();
    buf[n] = '\0';
    return TQString::fromLocal8Bit(buf);
}

void MainWindow::ensureInternalDnsRule(const TQString& node)
{
    if (m_internalDnsRuleCreated)
        return;

    // Normalize peer address (matches Python get_addr: "unix:" -> "unix:/local")
    TQString nNode = node;
    {
        TQString p, a;
        Nodes::splitPeer(node, p, a);
        nNode = p + ":" + a;
    }

    const TQString name = internalDnsRuleName();
    Rules::RuleRecord* existing = Rules::instance()->get(name, nNode);

    // If the daemon isn't ready to receive notifications yet, avoid dropping
    // the rule update (sendNotification is a no-op if no stream is registered).
    // We'll retry later from onStatsUpdated().
    if (!m_server->hasNotificationStream(nNode))
        return;

    const TQString exe = selfExePath();
    if (exe.isEmpty())
        return;

    protocol::Rule r;
    r.set_name(name.latin1());
    r.set_enabled(true);
    r.set_precedence(false);
    r.set_action(Config::ACTION_ALLOW);
    r.set_duration(Config::DURATION_UNTIL_RESTART);

    protocol::Operator* op = r.mutable_operator_();
    op->set_type(Config::RULE_TYPE_SIMPLE);
    op->set_operand(Config::OPERAND_PROCESS_PATH);
    op->set_data(exe.latin1());
    op->set_sensitive(false);

    const TQString now = TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    Rules::instance()->add(now, nNode, r);

    protocol::Notification notif;
    notif.set_id((uint64_t)time(0));
    notif.set_type(protocol::CHANGE_RULE);
    protocol::Rule* rr = notif.add_rules();
    if (rr)
        rr->CopyFrom(r);
    m_server->sendNotification(nNode, notif);

    m_internalDnsRuleCreated = 1;
    m_internalDnsRuleNode = nNode;

    if (m_tabs && m_tabs->currentPageIndex() == TAB_ADDRS)
        refreshAddrs();
}

void MainWindow::removeInternalDnsRule()
{
    if (!m_internalDnsRuleCreated)
        return;
    if (m_internalDnsRuleNode.isEmpty())
        return;

    const TQString name = internalDnsRuleName();
    const TQString node = m_internalDnsRuleNode;

    if (m_server) {
        protocol::Notification notif;
        notif.set_id((uint64_t)time(0));
        notif.set_type(protocol::DELETE_RULE);
        protocol::Rule* r = notif.add_rules();
        if (r)
            r->set_name(name.latin1());
        m_server->broadcastNotification(notif);
    }
    Rules::instance()->remove(name, node);

    m_internalDnsRuleCreated = 0;
    m_internalDnsRuleNode = TQString();
}

void MainWindow::onAboutClicked()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::onQuitClicked()
{
    TQString defaultAction = "-";
    Config* cfg = Config::get();
    if (cfg)
        defaultAction = cfg->getDefaultAction();

    TQString msg = "Quit OpenSnitch UI?\n\n"
                  "Note: the OpenSnitch daemon will continue running.\n\n"
                  "Default action: " + defaultAction;

    const int r = TQMessageBox::warning(this, "Quit", msg,
                                       "Quit", "Cancel", 0,
                                       1, 1);
    if (r != 0)
        return;

    onQuitRequested();
}

void MainWindow::setRefreshIntervalSeconds(int seconds)
{
    if (!m_refreshTimer)
        return;
    if (seconds <= 0)
        seconds = 1;
    m_refreshTimer->start(seconds * 1000);
}

void MainWindow::setupUi()
{
    setCaption("OpenSnitch-tde Network Statistics");
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(opensnitch_icon_png, (int)opensnitch_icon_png_len, 32);
        if (!pm.isNull())
            setIcon(pm);
    }
    setMinimumSize(1000, 700);
    resize(1000, 700);

    m_mainLay = new TQVBoxLayout(this, 0, 0);

    setupToolBar();
    setupTabWidget();
    setupFilterBar();
    setupStatusBar();
}

void MainWindow::setupToolBar()
{
    m_toolBar = new TQFrame(this);
    m_toolBar->setFrameShape(TQFrame::StyledPanel);
    m_toolBar->setFrameShadow(TQFrame::Raised);

    TQHBoxLayout* lay = new TQHBoxLayout(m_toolBar, 2, 4);

    // Left: save, preferences, new buttons
    m_saveBtn = new TQToolButton(m_toolBar);
    applyEmbeddedIcon(m_saveBtn, document_save_png, (int)document_save_png_len);
    m_saveBtn->setTextLabel("Save");
    m_saveBtn->setUsesTextLabel(false);
    m_saveBtn->setAutoRaise(true);
    connect(m_saveBtn, SIGNAL(clicked()), this, SLOT(onSaveClicked()));
    lay->addWidget(m_saveBtn);

    m_refreshBtn = new TQToolButton(m_toolBar);
    applyEmbeddedIcon(m_refreshBtn, preferences_desktop_png, (int)preferences_desktop_png_len);
    m_refreshBtn->setTextLabel("Preferences");
    m_refreshBtn->setUsesTextLabel(false);
    m_refreshBtn->setAutoRaise(true);
    connect(m_refreshBtn, SIGNAL(clicked()), this, SLOT(onPreferencesClicked()));
    lay->addWidget(m_refreshBtn);

    m_newBtn = new TQToolButton(m_toolBar);
    applyEmbeddedIcon(m_newBtn, document_new_png, (int)document_new_png_len);
    m_newBtn->setTextLabel("New rule");
    m_newBtn->setUsesTextLabel(false);
    m_newBtn->setAutoRaise(true);
    connect(m_newBtn, SIGNAL(clicked()), this, SLOT(onNewClicked()));
    lay->addWidget(m_newBtn);

    m_aboutBtn = new TQToolButton(m_toolBar);
    applyEmbeddedIcon(m_aboutBtn, about_png, (int)about_png_len);
    m_aboutBtn->setTextLabel("About");
    m_aboutBtn->setUsesTextLabel(false);
    m_aboutBtn->setAutoRaise(true);
    connect(m_aboutBtn, SIGNAL(clicked()), this, SLOT(onAboutClicked()));
    lay->addWidget(m_aboutBtn);

    m_quitBtn = new TQToolButton(m_toolBar);
    applyEmbeddedIcon(m_quitBtn, quit_png, (int)quit_png_len);
    m_quitBtn->setTextLabel("Quit");
    m_quitBtn->setUsesTextLabel(false);
    m_quitBtn->setAutoRaise(true);
    connect(m_quitBtn, SIGNAL(clicked()), this, SLOT(onQuitClicked()));
    lay->addWidget(m_quitBtn);

    // Spacer
    lay->addStretch(1);

    // Right: State label + status + pause button
    m_stateLabel = new TQLabel("State:", m_toolBar);
    lay->addWidget(m_stateLabel);

    m_stateValueLabel = new TQLabel("Active", m_toolBar);
    TQPalette pal = m_stateValueLabel->palette();
    pal.setColor(TQColorGroup::Foreground, TQColor(0, 0x99, 0));
    m_stateValueLabel->setPalette(pal);
    m_stateValueLabel->setAlignment(TQt::AlignLeft | TQt::AlignVCenter);
    lay->addWidget(m_stateValueLabel);

    m_pauseBtn = new TQToolButton(m_toolBar);
    applyEmbeddedIcon(m_pauseBtn, media_playback_pause_png, (int)media_playback_pause_png_len);
    m_pauseBtn->setTextLabel("Pause interception");
    m_pauseBtn->setUsesTextLabel(false);
    m_pauseBtn->setToggleButton(true);
    m_pauseBtn->setAutoRaise(true);
    connect(m_pauseBtn, SIGNAL(clicked()), this, SLOT(onInterceptionToggled()));
    lay->addWidget(m_pauseBtn);

    m_mainLay->addWidget(m_toolBar);
}

void MainWindow::setupTabWidget()
{
    m_tabs = new TQTabWidget(this);
    m_tabs->setMargin(0);
    m_mainLay->addWidget(m_tabs, 1);

    setupEventsTab();
    setupNodesTab();
    setupRulesTab();
    setupHostsTab();

    setupProcsTab();

    setupAddrsTab();

    setupPortsTab();

    setupUsersTab();

    m_tabs->addTab(m_eventsTab, loadEmbeddedIcon(events_png, (int)events_png_len), "Events");
    m_tabs->addTab(m_nodesTab, loadEmbeddedIcon(nodes_png, (int)nodes_png_len), "Nodes");
    m_tabs->addTab(m_rulesTab, loadEmbeddedIcon(rules_png, (int)rules_png_len), "Rules");
    m_tabs->addTab(m_hostsTab, loadEmbeddedIcon(hosts_png, (int)hosts_png_len), "Hosts");
    m_tabs->addTab(m_procsTab, loadEmbeddedIcon(applications_png, (int)applications_png_len), "Applications");
    m_tabs->addTab(m_addrsTab, loadEmbeddedIcon(addr_png, (int)addr_png_len), "Addresses");
    m_tabs->addTab(m_portsTab, loadEmbeddedIcon(ports_png, (int)ports_png_len), "Ports");
    m_tabs->addTab(m_usersTab, loadEmbeddedIcon(users_png, (int)users_png_len), "Users");

    connect(m_tabs, SIGNAL(currentChanged(TQWidget*)), this, SLOT(onTabChanged(TQWidget*)));
}

void MainWindow::setupAddrsTab()
{
    m_addrsTab = new TQWidget(m_tabs);
    TQVBoxLayout* lay = new TQVBoxLayout(m_addrsTab, 0, 0);

    m_addrsInDetail = 0;
    m_addrsDetailWhat = TQString();

    m_addrsDetailBar = new TQWidget(m_addrsTab);
    TQHBoxLayout* detailLay = new TQHBoxLayout(m_addrsDetailBar, 2, 4);
    m_addrsBackBtn = new TQPushButton("", m_addrsDetailBar);
    m_addrsBackBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
        if (!pm.isNull())
            m_addrsBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_addrsBackBtn);

    m_addrsDetailLabel = new TQLabel(m_addrsDetailBar);
    detailLay->addWidget(m_addrsDetailLabel, 1);
    m_addrsDetailBar->hide();
    connect(m_addrsBackBtn, SIGNAL(clicked()), this, SLOT(onAddrDetailBackClicked()));

    // Main list: What, Hits, Network name (empty placeholder; Python uses ASN DB)
    m_addrsModel = new TQtListStore(3, this);
    m_addrsModel->setHeader(0, "What");
    m_addrsModel->setHeader(1, "Hits");
    m_addrsModel->setHeader(2, "Network name");

    m_addrsModel->setColumnStyle(2, TQtCellStyle().setForeground(TQColor(0, 0, 0xCC)));

    m_addrsTable = new TQtMvcTableView(m_addrsTab);
    m_addrsTable->setModel(m_addrsModel);
    m_addrsTable->setSortingEnabled(true);
    m_addrsTable->setColumnWidth(0, 200);
    m_addrsTable->setColumnWidth(1, 80);
    m_addrsTable->setColumnWidth(2, 360);
    connect(m_addrsTable, SIGNAL(doubleClicked(int,int,int,const TQPoint&)),
            this, SLOT(onAddrRowDoubleClicked4(int,int,int,const TQPoint&)));

    // Detail list: matches Python StatsDialog::_set_addrs_query
    m_addrsConnModel = new TQtListStore(12, this);
    m_addrsConnModel->setHeader(0, "Time");
    m_addrsConnModel->setHeader(1, "Node");
    m_addrsConnModel->setHeader(2, "Hits");
    m_addrsConnModel->setHeader(3, "Action");
    m_addrsConnModel->setHeader(4, "UID");
    m_addrsConnModel->setHeader(5, "Protocol");
    m_addrsConnModel->setHeader(6, "Destination");
    m_addrsConnModel->setHeader(7, "DstPort");
    m_addrsConnModel->setHeader(8, "Process");
    m_addrsConnModel->setHeader(9, "Args");
    m_addrsConnModel->setHeader(10, "CWD");
    m_addrsConnModel->setHeader(11, "Rule");

    // Colorize Action column like Python delegate.
    m_addrsConnModel->addStyleRule(TQtStyleRule("allow", 3,
        TQtCellStyle().setForeground(TQColor(0, 0x99, 0)).setBold(true)));
    m_addrsConnModel->addStyleRule(TQtStyleRule("deny", 3,
        TQtCellStyle().setForeground(TQColor(0xCC, 0, 0)).setBold(true)));
    m_addrsConnModel->addStyleRule(TQtStyleRule("reject", 3,
        TQtCellStyle().setForeground(TQColor(0xCC, 0, 0)).setBold(true)));

    m_addrsConnView = new TQtMvcTableView(m_addrsTab);
    m_addrsConnView->setModel(m_addrsConnModel);
    m_addrsConnView->setSortingEnabled(true);
    m_addrsConnView->hide();

    lay->addWidget(m_addrsDetailBar);
    lay->addWidget(m_addrsTable, 1);
    lay->addWidget(m_addrsConnView, 1);

    // Async resolver for Network name column.
    NetIdentityResolver::instance()->setUiTarget(this);
}

void MainWindow::setupPortsTab()
{
    m_portsTab = new TQWidget(m_tabs);
    TQVBoxLayout* lay = new TQVBoxLayout(m_portsTab, 0, 0);

    m_portsInDetail = 0;
    m_portsDetailWhat = TQString();

    m_portsDetailBar = new TQWidget(m_portsTab);
    TQHBoxLayout* detailLay = new TQHBoxLayout(m_portsDetailBar, 2, 4);
    m_portsBackBtn = new TQPushButton("", m_portsDetailBar);
    m_portsBackBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
        if (!pm.isNull())
            m_portsBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_portsBackBtn);

    m_portsDetailLabel = new TQLabel(m_portsDetailBar);
    detailLay->addWidget(m_portsDetailLabel, 1);
    m_portsDetailBar->hide();
    connect(m_portsBackBtn, SIGNAL(clicked()), this, SLOT(onPortDetailBackClicked()));

    // Main list: What, Hits
    m_portsModel = new TQtListStore(2, this);
    m_portsModel->setHeader(0, "what");
    m_portsModel->setHeader(1, "hits");

    m_portsModel->setColumnStyle(1, TQtCellStyle().setForeground(TQColor(0xD0, 0x70, 0x00)).setBold(true));

    m_portsTable = new TQtMvcTableView(m_portsTab);
    m_portsTable->setModel(m_portsModel);
    m_portsTable->setSortingEnabled(true);
    m_portsTable->setColumnWidth(0, 120);
    m_portsTable->setColumnWidth(1, 80);
    connect(m_portsTable, SIGNAL(doubleClicked(int,int,int,const TQPoint&)),
            this, SLOT(onPortRowDoubleClicked4(int,int,int,const TQPoint&)));

    // Detail list: matches Python StatsDialog::_set_ports_query
    m_portsConnModel = new TQtListStore(12, this);
    m_portsConnModel->setHeader(0, "Time");
    m_portsConnModel->setHeader(1, "Node");
    m_portsConnModel->setHeader(2, "Hits");
    m_portsConnModel->setHeader(3, "Action");
    m_portsConnModel->setHeader(4, "UID");
    m_portsConnModel->setHeader(5, "Protocol");
    m_portsConnModel->setHeader(6, "DstIP");
    m_portsConnModel->setHeader(7, "Destination");
    m_portsConnModel->setHeader(8, "Process");
    m_portsConnModel->setHeader(9, "Args");
    m_portsConnModel->setHeader(10, "CWD");
    m_portsConnModel->setHeader(11, "Rule");

    m_portsConnModel->addStyleRule(TQtStyleRule("allow", 3,
        TQtCellStyle().setForeground(TQColor(0, 0x99, 0)).setBold(true)));
    m_portsConnModel->addStyleRule(TQtStyleRule("deny", 3,
        TQtCellStyle().setForeground(TQColor(0xCC, 0, 0)).setBold(true)));
    m_portsConnModel->addStyleRule(TQtStyleRule("reject", 3,
        TQtCellStyle().setForeground(TQColor(0xCC, 0, 0)).setBold(true)));

    m_portsConnView = new TQtMvcTableView(m_portsTab);
    m_portsConnView->setModel(m_portsConnModel);
    m_portsConnView->setSortingEnabled(true);
    m_portsConnView->hide();

    lay->addWidget(m_portsDetailBar);
    lay->addWidget(m_portsTable, 1);
    lay->addWidget(m_portsConnView, 1);
}

void MainWindow::setupUsersTab()
{
    m_usersTab = new TQWidget(m_tabs);
    TQVBoxLayout* lay = new TQVBoxLayout(m_usersTab, 0, 0);

    m_usersInDetail = 0;
    m_usersDetailWhat = TQString();

    m_usersDetailBar = new TQWidget(m_usersTab);
    TQHBoxLayout* detailLay = new TQHBoxLayout(m_usersDetailBar, 2, 4);
    m_usersBackBtn = new TQPushButton("", m_usersDetailBar);
    m_usersBackBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
        if (!pm.isNull())
            m_usersBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_usersBackBtn);

    m_usersDetailLabel = new TQLabel(m_usersDetailBar);
    detailLay->addWidget(m_usersDetailLabel, 1);
    m_usersDetailBar->hide();
    connect(m_usersBackBtn, SIGNAL(clicked()), this, SLOT(onUserDetailBackClicked()));

    // Main list: what, hits
    m_usersModel = new TQtListStore(2, this);
    m_usersModel->setHeader(0, "what");
    m_usersModel->setHeader(1, "hits");

    m_usersModel->setColumnStyle(1, TQtCellStyle().setForeground(TQColor(0xD0, 0x70, 0x00)).setBold(true));

    m_usersTableMvc = new TQtMvcTableView(m_usersTab);
    m_usersTableMvc->setModel(m_usersModel);
    m_usersTableMvc->setSortingEnabled(true);
    m_usersTableMvc->setColumnWidth(0, 220);
    m_usersTableMvc->setColumnWidth(1, 90);
    connect(m_usersTableMvc, SIGNAL(doubleClicked(int,int,int,const TQPoint&)),
            this, SLOT(onUserRowDoubleClicked4(int,int,int,const TQPoint&)));

    // Detail list: matches Python StatsDialog::_set_users_query
    m_usersConnModel = new TQtListStore(13, this);
    m_usersConnModel->setHeader(0, "Time");
    m_usersConnModel->setHeader(1, "UID");
    m_usersConnModel->setHeader(2, "Node");
    m_usersConnModel->setHeader(3, "Hits");
    m_usersConnModel->setHeader(4, "Action");
    m_usersConnModel->setHeader(5, "Protocol");
    m_usersConnModel->setHeader(6, "DstIP");
    m_usersConnModel->setHeader(7, "Destination");
    m_usersConnModel->setHeader(8, "DstPort");
    m_usersConnModel->setHeader(9, "Process");
    m_usersConnModel->setHeader(10, "Args");
    m_usersConnModel->setHeader(11, "CWD");
    m_usersConnModel->setHeader(12, "Rule");

    m_usersConnModel->addStyleRule(TQtStyleRule("allow", 4,
        TQtCellStyle().setForeground(TQColor(0, 0x99, 0)).setBold(true)));
    m_usersConnModel->addStyleRule(TQtStyleRule("deny", 4,
        TQtCellStyle().setForeground(TQColor(0xCC, 0, 0)).setBold(true)));
    m_usersConnModel->addStyleRule(TQtStyleRule("reject", 4,
        TQtCellStyle().setForeground(TQColor(0xCC, 0, 0)).setBold(true)));

    m_usersConnView = new TQtMvcTableView(m_usersTab);
    m_usersConnView->setModel(m_usersConnModel);
    m_usersConnView->setSortingEnabled(true);
    m_usersConnView->hide();

    lay->addWidget(m_usersDetailBar);
    lay->addWidget(m_usersTableMvc, 1);
    lay->addWidget(m_usersConnView, 1);
}

void MainWindow::setupEventsTab()
{
    m_eventsTab = new TQWidget(m_tabs);

    TQVBoxLayout* evtLay = new TQVBoxLayout(m_eventsTab, 0, 0);

    // MVC model: 7 columns
    m_eventsModel = new TQtListStore(EVT_COL_COUNT, this);
    m_eventsModel->setHeader(EVT_COL_TIME, "Time");
    m_eventsModel->setHeader(EVT_COL_NODE, "Node");
    m_eventsModel->setHeader(EVT_COL_ACTION, "Action");
    m_eventsModel->setHeader(EVT_COL_DEST, "Destination");
    m_eventsModel->setHeader(EVT_COL_PROTO, "Protocol");
    m_eventsModel->setHeader(EVT_COL_PROCESS, "Process");
    m_eventsModel->setHeader(EVT_COL_RULE, "Rule");

    // Style rules for action column coloring (replaces ColoredTableItem)
    m_eventsModel->addStyleRule(TQtStyleRule("allow", EVT_COL_ACTION,
        TQtCellStyle().setForeground(TQColor(0, 0x99, 0)).setBold(true)));
    m_eventsModel->addStyleRule(TQtStyleRule("deny", EVT_COL_ACTION,
        TQtCellStyle().setForeground(TQColor(0xCC, 0, 0)).setBold(true)));
    m_eventsModel->addStyleRule(TQtStyleRule("reject", EVT_COL_ACTION,
        TQtCellStyle().setForeground(TQColor(0xCC, 0, 0)).setBold(true)));

    m_eventsModel->setColumnStyle(EVT_COL_RULE, TQtCellStyle().setForeground(TQColor(0, 0, 0xCC)));

    // MVC view
    m_eventsTable = new TQtMvcTableView(m_eventsTab);
    m_eventsTable->setModel(m_eventsModel);
    m_eventsTable->setSortingEnabled(true);
    m_eventsTable->setVScrollBarMode(TQScrollView::Auto);

    m_eventsTable->setColumnWidth(EVT_COL_TIME, 140);
    m_eventsTable->setColumnWidth(EVT_COL_NODE, 120);
    m_eventsTable->setColumnWidth(EVT_COL_ACTION, 60);
    m_eventsTable->setColumnWidth(EVT_COL_DEST, 180);
    m_eventsTable->setColumnWidth(EVT_COL_PROTO, 80);
    m_eventsTable->setColumnWidth(EVT_COL_PROCESS, 200);
    m_eventsTable->setColumnWidth(EVT_COL_RULE, 120);

    connect(m_eventsTable, SIGNAL(doubleClicked(int,int,int,const TQPoint&)),
            this, SLOT(onEventRowDoubleClicked4(int,int,int,const TQPoint&)));

    applyEventsColumnsFromConfig();

    evtLay->addWidget(m_eventsTable, 1);
}

void MainWindow::onEventRowDoubleClicked(int modelRow, int col)
{
    if (!m_eventsTable || !m_eventsModel || !m_tabs)
        return;
    if (modelRow < 0 || modelRow >= m_eventsModel->rowCount())
        return;

    if (col == EVT_COL_PROCESS) {
        TQString proc = m_eventsModel->data(modelRow, EVT_COL_PROCESS).toString().stripWhiteSpace();
        if (proc.isEmpty())
            return;
        m_tabs->setCurrentPage(TAB_PROCS);

        if (m_procsInDetail)
            onProcDetailBackClicked();
        else
            refreshProcs();

        const int r = findExactRow(m_procsModel, 0, proc);
        if (r >= 0 && m_procsTable) {
            m_procsTable->selectModelRow(r);
            m_procsTable->ensureModelRowVisible(r);
        }
    } else if (col == EVT_COL_NODE) {
        TQString node = m_eventsModel->data(modelRow, EVT_COL_NODE).toString().stripWhiteSpace();
        if (node.isEmpty())
            return;
        m_tabs->setCurrentPage(TAB_NODES);

        if (m_nodesInDetail)
            onNodeDetailBackClicked();
        else
            refreshNodes();

        int r = findExactRow(m_nodesModel, NODE_COL_ADDR, node);
        if (r < 0)
            r = findExactRow(m_nodesModel, NODE_COL_HOSTNAME, node);
        if (r >= 0 && m_nodesTable) {
            m_nodesTable->selectModelRow(r);
            m_nodesTable->ensureModelRowVisible(r);
        }
    } else if (col == EVT_COL_RULE) {
        TQString rule = m_eventsModel->data(modelRow, EVT_COL_RULE).toString().stripWhiteSpace();
        if (rule.isEmpty())
            return;
        m_tabs->setCurrentPage(TAB_RULES);

        if (m_rulesInDetail)
            setRulesDetailView(TQString(), TQString(), 0);
        refreshRules();

        const int r = findExactRow(m_rulesModel, RULEMVC_COL_NAME, rule);
        if (r >= 0 && m_rulesTable) {
            m_rulesTable->selectModelRow(r);
            m_rulesTable->ensureModelRowVisible(r);
        }
    }
}

void MainWindow::onEventRowDoubleClicked4(int row, int col, int button, const TQPoint& p)
{
    (void)button;
    (void)p;
    if (!m_eventsTable)
        return;

    int modelRow = m_eventsTable->modelRow(row);
    if (modelRow < 0)
        modelRow = m_eventsTable->selectedModelRow();
    if (modelRow < 0 && m_eventsModel && row >= 0 && row < m_eventsModel->rowCount())
        modelRow = row;

    onEventRowDoubleClicked(modelRow, col);
}

void MainWindow::applyEventsColumnsFromConfig()
{
    Config* cfg = Config::get();
    if (!cfg || !m_eventsTable)
        return;

    // First show all columns, then hide configured ones.
    // TQTable implements showColumn()/hideColumn().
    for (int i = 0; i < EVT_COL_COUNT; ++i)
        m_eventsTable->showColumn(i);

    TQVariant v = cfg->getSetting(Config::KEY_STATS_SHOW_COLUMNS_CONNECTIONS, TQVariant());
    if (!v.isValid())
        return;

    TQStringList hidden;
    {
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
        if (col < 0 || col >= EVT_COL_COUNT) continue;
        m_eventsTable->hideColumn(col);
    }

    // Force relayout/repaint so changes are immediately visible.
    m_eventsTable->update();
}

void MainWindow::setupRulesTab()
{
    m_rulesTab = new TQWidget(m_tabs);

    m_rulesInDetail = 0;
    m_rulesDetailNode = TQString();
    m_rulesDetailName = TQString();

    // Toolbar
    TQWidget* toolbar = new TQWidget(m_rulesTab);
    TQHBoxLayout* toolbarLay = new TQHBoxLayout(toolbar, 2, 4);

    m_rulesFilterCombo = new TQComboBox(toolbar);
    m_rulesFilterCombo->insertItem("All");
    m_rulesFilterCombo->insertItem("Allow");
    m_rulesFilterCombo->insertItem("Deny");
    m_rulesFilterCombo->insertItem("Reject");

    connect(m_rulesFilterCombo, SIGNAL(activated(int)), this, SLOT(onRulesUpdated()));

    toolbarLay->addWidget(m_rulesFilterCombo, 1);

    const int tbpx = toolbarIconPx();

    m_newRuleBtn = new TQPushButton("", toolbar);
    m_newRuleBtn->setFixedSize(tbpx, tbpx);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(document_new_png, (int)document_new_png_len, tbpx);
        if (!pm.isNull())
            m_newRuleBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }

    m_toggleRuleBtn = new TQPushButton("", toolbar);
    m_toggleRuleBtn->setFixedSize(tbpx, tbpx);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(toggle_png, (int)toggle_png_len, tbpx);
        if (!pm.isNull())
            m_toggleRuleBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }

    m_delRuleBtn = new TQPushButton("", toolbar);
    m_delRuleBtn->setFixedSize(tbpx, tbpx);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(trash_png, (int)trash_png_len, tbpx);
        if (!pm.isNull())
            m_delRuleBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    toolbarLay->addWidget(m_newRuleBtn);
    toolbarLay->addWidget(m_toggleRuleBtn);
    toolbarLay->addWidget(m_delRuleBtn);

    connect(m_delRuleBtn, SIGNAL(clicked()), this, SLOT(onDeleteRuleClicked()));
    connect(m_toggleRuleBtn, SIGNAL(clicked()), this, SLOT(onToggleRuleClicked()));
    connect(m_newRuleBtn, SIGNAL(clicked()), this, SLOT(onNewClicked()));

    // Split view: left tree + right list/detail
    TQHBoxLayout* mainLay = new TQHBoxLayout(m_rulesTab, 0, 0);
    TQVBoxLayout* rightLay = new TQVBoxLayout(0, 0, 0);

    // Detail bar (hidden by default, matches Nodes UX)
    m_rulesDetailBar = new TQWidget(m_rulesTab);
    TQHBoxLayout* detailLay = new TQHBoxLayout(m_rulesDetailBar, 2, 4);
    m_rulesBackBtn = new TQPushButton("", m_rulesDetailBar);
    m_rulesBackBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
        if (!pm.isNull())
            m_rulesBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_rulesBackBtn);

    m_rulesEditBtn = new TQPushButton("", m_rulesDetailBar);
    m_rulesEditBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(edit_png, (int)edit_png_len, 16);
        if (!pm.isNull())
            m_rulesEditBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_rulesEditBtn);

    m_rulesDeleteBtn = new TQPushButton("", m_rulesDetailBar);
    m_rulesDeleteBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(trash_png, (int)trash_png_len, 16);
        if (!pm.isNull())
            m_rulesDeleteBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_rulesDeleteBtn);

    m_rulesDetailLabel = new TQLabel(m_rulesDetailBar);
    detailLay->addWidget(m_rulesDetailLabel, 1);
    m_rulesDetailBar->hide();
    connect(m_rulesBackBtn, SIGNAL(clicked()), this, SLOT(onRuleDetailBackClicked()));
    connect(m_rulesEditBtn, SIGNAL(clicked()), this, SLOT(onRuleDetailEditClicked()));
    connect(m_rulesDeleteBtn, SIGNAL(clicked()), this, SLOT(onRuleDetailDeleteClicked()));

    // Left filter tree (matches Python "Application rules" + nodes)
    m_rulesTree = new TQListView(m_rulesTab);
    m_rulesTree->addColumn("Rule applies to");
    m_rulesTree->setRootIsDecorated(true);
    m_rulesTree->setSorting(-1);

    TQListViewItem* appRoot = new TQListViewItem(m_rulesTree, "Application rules");
    new TQListViewItem(appRoot, "Permanent");
    new TQListViewItem(appRoot, "Temporary");
    appRoot->setOpen(true);

    TQListViewItem* nodesRoot = new TQListViewItem(m_rulesTree, "Nodes");
    nodesRoot->setOpen(true);

    connect(m_rulesTree, SIGNAL(selectionChanged(TQListViewItem*)),
            this, SLOT(onRulesTreeSelectionChanged(TQListViewItem*)));

    updateRulesTreeNodes();

    // Rules MVC model/view
    m_rulesModel = new TQtListStore(RULEMVC_COL_COUNT, this);
    m_rulesModel->setHeader(RULEMVC_COL_TIME,         "Time");
    m_rulesModel->setHeader(RULEMVC_COL_NODE,         "Node");
    m_rulesModel->setHeader(RULEMVC_COL_NAME,         "Name");
    m_rulesModel->setHeader(RULEMVC_COL_ACTIVE,       "Active");
    m_rulesModel->setHeader(RULEMVC_COL_PRECEDENCE,   "Precedence");
    m_rulesModel->setHeader(RULEMVC_COL_ACTION,       "Action");
    m_rulesModel->setHeader(RULEMVC_COL_DURATION,     "Duration");
    m_rulesModel->setHeader(RULEMVC_COL_OP_TYPE,      "operator_type");
    m_rulesModel->setHeader(RULEMVC_COL_OP_SENSITIVE, "operator_sensitive");
    m_rulesModel->setHeader(RULEMVC_COL_OP_OPERAND,   "operator_operand");
    m_rulesModel->setHeader(RULEMVC_COL_OP_DATA,      "operator_data");

    // Action colors (match Python: allow=green, deny=red, reject=purple)
    m_rulesModel->addStyleRule(TQtStyleRule("allow", RULEMVC_COL_ACTION, TQtCellStyle().setForeground(TQColor(0x00, 0x80, 0x00))));
    m_rulesModel->addStyleRule(TQtStyleRule("deny",  RULEMVC_COL_ACTION, TQtCellStyle().setForeground(TQColor(0xFF, 0x00, 0x00))));
    m_rulesModel->addStyleRule(TQtStyleRule("reject",RULEMVC_COL_ACTION, TQtCellStyle().setForeground(TQColor(0x7F, 0x00, 0xFF))));

    m_rulesModel->setColumnStyle(RULEMVC_COL_NAME, TQtCellStyle().setForeground(TQColor(0, 0, 0xCC)));

    m_rulesTable = new TQtMvcTableView(m_rulesTab);
    m_rulesTable->setModel(m_rulesModel);
    m_rulesTable->setSortingEnabled(true);

    connect(m_rulesTable, SIGNAL(doubleClicked(int,int,int,const TQPoint&)),
            this, SLOT(onRuleRowDoubleClicked4(int,int,int,const TQPoint&)));
    connect(m_rulesTable, SIGNAL(rowContextMenuRequested(int,int,const TQPoint&)),
            this, SLOT(onRulesContextMenu(int,int,const TQPoint&)));

    // Detail view model/table (hidden by default)
    m_rulesConnModel = new TQtListStore(RULEDET_COL_COUNT, this);
    m_rulesConnModel->setHeader(RULEDET_COL_TIME,        "Time");
    m_rulesConnModel->setHeader(RULEDET_COL_NODE,        "Node");
    m_rulesConnModel->setHeader(RULEDET_COL_HITS,        "Connections");
    m_rulesConnModel->setHeader(RULEDET_COL_UID,         "UID");
    m_rulesConnModel->setHeader(RULEDET_COL_PROTOCOL,    "Protocol");
    m_rulesConnModel->setHeader(RULEDET_COL_DST_PORT,    "Dst Port");
    m_rulesConnModel->setHeader(RULEDET_COL_DESTINATION, "Destination");
    m_rulesConnModel->setHeader(RULEDET_COL_PROCESS,     "Process");
    m_rulesConnModel->setHeader(RULEDET_COL_PROC_ARGS,   "Args");
    m_rulesConnModel->setHeader(RULEDET_COL_CWD,         "CWD");

    m_rulesConnView = new TQtMvcTableView(m_rulesTab);
    m_rulesConnView->setModel(m_rulesConnModel);
    m_rulesConnView->setSortingEnabled(true);
    m_rulesConnView->hide();

    m_rulesTable->setColumnWidth(RULEMVC_COL_TIME,         140);
    m_rulesTable->setColumnWidth(RULEMVC_COL_NODE,         120);
    m_rulesTable->setColumnWidth(RULEMVC_COL_NAME,         140);
    m_rulesTable->setColumnWidth(RULEMVC_COL_ACTIVE,        60);
    m_rulesTable->setColumnWidth(RULEMVC_COL_PRECEDENCE,    80);
    m_rulesTable->setColumnWidth(RULEMVC_COL_ACTION,        60);
    m_rulesTable->setColumnWidth(RULEMVC_COL_DURATION,      90);
    m_rulesTable->setColumnWidth(RULEMVC_COL_OP_TYPE,       90);
    m_rulesTable->setColumnWidth(RULEMVC_COL_OP_SENSITIVE, 110);
    m_rulesTable->setColumnWidth(RULEMVC_COL_OP_OPERAND,   140);
    m_rulesTable->setColumnWidth(RULEMVC_COL_OP_DATA,      260);

    rightLay->addWidget(toolbar);
    rightLay->addWidget(m_rulesDetailBar);
    rightLay->addWidget(m_rulesTable, 1);
    rightLay->addWidget(m_rulesConnView, 1);

    mainLay->addWidget(m_rulesTree, 0);
    mainLay->addLayout(rightLay, 1);
}

void MainWindow::setupHostsTab()
{
    m_hostsTab = new TQWidget(m_tabs);
    TQVBoxLayout* lay = new TQVBoxLayout(m_hostsTab, 0, 0);

    m_hostsInDetail = 0;
    m_hostsDetailWhat = TQString();

    m_hostsDetailBar = new TQWidget(m_hostsTab);
    TQHBoxLayout* detailLay = new TQHBoxLayout(m_hostsDetailBar, 2, 4);
    m_hostsBackBtn = new TQPushButton("", m_hostsDetailBar);
    m_hostsBackBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
        if (!pm.isNull())
            m_hostsBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_hostsBackBtn);
    m_hostsDetailLabel = new TQLabel(m_hostsDetailBar);
    detailLay->addWidget(m_hostsDetailLabel, 1);
    m_hostsDetailBar->hide();
    connect(m_hostsBackBtn, SIGNAL(clicked()), this, SLOT(onHostDetailBackClicked()));

    m_hostsModel = new TQtListStore(2, this);
    m_hostsModel->setHeader(0, "What");
    m_hostsModel->setHeader(1, "Hits");

    m_hostsModel->setColumnStyle(1, TQtCellStyle().setForeground(TQColor(0xD0, 0x70, 0x00)).setBold(true));

    m_hostsTable = new TQtMvcTableView(m_hostsTab);
    m_hostsTable->setModel(m_hostsModel);
    m_hostsTable->setSortingEnabled(true);
    m_hostsTable->setColumnWidth(0, 320);
    m_hostsTable->setColumnWidth(1, 80);

    connect(m_hostsTable, SIGNAL(doubleClicked(int,int,int,const TQPoint&)),
            this, SLOT(onHostRowDoubleClicked4(int,int,int,const TQPoint&)));

    m_hostsConnModel = new TQtListStore(12, this);
    m_hostsConnModel->setHeader(0, "Time");
    m_hostsConnModel->setHeader(1, "Node");
    m_hostsConnModel->setHeader(2, "Hits");
    m_hostsConnModel->setHeader(3, "Action");
    m_hostsConnModel->setHeader(4, "UID");
    m_hostsConnModel->setHeader(5, "Protocol");
    m_hostsConnModel->setHeader(6, "Dst port");
    m_hostsConnModel->setHeader(7, "Dst ip");
    m_hostsConnModel->setHeader(8, "Process");
    m_hostsConnModel->setHeader(9, "Args");
    m_hostsConnModel->setHeader(10, "CWD");
    m_hostsConnModel->setHeader(11, "Rule");

    m_hostsConnView = new TQtMvcTableView(m_hostsTab);
    m_hostsConnView->setModel(m_hostsConnModel);
    m_hostsConnView->setSortingEnabled(true);
    m_hostsConnView->hide();

    lay->addWidget(m_hostsDetailBar);
    lay->addWidget(m_hostsTable, 1);
    lay->addWidget(m_hostsConnView, 1);
}

void MainWindow::setupProcsTab()
{
    m_procsTab = new TQWidget(m_tabs);
    TQVBoxLayout* lay = new TQVBoxLayout(m_procsTab, 0, 0);

    m_procsInDetail = 0;
    m_procsDetailWhat = TQString();

    m_procsDetailBar = new TQWidget(m_procsTab);
    TQHBoxLayout* detailLay = new TQHBoxLayout(m_procsDetailBar, 2, 4);
    m_procsBackBtn = new TQPushButton("", m_procsDetailBar);
    m_procsBackBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
        if (!pm.isNull())
            m_procsBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_procsBackBtn);

    m_procsDetailsBtn = new TQPushButton("", m_procsDetailBar);
    m_procsDetailsBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(system_search_png, (int)system_search_png_len, 16);
        if (!pm.isNull())
            m_procsDetailsBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_procsDetailsBtn);

    m_procsDetailLabel = new TQLabel(m_procsDetailBar);
    detailLay->addWidget(m_procsDetailLabel, 1);
    m_procsDetailBar->hide();
    connect(m_procsBackBtn, SIGNAL(clicked()), this, SLOT(onProcDetailBackClicked()));
    connect(m_procsDetailsBtn, SIGNAL(clicked()), this, SLOT(onProcDetailsClicked()));

    if (m_procsDetailsBtn)
        m_procsDetailsBtn->setEnabled(false);

    m_procsModel = new TQtListStore(2, this);
    m_procsModel->setHeader(0, "What");
    m_procsModel->setHeader(1, "Hits");

    m_procsModel->setColumnStyle(1, TQtCellStyle().setForeground(TQColor(0xD0, 0x70, 0x00)).setBold(true));

    m_procsTable = new TQtMvcTableView(m_procsTab);
    m_procsTable->setModel(m_procsModel);
    m_procsTable->setSortingEnabled(true);
    m_procsTable->setColumnWidth(0, 420);
    m_procsTable->setColumnWidth(1, 80);

    connect(m_procsTable, SIGNAL(doubleClicked(int,int,int,const TQPoint&)),
            this, SLOT(onProcRowDoubleClicked4(int,int,int,const TQPoint&)));

    // Detail view: matches Python _set_process_query
    m_procsConnModel = new TQtListStore(10, this);
    m_procsConnModel->setHeader(0, "Time");
    m_procsConnModel->setHeader(1, "Node");
    m_procsConnModel->setHeader(2, "Hits");
    m_procsConnModel->setHeader(3, "Action");
    m_procsConnModel->setHeader(4, "UID");
    m_procsConnModel->setHeader(5, "Destination");
    m_procsConnModel->setHeader(6, "PID");
    m_procsConnModel->setHeader(7, "Args");
    m_procsConnModel->setHeader(8, "CWD");
    m_procsConnModel->setHeader(9, "Rule");

    m_procsConnView = new TQtMvcTableView(m_procsTab);
    m_procsConnView->setModel(m_procsConnModel);
    m_procsConnView->setSortingEnabled(true);
    m_procsConnView->hide();

    connect(m_procsConnView, SIGNAL(selectionChanged(int)),
            this, SLOT(onProcConnSelectionChanged(int)));

    // Some TQt3 styles/behaviors don't trigger currentChanged() on mouse selection,
    // so also update the button state on click.
    connect(m_procsConnView, SIGNAL(clicked(int,int,int,const TQPoint&)),
            this, SLOT(onProcConnClicked4(int,int,int,const TQPoint&)));

    lay->addWidget(m_procsDetailBar);
    lay->addWidget(m_procsTable, 1);
    lay->addWidget(m_procsConnView, 1);
}

void MainWindow::setRulesDetailView(const TQString& node, const TQString& ruleName, int on)
{
    m_rulesInDetail = on ? 1 : 0;
    if (!on) {
        m_rulesDetailNode = TQString();
        m_rulesDetailName = TQString();
    } else {
        m_rulesDetailNode = node;
        m_rulesDetailName = ruleName;
    }

    if (m_rulesTree)
        m_rulesTree->setShown(on ? false : true);
    if (m_toggleRuleBtn)
        m_toggleRuleBtn->setShown(on ? false : true);
    if (m_delRuleBtn)
        m_delRuleBtn->setShown(on ? false : true);
    if (m_newRuleBtn)
        m_newRuleBtn->setShown(on ? false : true);
    if (m_rulesFilterCombo)
        m_rulesFilterCombo->setShown(on ? false : true);

    if (m_rulesDetailBar)
        m_rulesDetailBar->setShown(on ? true : false);
    if (m_rulesConnView)
        m_rulesConnView->setShown(on ? true : false);
    if (m_rulesTable)
        m_rulesTable->setShown(on ? false : true);

    if (m_rulesDetailLabel) {
        if (on)
            m_rulesDetailLabel->setText(ruleName);
        else
            m_rulesDetailLabel->setText(TQString());
    }
}

void MainWindow::onRuleRowDoubleClicked(int modelRow, int col)
{
    (void)col;
    if (!m_rulesTable || !m_rulesModel)
        return;
    if (m_rulesInDetail)
        return;

    if (modelRow < 0)
        return;

    const TQString ruleName = m_rulesModel->data(modelRow, RULEMVC_COL_NAME).toString().stripWhiteSpace();
    const TQString node = m_rulesModel->data(modelRow, RULEMVC_COL_NODE).toString().stripWhiteSpace();
    if (ruleName.isEmpty() || node.isEmpty())
        return;

    setRulesDetailView(node, ruleName, 1);
    refreshRuleConnections(ruleName, node);
    if (m_filterEdit && m_rulesConnView)
        m_rulesConnView->setFilterText(m_filterEdit->text());
}

void MainWindow::onRuleRowDoubleClicked4(int row, int col, int button, const TQPoint& p)
{
    (void)button;
    (void)p;
    if (!m_rulesTable)
        return;
    const int modelRow = m_rulesTable->modelRow(row);
    onRuleRowDoubleClicked(modelRow, col);
}

void MainWindow::onRuleDetailBackClicked()
{
    setRulesDetailView(TQString(), TQString(), 0);
    if (m_tabs && m_tabs->currentPageIndex() == TAB_RULES)
        refreshRules();
}

void MainWindow::onRuleDetailEditClicked()
{
    if (!m_rulesInDetail)
        return;
    if (m_rulesDetailName.isEmpty() || m_rulesDetailNode.isEmpty())
        return;

    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;

    TQValueList<TQVariant> binds;
    binds << m_rulesDetailName << m_rulesDetailNode;
    TQSqlQuery q = db->query("SELECT * FROM rules WHERE name=? AND node=?", binds);
    if (!q.next())
        return;

    protocol::Rule r;
    r.set_name(m_rulesDetailName.latin1());
    r.set_enabled(q.value(3).toString() == "True");
    r.set_precedence(q.value(4).toString() == "True");
    r.set_action(q.value(5).toString().latin1());
    r.set_duration(q.value(6).toString().latin1());
    protocol::Operator* op = r.mutable_operator_();
    op->set_type(q.value(7).toString().latin1());
    op->set_sensitive(q.value(8).toString() == "True");
    op->set_operand(q.value(9).toString().latin1());
    op->set_data(q.value(10).toString().latin1());

    if (!m_ruleDlg)
        m_ruleDlg = new RuleDialog(this);
    m_ruleDlg->editRule(r, m_rulesDetailNode);
}

void MainWindow::onRuleDetailDeleteClicked()
{
    if (!m_rulesInDetail)
        return;
    if (m_rulesDetailName.isEmpty() || m_rulesDetailNode.isEmpty())
        return;

    int ret = TQMessageBox::warning(this,
                                   "Delete rule",
                                   TQString("    Delete rule %1?    ").arg(m_rulesDetailName),
                                   TQMessageBox::Ok,
                                   TQMessageBox::Cancel);
    if (ret != TQMessageBox::Ok)
        return;

    if (m_server) {
        protocol::Notification notif;
        notif.set_id((uint64_t)time(0));
        notif.set_type(protocol::DELETE_RULE);
        protocol::Rule* rr = notif.add_rules();
        if (rr)
            rr->set_name(m_rulesDetailName.latin1());
        m_server->sendNotification(m_rulesDetailNode, notif);
    }

    Rules::instance()->remove(m_rulesDetailName, m_rulesDetailNode);

    // Return to list view after deletion (Python does refresh)
    setRulesDetailView(TQString(), TQString(), 0);
    if (m_tabs && m_tabs->currentPageIndex() == TAB_RULES)
        refreshRules();
}

void MainWindow::refreshRuleConnections(const TQString& ruleName, const TQString& node)
{
    if (!m_rulesConnModel)
        return;
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Identical to Python stats.py::_set_rules_query()
    // SELECT MAX(c.time), c.node, count(c.process), c.uid, c.protocol, c.dst_port,
    // CASE c.dst_host WHEN '' THEN c.dst_ip ELSE c.dst_host END, c.process,
    // c.process_args, c.process_cwd
    // FROM connections as c
    // WHERE c.rule=? AND c.node=?
    // GROUP BY c.process, c.process_args, c.uid, <destination expr>, c.dst_port
    // ORDER BY ... LIMIT ...
    TQString sql =
        "SELECT MAX(c.time) AS time, "
        "c.node AS node, "
        "COUNT(c.process) AS cons, "
        "c.uid AS uid, "
        "c.protocol AS protocol, "
        "c.dst_port AS dst_port, "
        "CASE c.dst_host WHEN '' THEN c.dst_ip ELSE c.dst_host END AS destination, "
        "c.process AS process, "
        "c.process_args AS process_args, "
        "c.process_cwd AS cwd "
        "FROM connections AS c "
        "WHERE c.rule=? AND c.node=? "
        "GROUP BY c.process, c.process_args, c.uid, destination, c.dst_port "
        "ORDER BY time DESC ";

    TQValueList<TQVariant> binds;
    binds << ruleName;
    binds << node;
    TQSqlQuery q = db->query(sql, binds);

    m_rulesConnModel->beginBatch();
    m_rulesConnModel->clear();
    int loaded = 0;
    while (q.next() && loaded < limit) {
        TQtRow r(RULEDET_COL_COUNT);
        r[RULEDET_COL_TIME]        = q.value(0).toString();
        r[RULEDET_COL_NODE]        = q.value(1).toString();
        r[RULEDET_COL_HITS]        = q.value(2).toString();
        r[RULEDET_COL_UID]         = q.value(3).toString();
        r[RULEDET_COL_PROTOCOL]    = q.value(4).toString();
        r[RULEDET_COL_DST_PORT]    = q.value(5).toString();
        r[RULEDET_COL_DESTINATION] = q.value(6).toString();
        r[RULEDET_COL_PROCESS]     = q.value(7).toString();
        r[RULEDET_COL_PROC_ARGS]   = q.value(8).toString();
        r[RULEDET_COL_CWD]         = q.value(9).toString();
        m_rulesConnModel->appendRow(r);
        ++loaded;
    }
    m_rulesConnModel->endBatch();
}

void MainWindow::onRulesContextMenu(int modelRow, int, const TQPoint& globalPos)
{
    if (m_rulesInDetail)
        return;
    if (!m_rulesModel || modelRow < 0)
        return;

    const TQString ruleName = m_rulesModel->data(modelRow, RULEMVC_COL_NAME).toString().stripWhiteSpace();
    const TQString node = m_rulesModel->data(modelRow, RULEMVC_COL_NODE).toString().stripWhiteSpace();
    const TQString enabledStr = m_rulesModel->data(modelRow, RULEMVC_COL_ACTIVE).toString().stripWhiteSpace();
    if (ruleName.isEmpty() || node.isEmpty())
        return;

    const int isEnabled = (enabledStr == "True") ? 1 : 0;

    enum {
        MENU_ID_APPLY_BASE = 1000,
        MENU_ID_ACT_ALLOW = 2000,
        MENU_ID_ACT_DENY = 2001,
        MENU_ID_ACT_REJECT = 2002,
        MENU_ID_DUR_ALWAYS = 2100,
        MENU_ID_DUR_UNTIL_REBOOT = 2101,
        MENU_ID_DUR_1H = 2102,
        MENU_ID_DUR_12H = 2103,
        MENU_ID_DUR_30M = 2104,
        MENU_ID_DUR_15M = 2105,
        MENU_ID_DUR_5M = 2106,
        MENU_ID_ENABLE = 2200,
        MENU_ID_DUPLICATE = 2300,
        MENU_ID_EDIT = 2400,
        MENU_ID_DELETE = 2500
    };

    TQPopupMenu menu(this);
    TQPopupMenu* nodesMenu = new TQPopupMenu(&menu);
    TQPopupMenu* actionMenu = new TQPopupMenu(&menu);
    TQPopupMenu* durMenu = new TQPopupMenu(&menu);

    // Apply to nodes (Python: "Apply to")
    TQMap<int, TQString> nodeActionToAddr;
    {
        const TQMap<TQString, Nodes::NodeData>& nm = Nodes::instance()->nodes();
        int nid = 0;
        for (TQMap<TQString, Nodes::NodeData>::ConstIterator it = nm.begin(); it != nm.end(); ++it) {
            const TQString n = it.key();
            int id = MENU_ID_APPLY_BASE + nid;
            nodesMenu->insertItem(n, id);
            nodeActionToAddr[id] = n;
            ++nid;
        }
        if (!nm.isEmpty())
            menu.insertItem("Apply to", nodesMenu);
    }

    const int idActAllow = actionMenu->insertItem("Allow", MENU_ID_ACT_ALLOW);
    const int idActDeny = actionMenu->insertItem("Deny", MENU_ID_ACT_DENY);
    const int idActReject = actionMenu->insertItem("Reject", MENU_ID_ACT_REJECT);
    menu.insertItem("Action", actionMenu);

    const int idDurAlways = durMenu->insertItem("Always", MENU_ID_DUR_ALWAYS);
    const int idDurUntilReboot = durMenu->insertItem("Until reboot", MENU_ID_DUR_UNTIL_REBOOT);
    const int idDur1h = durMenu->insertItem("1h", MENU_ID_DUR_1H);
    const int idDur12h = durMenu->insertItem("12h", MENU_ID_DUR_12H);
    const int idDur30m = durMenu->insertItem("30m", MENU_ID_DUR_30M);
    const int idDur15m = durMenu->insertItem("15m", MENU_ID_DUR_15M);
    const int idDur5m = durMenu->insertItem("5m", MENU_ID_DUR_5M);
    menu.insertItem("Duration", durMenu);

    const int idEnable = menu.insertItem(isEnabled ? "Disable" : "Enable", MENU_ID_ENABLE);
    const int idDuplicate = menu.insertItem("Duplicate", MENU_ID_DUPLICATE);
    const int idEdit = menu.insertItem("Edit", MENU_ID_EDIT);
    const int idDelete = menu.insertItem("Delete", MENU_ID_DELETE);

    const int actionId = menu.exec(globalPos);
    if (actionId < 0)
        return;

    // Apply-to node
    if (nodeActionToAddr.contains(actionId)) {
        const TQString dstNode = nodeActionToAddr[actionId];

        int ret = TQMessageBox::warning(this,
                                       "Apply rule",
                                       TQString("    Apply this rule to %1    ").arg(dstNode),
                                       TQMessageBox::Ok,
                                       TQMessageBox::Cancel);
        if (ret != TQMessageBox::Ok)
            return;

        // Apply the selected rule record to target node.
        Database* db = Database::instance();
        if (!db || !db->isOpen())
            return;
        TQValueList<TQVariant> binds;
        binds << ruleName << node;
        TQSqlQuery q = db->query("SELECT * FROM rules WHERE name=? AND node=?", binds);
        if (!q.next())
            return;

        protocol::Rule r;
        r.set_name(ruleName.latin1());
        r.set_enabled(q.value(3).toString() == "True");
        r.set_precedence(q.value(4).toString() == "True");
        r.set_action(q.value(5).toString().latin1());
        r.set_duration(q.value(6).toString().latin1());
        protocol::Operator* op = r.mutable_operator_();
        op->set_type(q.value(7).toString().latin1());
        op->set_sensitive(q.value(8).toString() == "True");
        op->set_operand(q.value(9).toString().latin1());
        op->set_data(q.value(10).toString().latin1());

        // Send and also insert into local DB (like Python)
        if (m_server) {
            protocol::Notification notif;
            notif.set_id((uint64_t)time(0));
            notif.set_type(protocol::CHANGE_RULE);
            protocol::Rule* rr = notif.add_rules();
            if (rr)
                rr->CopyFrom(r);
            m_server->sendNotification(dstNode, notif);
        }
        Rules::instance()->add(TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), dstNode, r);
        return;
    }

    // Helper to send CHANGE_RULE with updated action/duration/precedence
    auto sendChangeRule = [&](const TQString& field, const TQString& value) {
        Database* db = Database::instance();
        if (!db || !db->isOpen())
            return;

        // Update DB first (Python does DB update then notification)
        TQValueList<TQVariant> vals;
        vals << TQVariant(value) << TQVariant(ruleName) << TQVariant(node);
        db->update("rules", field + "=?", vals, "name=? AND node=?");

        // Fetch updated rule row
        TQValueList<TQVariant> binds;
        binds << ruleName << node;
        TQSqlQuery q = db->query("SELECT * FROM rules WHERE name=? AND node=?", binds);
        if (!q.next())
            return;

        protocol::Rule r;
        r.set_name(ruleName.latin1());
        r.set_enabled(q.value(3).toString() == "True");
        r.set_precedence(q.value(4).toString() == "True");
        r.set_action(q.value(5).toString().latin1());
        r.set_duration(q.value(6).toString().latin1());
        protocol::Operator* op = r.mutable_operator_();
        op->set_type(q.value(7).toString().latin1());
        op->set_sensitive(q.value(8).toString() == "True");
        op->set_operand(q.value(9).toString().latin1());
        op->set_data(q.value(10).toString().latin1());

        // Sync local Rules store (DB insert is REPLACE due to UNIQUE(node,name)).
        Rules::instance()->add(TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), node, r);

        if (m_server) {
            protocol::Notification notif;
            notif.set_id((uint64_t)time(0));
            notif.set_type(protocol::CHANGE_RULE);
            protocol::Rule* rr = notif.add_rules();
            if (rr)
                rr->CopyFrom(r);
            m_server->sendNotification(node, notif);
        }
    };

    if (actionId == idDelete) {
        // Same as toolbar delete
        if (m_server) {
            protocol::Notification notif;
            notif.set_id((uint64_t)time(0));
            notif.set_type(protocol::DELETE_RULE);
            protocol::Rule* r = notif.add_rules();
            if (r) {
                r->set_name(ruleName.latin1());
                r->set_enabled(false);
                r->set_action("");
                r->set_duration("");
                r->mutable_operator_()->set_type("");
                r->mutable_operator_()->set_operand("");
                r->mutable_operator_()->set_data("");
            }
            m_server->sendNotification(node, notif);
        }
        Rules::instance()->remove(ruleName, node);
        return;
    }
    if (actionId == idEnable) {
        const int wantEnabled = isEnabled ? 0 : 1;
        Rules::instance()->setEnabled(ruleName, node, wantEnabled ? true : false);
        if (m_server) {
            protocol::Notification notif;
            notif.set_id((uint64_t)time(0));
            notif.set_type(wantEnabled ? protocol::ENABLE_RULE : protocol::DISABLE_RULE);
            protocol::Rule* rr = notif.add_rules();
            if (rr)
                rr->set_name(ruleName.latin1());
            m_server->sendNotification(node, notif);
        }
        return;
    }
    if (actionId == idDuplicate) {
        // Python: clone rule with name "cloned-<idx>-<name>" and send CHANGE_RULE
        Database* db = Database::instance();
        if (!db || !db->isOpen())
            return;
        TQValueList<TQVariant> binds;
        binds << ruleName << node;
        TQSqlQuery q = db->query("SELECT * FROM rules WHERE name=? AND node=?", binds);
        if (!q.next())
            return;

        protocol::Rule r;
        r.set_enabled(q.value(3).toString() == "True");
        r.set_precedence(q.value(4).toString() == "True");
        r.set_action(q.value(5).toString().latin1());
        r.set_duration(q.value(6).toString().latin1());
        protocol::Operator* op = r.mutable_operator_();
        op->set_type(q.value(7).toString().latin1());
        op->set_sensitive(q.value(8).toString() == "True");
        op->set_operand(q.value(9).toString().latin1());
        op->set_data(q.value(10).toString().latin1());

        TQString newName;
        for (int i = 0; i < 100; ++i) {
            newName = TQString("cloned-%1-%2").arg(i).arg(ruleName);
            TQValueList<TQVariant> b;
            b << newName << node;
            TQSqlQuery exists = db->query("SELECT name FROM rules WHERE name=? AND node=?", b);
            if (!exists.next())
                break;
            newName = TQString();
        }
        if (newName.isEmpty())
            return;
        r.set_name(newName.latin1());

        Rules::instance()->add(TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"), node, r);
        if (m_server) {
            protocol::Notification notif;
            notif.set_id((uint64_t)time(0));
            notif.set_type(protocol::CHANGE_RULE);
            protocol::Rule* rr = notif.add_rules();
            if (rr)
                rr->CopyFrom(r);
            m_server->sendNotification(node, notif);
        }
        return;
    }
    if (actionId == idEdit) {
        Database* db = Database::instance();
        if (!db || !db->isOpen())
            return;

        TQValueList<TQVariant> binds;
        binds << ruleName << node;
        TQSqlQuery q = db->query("SELECT * FROM rules WHERE name=? AND node=?", binds);
        if (!q.next())
            return;

        protocol::Rule r;
        r.set_name(ruleName.latin1());
        r.set_enabled(q.value(3).toString() == "True");
        r.set_precedence(q.value(4).toString() == "True");
        r.set_action(q.value(5).toString().latin1());
        r.set_duration(q.value(6).toString().latin1());
        protocol::Operator* op = r.mutable_operator_();
        op->set_type(q.value(7).toString().latin1());
        op->set_sensitive(q.value(8).toString() == "True");
        op->set_operand(q.value(9).toString().latin1());
        op->set_data(q.value(10).toString().latin1());

        if (!m_ruleDlg)
            m_ruleDlg = new RuleDialog(this);
        m_ruleDlg->editRule(r, node);
        return;
    }

    if (actionId == idActAllow) sendChangeRule("action", Config::ACTION_ALLOW);
    else if (actionId == idActDeny) sendChangeRule("action", Config::ACTION_DENY);
    else if (actionId == idActReject) sendChangeRule("action", Config::ACTION_REJECT);
    else if (actionId == idDurAlways) sendChangeRule("duration", Config::DURATION_ALWAYS);
    else if (actionId == idDurUntilReboot) sendChangeRule("duration", Config::DURATION_UNTIL_RESTART);
    else if (actionId == idDur1h) sendChangeRule("duration", Config::DURATION_1H);
    else if (actionId == idDur12h) sendChangeRule("duration", Config::DURATION_12H);
    else if (actionId == idDur30m) sendChangeRule("duration", Config::DURATION_30M);
    else if (actionId == idDur15m) sendChangeRule("duration", Config::DURATION_15M);
    else if (actionId == idDur5m) sendChangeRule("duration", Config::DURATION_5M);
}

void MainWindow::setupNodesTab()
{
    m_nodesTab = new TQWidget(m_tabs);

    TQVBoxLayout* nodesLay = new TQVBoxLayout(m_nodesTab, 0, 0);

    m_nodesInDetail = 0;
    m_nodesDetailNode = TQString();

    // Detail bar (hidden by default)
    m_nodesDetailBar = new TQWidget(m_nodesTab);
    TQHBoxLayout* detailLay = new TQHBoxLayout(m_nodesDetailBar, 2, 4);
    m_nodesBackBtn = new TQPushButton("", m_nodesDetailBar);
    m_nodesBackBtn->setFixedSize(26, 24);
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
        if (!pm.isNull())
            m_nodesBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }
    detailLay->addWidget(m_nodesBackBtn);
    m_nodesDetailLabel = new TQLabel(m_nodesDetailBar);
    detailLay->addWidget(m_nodesDetailLabel, 1);
    m_nodesDetailBar->hide();
    nodesLay->addWidget(m_nodesDetailBar);
    connect(m_nodesBackBtn, SIGNAL(clicked()), this, SLOT(onNodeDetailBackClicked()));

    // Node connections MVC view (detail view, hidden by default)
    m_nodesConnModel = new TQtListStore(12, this);
    m_nodesConnModel->setHeader(0,  "Time");
    m_nodesConnModel->setHeader(1,  "Action");
    m_nodesConnModel->setHeader(2,  "Connections");
    m_nodesConnModel->setHeader(3,  "User ID");
    m_nodesConnModel->setHeader(4,  "Protocol");
    m_nodesConnModel->setHeader(5,  "DstIP");
    m_nodesConnModel->setHeader(6,  "DstHost");
    m_nodesConnModel->setHeader(7,  "DstPort");
    m_nodesConnModel->setHeader(8,  "Process");
    m_nodesConnModel->setHeader(9,  "Args");
    m_nodesConnModel->setHeader(10, "CWD");
    m_nodesConnModel->setHeader(11, "Rule");

    // Action colors (match Python: allow=green, deny=red, reject=purple)
    m_nodesConnModel->addStyleRule(TQtStyleRule("allow", 1, TQtCellStyle().setForeground(TQColor(0x00, 0x80, 0x00))));
    m_nodesConnModel->addStyleRule(TQtStyleRule("deny",  1, TQtCellStyle().setForeground(TQColor(0xFF, 0x00, 0x00))));
    m_nodesConnModel->addStyleRule(TQtStyleRule("reject",1, TQtCellStyle().setForeground(TQColor(0x7F, 0x00, 0xFF))));

    m_nodesConnView = new TQtMvcTableView(m_nodesTab);
    m_nodesConnView->setModel(m_nodesConnModel);
    m_nodesConnView->setSortingEnabled(true);
    m_nodesConnView->hide();
    nodesLay->addWidget(m_nodesConnView, 1);

    // Nodes list MVC view
    m_nodesModel = new TQtListStore(NODE_COL_COUNT, this);
    m_nodesModel->setHeader(NODE_COL_LAST_CONN,   "Last Connection");
    m_nodesModel->setHeader(NODE_COL_ADDR,        "Address");
    m_nodesModel->setHeader(NODE_COL_STATUS,      "Status");
    m_nodesModel->setHeader(NODE_COL_HOSTNAME,    "Hostname");
    m_nodesModel->setHeader(NODE_COL_VERSION,     "Version");
    m_nodesModel->setHeader(NODE_COL_UPTIME,      "Uptime");
    m_nodesModel->setHeader(NODE_COL_RULES,       "Rules");
    m_nodesModel->setHeader(NODE_COL_CONNECTIONS, "Connections");
    m_nodesModel->setHeader(NODE_COL_DROPPED,     "Dropped");
    m_nodesModel->setHeader(NODE_COL_KERNEL,      "Kernel Version");

    // Status colors
    m_nodesModel->addStyleRule(TQtStyleRule("online", NODE_COL_STATUS,
        TQtCellStyle().setForeground(TQColor(0x00, 0x80, 0x00))));

    m_nodesModel->setColumnStyle(NODE_COL_DROPPED, TQtCellStyle().setForeground(TQColor(0xCC, 0x00, 0x00)));

    m_nodesTable = new TQtMvcTableView(m_nodesTab);
    m_nodesTable->setModel(m_nodesModel);
    m_nodesTable->setSortingEnabled(true);
    m_nodesTable->setColumnWidth(NODE_COL_LAST_CONN,   140);
    m_nodesTable->setColumnWidth(NODE_COL_ADDR,        130);
    m_nodesTable->setColumnWidth(NODE_COL_STATUS,       80);
    m_nodesTable->setColumnWidth(NODE_COL_HOSTNAME,    120);
    m_nodesTable->setColumnWidth(NODE_COL_VERSION,      80);
    m_nodesTable->setColumnWidth(NODE_COL_UPTIME,       80);
    m_nodesTable->setColumnWidth(NODE_COL_RULES,        60);
    m_nodesTable->setColumnWidth(NODE_COL_CONNECTIONS,  90);
    m_nodesTable->setColumnWidth(NODE_COL_DROPPED,      80);
    m_nodesTable->setColumnWidth(NODE_COL_KERNEL,      160);

    nodesLay->addWidget(m_nodesTable, 1);

    connect(m_nodesTable, SIGNAL(doubleClicked(int,int,int,const TQPoint&)),
            this, SLOT(onNodeRowDoubleClicked(int,int,int,const TQPoint&)));
}

void MainWindow::setNodesDetailView(const TQString& node, int on)
{
    m_nodesInDetail = on ? 1 : 0;
    if (!on)
        m_nodesDetailNode = TQString();
    else
        m_nodesDetailNode = node;

    if (m_nodesDetailBar)
        m_nodesDetailBar->setShown(on ? true : false);
    if (m_nodesConnView)
        m_nodesConnView->setShown(on ? true : false);
    if (m_nodesTable)
        m_nodesTable->setShown(on ? false : true);

    if (on && m_nodesDetailLabel) {
        TQString lbl = node;
        if (lbl.isEmpty())
            lbl = "unix:/local";
        m_nodesDetailLabel->setText(lbl);
    }
}

void MainWindow::onNodeRowDoubleClicked(int row, int col, int button, const TQPoint& p)
{
    (void)col;
    (void)button;
    (void)p;
    if (!m_nodesTable)
        return;
    if (m_nodesInDetail)
        return;

    // Nodes list is an MVC view: 'row' is a display row, map it to model row.
    int mRow = m_nodesTable ? m_nodesTable->modelRow(row) : -1;
    TQString node = (mRow >= 0 && m_nodesModel)
        ? m_nodesModel->data(mRow, NODE_COL_ADDR).toString().stripWhiteSpace()
        : TQString();
    if (node.isEmpty())
        return;
    setNodesDetailView(node, 1);
    refreshNodeConnections(node);
}

void MainWindow::onNodeDetailBackClicked()
{
    setNodesDetailView(TQString(), 0);
    if (m_tabs && m_tabs->currentPageIndex() == TAB_NODES)
        refreshNodes();
}

void MainWindow::refreshNodeConnections(const TQString& node)
{
    if (!m_nodesConnModel)
        return;
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Python detail view groups by displayed fields and shows a "Connections" count.
    // Note: Database::select() ignores custom fields in this codebase; use a raw SQL query.
    TQString sql =
        "SELECT MAX(time) AS time, "
        "action AS action, "
        "COUNT(*) AS cons, "
        "uid AS uid, "
        "protocol AS protocol, "
        "dst_ip AS dst_ip, "
        "dst_host AS dst_host, "
        "dst_port AS dst_port, "
        "process AS process, "
        "process_args AS process_args, "
        "process_cwd AS process_cwd, "
        "rule AS rule "
        "FROM connections "
        "WHERE node=? "
        "GROUP BY action, uid, protocol, dst_ip, dst_host, dst_port, process, process_args, process_cwd, rule "
        "ORDER BY time DESC ";

    TQValueList<TQVariant> binds;
    binds << node;
    TQSqlQuery q = db->query(sql, binds);

    m_nodesConnModel->beginBatch();
    m_nodesConnModel->clear();
    int loaded = 0;
    while (q.next() && loaded < limit) {
        const TQString ruleName = q.value(11).toString();
        if (isInternalUiRuleName(ruleName))
            continue;
        TQtRow r(12);
        r[0]  = q.value(0).toString();
        r[1]  = q.value(1).toString();
        r[2]  = q.value(2).toString();
        r[3]  = q.value(3).toString();
        r[4]  = q.value(4).toString();
        r[5]  = q.value(5).toString();
        r[6]  = q.value(6).toString();
        r[7]  = q.value(7).toString();
        r[8]  = q.value(8).toString();
        r[9]  = q.value(9).toString();
        r[10] = q.value(10).toString();
        r[11] = ruleName;
        m_nodesConnModel->appendRow(r);
        ++loaded;
    }
    m_nodesConnModel->endBatch();

    if (m_filterEdit && m_nodesConnView)
        m_nodesConnView->setFilterText(m_filterEdit->text());
}

void MainWindow::setupGenericTab(TQWidget** tab, TQTable** table, int cols,
                                  const char** headers, const int* widths)
{
    *tab = new TQWidget(m_tabs);
    TQVBoxLayout* lay = new TQVBoxLayout(*tab, 0, 0);

    *table = new TQTable(*tab);
    (*table)->setNumCols(cols);
    (*table)->setNumRows(0);
    (*table)->setSelectionMode(TQTable::SingleRow);
    (*table)->setReadOnly(true);
    (*table)->setSorting(true);
    (*table)->setFocusStyle(TQTable::FollowStyle);
    (*table)->setLeftMargin(0);
    (*table)->setHScrollBarMode(TQScrollView::Auto);
    (*table)->setVScrollBarMode(TQScrollView::Auto);

    TQHeader* hdr = (*table)->horizontalHeader();
    for (int i = 0; i < cols; i++) {
        hdr->setLabel(i, headers[i]);
        (*table)->setColumnWidth(i, widths[i]);
    }

    lay->addWidget(*table, 1);
}

void MainWindow::setupFilterBar()
{
    m_filterBar = new TQFrame(this);
    m_filterBar->setFrameShape(TQFrame::StyledPanel);
    m_filterBar->setFrameShadow(TQFrame::Raised);

    TQPalette barPal = m_filterBar->palette();
    barPal.setColor(TQColorGroup::Background, TQColor(0xF0, 0xF0, 0xF0));
    m_filterBar->setPalette(barPal);

    TQHBoxLayout* lay = new TQHBoxLayout(m_filterBar, 2, 4);

    TQLabel* filterLbl = new TQLabel("Filter", m_filterBar);
    lay->addWidget(filterLbl);

    // Action filter combo: -, Allow, Deny, Reject (matches Python comboAction)
    m_actionCombo = new TQComboBox(m_filterBar);
    m_actionCombo->insertItem("-");
    m_actionCombo->insertItem(loadIcon("emblem-default", 16), "Allow");
    m_actionCombo->insertItem(loadIcon("emblem-important", 16), "Deny");
    m_actionCombo->insertItem(loadIcon("window-close", 16), "Reject");
    connect(m_actionCombo, SIGNAL(activated(int)), this, SLOT(onActionComboChanged(int)));
    lay->addWidget(m_actionCombo);

    m_filterEdit = new TQLineEdit(m_filterBar);
    m_filterEdit->setSizePolicy(TQSizePolicy(TQSizePolicy::Expanding, TQSizePolicy::Fixed));
    connect(m_filterEdit, SIGNAL(textChanged(const TQString&)),
             this, SLOT(onFilterTextChanged(const TQString&)));
    lay->addWidget(m_filterEdit, 1);

    m_clearFilterBtn = new TQToolButton(m_filterBar);
    applyEmbeddedIconPx(m_clearFilterBtn, dialog_close_png, (int)dialog_close_png_len, logsToolbarIconPx());
    m_clearFilterBtn->setTextLabel("Clear filter");
    m_clearFilterBtn->setAutoRaise(true);
    m_clearFilterBtn->setFixedSize(logsToolbarIconPx(), logsToolbarIconPx());
    connect(m_clearFilterBtn, SIGNAL(clicked()), m_filterEdit, SLOT(clear()));
    lay->addWidget(m_clearFilterBtn);

    // Spacer between filter and limit
    lay->addSpacing(20);

    m_limitCombo = new TQComboBox(m_filterBar);
    m_limitCombo->insertItem("50");
    m_limitCombo->insertItem("100");
    m_limitCombo->insertItem("200");
    m_limitCombo->insertItem("300");
    connect(m_limitCombo, SIGNAL(activated(int)), this, SLOT(onLimitComboChanged(int)));
    lay->addWidget(m_limitCombo);

    // Clean stats button: deletes all events from DB (matches Python cmdCleanSql)
    m_clearStatsBtn = new TQToolButton(m_filterBar);
    applyEmbeddedIconPx(m_clearStatsBtn, edit_clear_png, (int)edit_clear_png_len, logsToolbarIconPx());
    m_clearStatsBtn->setTextLabel("Delete all intercepted events");
    m_clearStatsBtn->setAutoRaise(true);
    connect(m_clearStatsBtn, SIGNAL(clicked()), this, SLOT(onCleanStatsClicked()));
    lay->addWidget(m_clearStatsBtn);

    m_freezeLogsBtn = new TQToolButton(m_filterBar);
    applyEmbeddedIconPx(m_freezeLogsBtn, media_playback_pause_png, (int)media_playback_pause_png_len, logsToolbarIconPx());
    m_freezeLogsBtn->setTextLabel("Freeze logs");
    m_freezeLogsBtn->setUsesTextLabel(false);
    m_freezeLogsBtn->setToggleButton(true);
    m_freezeLogsBtn->setAutoRaise(true);
    m_freezeLogsBtn->setFixedSize(logsToolbarIconPx(), logsToolbarIconPx());
    connect(m_freezeLogsBtn, SIGNAL(clicked()), this, SLOT(onFreezeLogsToggled()));
    lay->addWidget(m_freezeLogsBtn);

    m_helpBtn = new TQToolButton(m_filterBar);
    applyEmbeddedIconPx(m_helpBtn, quickhelp_png, (int)quickhelp_png_len, logsToolbarIconPx());
    m_helpBtn->setTextLabel("Help");
    m_helpBtn->setAutoRaise(true);
    connect(m_helpBtn, SIGNAL(clicked()), this, SLOT(onHelpClicked()));
    lay->addWidget(m_helpBtn);

    applyLogsToolbarSizing(m_filterBar, logsToolbarIconPx());

    m_mainLay->addWidget(m_filterBar);
}

void MainWindow::setupStatusBar()
{
    TQWidget* statusBar = new TQWidget(this);
    TQHBoxLayout* lay = new TQHBoxLayout(statusBar, 2, 6);

    m_statsLabel = new TQLabel("Connections 0  Dropped 0  Uptime 0h 0m  Rules 0", statusBar);
    m_statsLabel->setAlignment(TQt::AlignLeft | TQt::AlignVCenter);
    TQFont statsFont = m_statsLabel->font();
    statsFont.setPointSize(statsFont.pointSize() - 1);
    m_statsLabel->setFont(statsFont);

    m_versionLabel = new TQLabel("Version -", statusBar);
    m_versionLabel->setAlignment(TQt::AlignRight | TQt::AlignVCenter);
    m_versionLabel->setFont(statsFont);

    lay->addWidget(m_statsLabel, 1);
    lay->addWidget(m_versionLabel);

    m_mainLay->addWidget(statusBar);
}

void MainWindow::reloadIcons()
{
    {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(opensnitch_icon_png, (int)opensnitch_icon_png_len, 32);
        if (!pm.isNull())
            setIcon(pm);
    }
    if (m_saveBtn) applyEmbeddedIcon(m_saveBtn, document_save_png, (int)document_save_png_len);
    if (m_refreshBtn) applyEmbeddedIcon(m_refreshBtn, preferences_desktop_png, (int)preferences_desktop_png_len);
    if (m_aboutBtn) applyEmbeddedIcon(m_aboutBtn, about_png, (int)about_png_len);
    if (m_quitBtn) applyEmbeddedIcon(m_quitBtn, quit_png, (int)quit_png_len);
    if (m_newBtn) applyEmbeddedIcon(m_newBtn, document_new_png, (int)document_new_png_len);
    if (m_pauseBtn) {
        m_pauseBtn->setIconSet(TQIconSet());
        if (m_interceptionEnabled)
            applyEmbeddedIcon(m_pauseBtn, media_playback_pause_png, (int)media_playback_pause_png_len);
        else
            applyEmbeddedIcon(m_pauseBtn, media_playback_start_png, (int)media_playback_start_png_len);
    }
    {
        const int lpx = logsToolbarIconPx();
        if (m_clearFilterBtn) {
            m_clearFilterBtn->setFixedSize(lpx, lpx);
            applyEmbeddedIconPx(m_clearFilterBtn, dialog_close_png, (int)dialog_close_png_len, lpx);
        }
        if (m_clearStatsBtn) {
            m_clearStatsBtn->setFixedSize(lpx, lpx);
            applyEmbeddedIconPx(m_clearStatsBtn, edit_clear_png, (int)edit_clear_png_len, lpx);
        }
        if (m_freezeLogsBtn) {
            m_freezeLogsBtn->setFixedSize(lpx, lpx);
            m_freezeLogsBtn->setIconSet(TQIconSet());
            if (m_logsFrozen)
                applyEmbeddedIconPx(m_freezeLogsBtn, media_playback_start_png, (int)media_playback_start_png_len, lpx);
            else
                applyEmbeddedIconPx(m_freezeLogsBtn, media_playback_pause_png, (int)media_playback_pause_png_len, lpx);
        }
        if (m_helpBtn) {
            m_helpBtn->setFixedSize(lpx, lpx);
            applyEmbeddedIconPx(m_helpBtn, quickhelp_png, (int)quickhelp_png_len, lpx);
        }
        applyLogsToolbarSizing(m_filterBar, lpx);
    }

    {
        TQPixmap pm;
        if (m_rulesBackBtn) {
            pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
            if (!pm.isNull())
                m_rulesBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_rulesEditBtn) {
            pm = IconTheme::loadEmbeddedPixmap(edit_png, (int)edit_png_len, 16);
            if (!pm.isNull())
                m_rulesEditBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_rulesDeleteBtn) {
            pm = IconTheme::loadEmbeddedPixmap(trash_png, (int)trash_png_len, 16);
            if (!pm.isNull())
                m_rulesDeleteBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_nodesBackBtn) {
            pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
            if (!pm.isNull())
                m_nodesBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_hostsBackBtn) {
            pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
            if (!pm.isNull())
                m_hostsBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_procsBackBtn) {
            pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
            if (!pm.isNull())
                m_procsBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_addrsBackBtn) {
            pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
            if (!pm.isNull())
                m_addrsBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_portsBackBtn) {
            pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
            if (!pm.isNull())
                m_portsBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_usersBackBtn) {
            pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
            if (!pm.isNull())
                m_usersBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_delRuleBtn) {
            const int tbpx = toolbarIconPx();
            m_delRuleBtn->setFixedSize(tbpx, tbpx);
            pm = IconTheme::loadEmbeddedPixmap(trash_png, (int)trash_png_len, tbpx);
            if (!pm.isNull())
                m_delRuleBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
    }

    {
        const int tbpx = toolbarIconPx();
        if (m_newRuleBtn) {
            m_newRuleBtn->setFixedSize(tbpx, tbpx);
            TQPixmap pm = IconTheme::loadEmbeddedPixmap(document_new_png, (int)document_new_png_len, tbpx);
            if (!pm.isNull())
                m_newRuleBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_toggleRuleBtn) {
            m_toggleRuleBtn->setFixedSize(tbpx, tbpx);
            TQPixmap pm = IconTheme::loadEmbeddedPixmap(toggle_png, (int)toggle_png_len, tbpx);
            if (!pm.isNull())
                m_toggleRuleBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
        if (m_delRuleBtn) {
            m_delRuleBtn->setFixedSize(tbpx, tbpx);
            TQPixmap pm = IconTheme::loadEmbeddedPixmap(trash_png, (int)trash_png_len, tbpx);
            if (!pm.isNull())
                m_delRuleBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
        }
    }

    if (m_tabs) {
        m_tabs->changeTab(m_eventsTab, loadEmbeddedIcon(events_png, (int)events_png_len), "Events");
        m_tabs->changeTab(m_nodesTab, loadEmbeddedIcon(nodes_png, (int)nodes_png_len), "Nodes");
        m_tabs->changeTab(m_rulesTab, loadEmbeddedIcon(rules_png, (int)rules_png_len), "Rules");
        m_tabs->changeTab(m_hostsTab, loadEmbeddedIcon(hosts_png, (int)hosts_png_len), "Hosts");
        m_tabs->changeTab(m_procsTab, loadEmbeddedIcon(applications_png, (int)applications_png_len), "Applications");
        m_tabs->changeTab(m_addrsTab, loadEmbeddedIcon(addr_png, (int)addr_png_len), "Addresses");
        m_tabs->changeTab(m_portsTab, loadEmbeddedIcon(ports_png, (int)ports_png_len), "Ports");
        m_tabs->changeTab(m_usersTab, loadEmbeddedIcon(users_png, (int)users_png_len), "Users");
    }

    if (m_nodesBackBtn) {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(back_png, (int)back_png_len, 16);
        if (!pm.isNull())
            m_nodesBackBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }

    if (m_procsDetailsBtn) {
        TQPixmap pm = IconTheme::loadEmbeddedPixmap(system_search_png, (int)system_search_png_len, 16);
        if (!pm.isNull())
            m_procsDetailsBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    }

    emit iconsThemeChanged();
}

void MainWindow::fullRefreshEvents()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;

    applyEventsColumnsFromConfig();

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Load all events from DB (no WHERE clause — MVC view handles filtering)
    TQSqlCursor* cur = db->select("connections", "*", TQString(), "time DESC", limit);
    if (!cur)
        return;

    // Save selection
    int selModelRow = m_eventsTable->selectedModelRow();
    TQString selKey;
    if (selModelRow >= 0 && selModelRow < m_eventsModel->rowCount()) {
        // Stable key to re-identify the selected row even if new rows are prepended.
        // Use visible columns (same as model) as a composite key.
        // Note: connections table has no numeric id.
        selKey = m_eventsModel->data(selModelRow, EVT_COL_TIME).toString() + "\x1f" +
                 m_eventsModel->data(selModelRow, EVT_COL_NODE).toString() + "\x1f" +
                 m_eventsModel->data(selModelRow, EVT_COL_ACTION).toString() + "\x1f" +
                 m_eventsModel->data(selModelRow, EVT_COL_DEST).toString() + "\x1f" +
                 m_eventsModel->data(selModelRow, EVT_COL_PROTO).toString() + "\x1f" +
                 m_eventsModel->data(selModelRow, EVT_COL_PROCESS).toString() + "\x1f" +
                 m_eventsModel->data(selModelRow, EVT_COL_RULE).toString();
    }

    // Batch-load into model (single modelReset at end)
    m_eventsModel->beginBatch();
    m_eventsModel->clear();
    int loaded = 0;
    int loadedSel = -1;
    while (cur->next() && loaded < limit) {
        TQtRow row(EVT_COL_COUNT);
        row[EVT_COL_TIME]    = cur->value("time").toString();
        row[EVT_COL_NODE]    = cur->value("node").toString();
        row[EVT_COL_ACTION]  = cur->value("action").toString();
        row[EVT_COL_DEST]    = cur->value("dst_host").toString();
        row[EVT_COL_PROTO]   = cur->value("protocol").toString();
        row[EVT_COL_PROCESS] = cur->value("process").toString();
        row[EVT_COL_RULE]    = cur->value("rule").toString();

        if (isInternalUiRuleName(row[EVT_COL_RULE].toString()))
            continue;

        if (loadedSel < 0 && !selKey.isEmpty()) {
            TQString key = row[EVT_COL_TIME].toString() + "\x1f" +
                           row[EVT_COL_NODE].toString() + "\x1f" +
                           row[EVT_COL_ACTION].toString() + "\x1f" +
                           row[EVT_COL_DEST].toString() + "\x1f" +
                           row[EVT_COL_PROTO].toString() + "\x1f" +
                           row[EVT_COL_PROCESS].toString() + "\x1f" +
                           row[EVT_COL_RULE].toString();
            if (key == selKey)
                loadedSel = loaded;
        }
        m_eventsModel->appendRow(row);
        ++loaded;
    }
    m_eventsModel->endBatch();
    delete cur;

    // Apply MVC view filters (action combo + text filter)
    applyEventsFilter();

    // Restore selection
    int newSel = loadedSel;
    if (newSel < 0 && selModelRow >= 0 && selModelRow < m_eventsModel->rowCount())
        newSel = selModelRow;
    if (newSel >= 0) {
        m_eventsTable->selectModelRow(newSel);
        m_eventsTable->ensureModelRowVisible(newSel);
    }
}

void MainWindow::refreshEvents()
{
    if (m_logsFrozen)
        return;
    // MVC handles filtering client-side — just reload from DB
    fullRefreshEvents();
}

void MainWindow::onFreezeLogsToggled()
{
    m_logsFrozen = !m_logsFrozen;

    if (m_freezeLogsBtn) {
        m_freezeLogsBtn->setIconSet(TQIconSet());
        if (m_logsFrozen)
            applyEmbeddedIcon(m_freezeLogsBtn, media_playback_start_png, (int)media_playback_start_png_len);
        else
            applyEmbeddedIcon(m_freezeLogsBtn, media_playback_pause_png, (int)media_playback_pause_png_len);
    }

    if (m_eventsTable) {
        TQHeader* hdr = m_eventsTable->horizontalHeader();
        if (hdr) {
            if (!m_eventsViewportPalSaved) {
                m_eventsViewportPal = hdr->palette();
                m_eventsViewportPalSaved = 1;
            }

            if (m_logsFrozen) {
                TQPalette pal = m_eventsViewportPal;
                pal.setColor(TQColorGroup::Background, TQColor(0xD0, 0xD0, 0xD0));
                hdr->setPalette(pal);
            } else {
                if (m_eventsViewportPalSaved)
                    hdr->setPalette(m_eventsViewportPal);
                refreshEvents();
            }

            hdr->update();
        }
    }

    updateStats(m_connectionsCount, m_droppedCount, m_uptimeStr, m_rulesCount);
}

void MainWindow::applyEventsFilter()
{
    // Action combo → column filter on EVT_COL_ACTION (DB stores lowercase)
    int actionIdx = m_actionCombo->currentItem();
    if (actionIdx == 1)
        m_eventsTable->setColumnFilter(EVT_COL_ACTION, "allow");
    else if (actionIdx == 2)
        m_eventsTable->setColumnFilter(EVT_COL_ACTION, "deny");
    else if (actionIdx == 3)
        m_eventsTable->setColumnFilter(EVT_COL_ACTION, "reject");
    else
        m_eventsTable->setColumnFilter(-1, TQString());  // no column filter

    // Text filter → global search across all columns
    m_eventsTable->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshRules()
{
    Rules* rules = Rules::instance();
    const TQMap<TQString, Rules::RuleRecord>& ruleMap = rules->rules();
    int filterIdx = m_rulesFilterCombo->currentItem();

    // Left tree filters
    TQString nodeFilter;
    int applyFilter = -1; // -1=all, 0=permanent, 1=temporary
    if (m_rulesTree) {
        TQListViewItem* sel = m_rulesTree->selectedItem();
        if (sel && sel->parent()) {
            if (sel->parent()->text(0) == "Nodes") {
                nodeFilter = sel->text(0);
            } else if (sel->parent()->text(0) == "Application rules") {
                if (sel->text(0) == "Permanent") applyFilter = 0;
                else if (sel->text(0) == "Temporary") applyFilter = 1;
            }
        }
    }

    m_rulesModel->beginBatch();
    m_rulesModel->clear();

    for (TQMap<TQString, Rules::RuleRecord>::ConstIterator it = ruleMap.begin();
         it != ruleMap.end(); ++it) {
        const Rules::RuleRecord& r = it.data();

        // Hide internal UI rules.
        if (r.name.startsWith("__ui_internal__"))
            continue;

        if (!nodeFilter.isEmpty() && r.node != nodeFilter)
            continue;

        if (applyFilter == 0) {
            if (r.duration != "always")
                continue;
        } else if (applyFilter == 1) {
            if (r.duration == "always")
                continue;
        }

        if (filterIdx == 1 && r.action != "allow") continue;
        if (filterIdx == 2 && r.action != "deny") continue;
        if (filterIdx == 3 && r.action != "reject") continue;

        TQtRow row(RULEMVC_COL_COUNT);
        row[RULEMVC_COL_TIME]         = r.time;
        row[RULEMVC_COL_NODE]         = r.node;
        row[RULEMVC_COL_NAME]         = r.name;
        row[RULEMVC_COL_ACTIVE]       = r.enabled ? "True" : "False";
        row[RULEMVC_COL_PRECEDENCE]   = r.precedence ? "True" : "False";
        row[RULEMVC_COL_ACTION]       = r.action;
        row[RULEMVC_COL_DURATION]     = r.duration;
        row[RULEMVC_COL_OP_TYPE]      = r.opType;
        row[RULEMVC_COL_OP_SENSITIVE] = r.opSensitive ? "True" : "False";
        row[RULEMVC_COL_OP_OPERAND]   = r.opOperand;
        row[RULEMVC_COL_OP_DATA]      = pretty_operator_data(r.opType, r.opOperand, r.opData);
        m_rulesModel->appendRow(row);
    }

    m_rulesModel->endBatch();
}

void MainWindow::updateRulesTreeNodes()
{
    if (!m_rulesTree)
        return;

    TQString selText;
    {
        TQListViewItem* sel = m_rulesTree->selectedItem();
        if (sel && sel->parent() && sel->parent()->text(0) == "Nodes")
            selText = sel->text(0);
    }

    disconnect(m_rulesTree, SIGNAL(selectionChanged(TQListViewItem*)),
               this, SLOT(onRulesTreeSelectionChanged(TQListViewItem*)));

    TQListViewItem* nodesRoot = 0;
    for (TQListViewItem* itn = m_rulesTree->firstChild(); itn; itn = itn->nextSibling()) {
        if (itn->text(0) == "Nodes") { nodesRoot = itn; break; }
    }
    if (nodesRoot) {
        while (nodesRoot->firstChild())
            delete nodesRoot->firstChild();

        Nodes* nodes = Nodes::instance();
        const TQMap<TQString, Nodes::NodeData>& nm = nodes->nodes();
        TQListViewItem* toSelect = 0;
        for (TQMap<TQString, Nodes::NodeData>::ConstIterator nit = nm.begin(); nit != nm.end(); ++nit) {
            TQListViewItem* item = new TQListViewItem(nodesRoot, nit.key());
            if (!selText.isEmpty() && nit.key() == selText)
                toSelect = item;
        }
        nodesRoot->setOpen(true);
        if (toSelect)
            m_rulesTree->setSelected(toSelect, true);
    }

    connect(m_rulesTree, SIGNAL(selectionChanged(TQListViewItem*)),
            this, SLOT(onRulesTreeSelectionChanged(TQListViewItem*)));
}

void MainWindow::onRulesTreeSelectionChanged(TQListViewItem*)
{
    refreshRules();
}

void MainWindow::refreshNodes()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;

    if (!m_nodesModel)
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    TQSqlCursor* cur = db->select("nodes", "*", TQString(), "status DESC, addr ASC", limit);
    if (!cur)
        return;

    m_nodesModel->beginBatch();
    m_nodesModel->clear();
    int loaded = 0;
    while (cur->next() && loaded < limit) {
        TQtRow r(NODE_COL_COUNT);
        r[NODE_COL_LAST_CONN]   = cur->value("last_connection").toString();
        r[NODE_COL_ADDR]        = cur->value("addr").toString();
        r[NODE_COL_STATUS]      = cur->value("status").toString();
        r[NODE_COL_HOSTNAME]    = cur->value("hostname").toString();
        r[NODE_COL_VERSION]     = cur->value("daemon_version").toString();
        r[NODE_COL_UPTIME]      = cur->value("daemon_uptime").toString();
        r[NODE_COL_RULES]       = cur->value("daemon_rules").toString();
        r[NODE_COL_CONNECTIONS] = cur->value("cons").toString();
        r[NODE_COL_DROPPED]     = cur->value("cons_dropped").toString();
        r[NODE_COL_KERNEL]      = cur->value("version").toString();
        m_nodesModel->appendRow(r);
        ++loaded;
    }

    m_nodesModel->endBatch();
    delete cur;

    if (m_filterEdit && m_nodesTable)
        m_nodesTable->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshProcs()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_procsModel)
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Matches Python: TAB_PROCS list is table 'procs' with columns what,hits ordered by hits
    TQValueList<TQVariant> binds;
    TQSqlQuery q = db->query("SELECT what, hits FROM procs ORDER BY hits DESC LIMIT ?", binds << TQVariant(limit));

    m_procsModel->beginBatch();
    m_procsModel->clear();
    while (q.next()) {
        TQtRow r(2);
        r[0] = q.value(0).toString();
        r[1] = q.value(1).toString();
        m_procsModel->appendRow(r);
    }
    m_procsModel->endBatch();

    if (m_filterEdit && m_procsTable)
        m_procsTable->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshAddrs()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_addrsModel)
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    TQValueList<TQVariant> binds;
    TQSqlQuery q = db->query("SELECT what, hits FROM addrs ORDER BY hits DESC LIMIT ?",
                             binds << TQVariant(limit));

    m_addrsModel->beginBatch();
    m_addrsModel->clear();
    while (q.next()) {
        TQtRow r(3);
        const TQString ip = q.value(0).toString();
        r[0] = ip;
        r[1] = q.value(1).toString();
        r[2] = m_internalDnsRuleCreated ? "Resolving..." : TQString();
        m_addrsModel->appendRow(r);

        if (!m_internalDnsRuleCreated)
            continue;

        NetIdentity ni;
        if (NetIdentityResolver::instance()->lookup(ip, ni)) {
            if (!ni.provider.isEmpty())
                m_addrsModel->setData(m_addrsModel->rowCount() - 1, 2, ni.provider);
            else if (!ni.hostname.isEmpty())
                m_addrsModel->setData(m_addrsModel->rowCount() - 1, 2, ni.hostname);
            else
                m_addrsModel->setData(m_addrsModel->rowCount() - 1, 2, TQString());
        } else {
            NetIdentityResolver::instance()->request(ip);
        }
    }
    m_addrsModel->endBatch();

    if (m_filterEdit && m_addrsTable)
        m_addrsTable->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshAddrConnections(const TQString& addr)
{
    if (addr.isEmpty())
        return;
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_addrsConnModel)
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Matches Python StatsDialog::_set_addrs_query
    TQString sql =
        "SELECT "
        "MAX(c.time) AS time, "
        "c.node AS node, "
        "count(c.dst_ip) AS hits, "
        "c.action AS action, "
        "c.uid AS uid, "
        "c.protocol AS protocol, "
        "CASE c.dst_host WHEN '' "
        "   THEN c.dst_ip "
        "   ELSE c.dst_host "
        "END AS destination, "
        "c.dst_port AS dst_port, "
        "c.process || ' (' || c.pid || ')' AS process, "
        "c.process_args AS process_args, "
        "c.process_cwd AS cwd, "
        "c.rule AS rule "
        "FROM connections AS c "
        "WHERE c.dst_ip = ? "
        "GROUP BY c.pid, c.process, c.process_args, c.src_ip, c.dst_port, destination, c.protocol, c.action, c.uid, c.node "
        "ORDER BY time DESC "
        "LIMIT ?";

    TQValueList<TQVariant> binds;
    binds << TQVariant(addr) << TQVariant(limit);
    TQSqlQuery q = db->query(sql, binds);

    m_addrsConnModel->beginBatch();
    m_addrsConnModel->clear();
    while (q.next()) {
        const TQString ruleName = q.value(11).toString();
        if (isInternalUiRuleName(ruleName))
            continue;
        TQtRow r(12);
        r[0]  = q.value(0).toString();
        r[1]  = q.value(1).toString();
        r[2]  = q.value(2).toString();
        r[3]  = q.value(3).toString();
        r[4]  = q.value(4).toString();
        r[5]  = q.value(5).toString();
        r[6]  = q.value(6).toString();
        r[7]  = q.value(7).toString();
        r[8]  = q.value(8).toString();
        r[9]  = q.value(9).toString();
        r[10] = q.value(10).toString();
        r[11] = ruleName;
        m_addrsConnModel->appendRow(r);
    }
    m_addrsConnModel->endBatch();

    if (m_filterEdit && m_addrsConnView)
        m_addrsConnView->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshPorts()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_portsModel)
        return;

    TQString limitText;
    if (m_limitCombo)
        limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    TQValueList<TQVariant> binds;
    TQSqlQuery q = db->query("SELECT what, hits FROM ports ORDER BY hits DESC LIMIT ?",
                             binds << TQVariant(limit));

    m_portsModel->beginBatch();
    m_portsModel->clear();
    while (q.next()) {
        TQtRow r(2);
        r[0] = q.value(0).toString();
        r[1] = q.value(1).toString();
        m_portsModel->appendRow(r);
    }
    m_portsModel->endBatch();

    if (m_filterEdit && m_portsTable)
        m_portsTable->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshPortConnections(const TQString& port)
{
    if (port.isEmpty())
        return;
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_portsConnModel)
        return;

    TQString limitText;
    if (m_limitCombo)
        limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Matches Python StatsDialog::_set_ports_query
    TQString sql =
        "SELECT "
        "MAX(c.time) AS time, "
        "c.node AS node, "
        "count(c.dst_ip) AS hits, "
        "c.action AS action, "
        "c.uid AS uid, "
        "c.protocol AS protocol, "
        "c.dst_ip AS dst_ip, "
        "CASE c.dst_host WHEN '' "
        "   THEN c.dst_ip "
        "   ELSE c.dst_host "
        "END AS destination, "
        "c.process || ' (' || c.pid || ')' AS process, "
        "c.process_args AS process_args, "
        "c.process_cwd AS cwd, "
        "c.rule AS rule "
        "FROM connections AS c "
        "WHERE c.dst_port = ? "
        "GROUP BY c.pid, c.process, c.process_args, destination, c.src_ip, c.dst_ip, c.protocol, c.action, c.uid, c.node "
        "ORDER BY time DESC "
        "LIMIT ?";

    TQValueList<TQVariant> binds;
    binds << TQVariant(port) << TQVariant(limit);
    TQSqlQuery q = db->query(sql, binds);

    m_portsConnModel->beginBatch();
    m_portsConnModel->clear();
    while (q.next()) {
        const TQString ruleName = q.value(11).toString();
        if (isInternalUiRuleName(ruleName))
            continue;
        TQtRow r(12);
        r[0]  = q.value(0).toString();
        r[1]  = q.value(1).toString();
        r[2]  = q.value(2).toString();
        r[3]  = q.value(3).toString();
        r[4]  = q.value(4).toString();
        r[5]  = q.value(5).toString();
        r[6]  = q.value(6).toString();
        r[7]  = q.value(7).toString();
        r[8]  = q.value(8).toString();
        r[9]  = q.value(9).toString();
        r[10] = q.value(10).toString();
        r[11] = ruleName;
        m_portsConnModel->appendRow(r);
    }
    m_portsConnModel->endBatch();

    if (m_filterEdit && m_portsConnView)
        m_portsConnView->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshUsers()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_usersModel)
        return;

    TQString limitText;
    if (m_limitCombo)
        limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    TQValueList<TQVariant> binds;
    TQSqlQuery q = db->query("SELECT what, hits FROM users ORDER BY hits DESC LIMIT ?",
                             binds << TQVariant(limit));

    m_usersModel->beginBatch();
    m_usersModel->clear();
    while (q.next()) {
        TQtRow r(2);
        r[0] = formatUserUidDisplay(q.value(0).toString());
        r[1] = q.value(1).toString();
        m_usersModel->appendRow(r);
    }
    m_usersModel->endBatch();

    if (m_filterEdit && m_usersTableMvc)
        m_usersTableMvc->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshUserConnections(const TQString& user)
{
    if (user.isEmpty())
        return;
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_usersConnModel)
        return;

    // Python accepts "name (uid)". Extract uid if present.
    TQString uid = user;
    int p1 = uid.find('(');
    int p2 = uid.find(')');
    if (p1 >= 0 && p2 > p1)
        uid = uid.mid(p1 + 1, p2 - p1 - 1).stripWhiteSpace();
    else
        uid = uid.stripWhiteSpace();

    if (uid.isEmpty())
        return;

    TQString limitText;
    if (m_limitCombo)
        limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Matches Python StatsDialog::_set_users_query
    TQString sql =
        "SELECT "
        "MAX(c.time) AS time, "
        "c.uid AS uid, "
        "c.node AS node, "
        "count(c.dst_ip) AS hits, "
        "c.action AS action, "
        "c.protocol AS protocol, "
        "c.dst_ip AS dst_ip, "
        "c.dst_host AS destination, "
        "c.dst_port AS dst_port, "
        "c.process || ' (' || c.pid || ')' AS process, "
        "c.process_args AS process_args, "
        "c.process_cwd AS cwd, "
        "c.rule AS rule "
        "FROM connections AS c "
        "WHERE c.uid = ? "
        "GROUP BY c.pid, c.process, c.process_args, c.src_ip, c.dst_ip, c.dst_host, c.dst_port, c.protocol, c.action, c.node "
        "ORDER BY time DESC "
        "LIMIT ?";

    TQValueList<TQVariant> binds;
    binds << TQVariant(uid) << TQVariant(limit);
    TQSqlQuery q = db->query(sql, binds);

    m_usersConnModel->beginBatch();
    m_usersConnModel->clear();
    while (q.next()) {
        const TQString ruleName = q.value(12).toString();
        if (isInternalUiRuleName(ruleName))
            continue;
        TQtRow r(13);
        r[0]  = q.value(0).toString();
        r[1]  = formatUserUidDisplay(q.value(1).toString());
        r[2]  = q.value(2).toString();
        r[3]  = q.value(3).toString();
        r[4]  = q.value(4).toString();
        r[5]  = q.value(5).toString();
        r[6]  = q.value(6).toString();
        r[7]  = q.value(7).toString();
        r[8]  = q.value(8).toString();
        r[9]  = q.value(9).toString();
        r[10] = q.value(10).toString();
        r[11] = q.value(11).toString();
        r[12] = ruleName;
        m_usersConnModel->appendRow(r);
    }
    m_usersConnModel->endBatch();

    if (m_filterEdit && m_usersConnView)
        m_usersConnView->setFilterText(m_filterEdit->text());
}
void MainWindow::refreshHosts()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_hostsModel)
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    TQValueList<TQVariant> binds;
    TQSqlQuery q = db->query("SELECT what, hits FROM hosts ORDER BY hits DESC LIMIT ?", binds << TQVariant(limit));

    m_hostsModel->beginBatch();
    m_hostsModel->clear();
    int loaded = 0;
    while (q.next() && loaded < limit) {
        TQtRow r(2);
        r[0] = q.value(0).toString();
        r[1] = q.value(1).toString();
        m_hostsModel->appendRow(r);
        ++loaded;
    }
    m_hostsModel->endBatch();

    if (m_filterEdit && m_hostsTable)
        m_hostsTable->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshHostConnections(const TQString& host)
{
    if (host.isEmpty())
        return;
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_hostsConnModel)
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Matches Python StatsDialog::_set_hosts_query
    TQString sql =
        "SELECT "
        "MAX(c.time) AS time, "
        "c.node AS node, "
        "count(c.process) AS hits, "
        "c.action AS action, "
        "c.uid AS uid, "
        "c.protocol AS protocol, "
        "c.dst_port AS dst_port, "
        "c.dst_ip AS dst_ip, "
        "c.process || ' (' || c.pid || ')' AS process, "
        "c.process_args AS process_args, "
        "c.process_cwd AS cwd, "
        "c.rule AS rule "
        "FROM connections AS c "
        "WHERE c.dst_host = ? "
        "GROUP BY c.pid, c.process, c.process_args, c.src_ip, c.dst_ip, c.dst_port, c.protocol, c.action, c.node "
        "ORDER BY time DESC "
        "LIMIT ?";

    TQValueList<TQVariant> binds;
    binds << TQVariant(host) << TQVariant(limit);
    TQSqlQuery q = db->query(sql, binds);

    m_hostsConnModel->beginBatch();
    m_hostsConnModel->clear();
    int loaded = 0;
    while (q.next() && loaded < limit) {
        const TQString ruleName = q.value(11).toString();
        if (isInternalUiRuleName(ruleName))
            continue;
        TQtRow r(12);
        r[0]  = q.value(0).toString();
        r[1]  = q.value(1).toString();
        r[2]  = q.value(2).toString();
        r[3]  = q.value(3).toString();
        r[4]  = q.value(4).toString();
        r[5]  = q.value(5).toString();
        r[6]  = q.value(6).toString();
        r[7]  = q.value(7).toString();
        r[8]  = q.value(8).toString();
        r[9]  = q.value(9).toString();
        r[10] = q.value(10).toString();
        r[11] = ruleName;
        m_hostsConnModel->appendRow(r);
        ++loaded;
    }
    m_hostsConnModel->endBatch();

    if (m_filterEdit && m_hostsConnView)
        m_hostsConnView->setFilterText(m_filterEdit->text());
}

void MainWindow::refreshProcConnections(const TQString& procPath)
{
    if (procPath.isEmpty())
        return;
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;
    if (!m_procsConnModel)
        return;

    TQString limitText = m_limitCombo->currentText();
    int limit = limitText.toInt();
    if (limit <= 0) limit = 50;

    // Matches Python StatsDialog::_set_process_query
    TQString sql =
        "SELECT "
        "MAX(c.time) AS time, "
        "c.node AS node, "
        "count(c.dst_ip) AS hits, "
        "c.action AS action, "
        "c.uid AS uid, "
        "CASE c.dst_host WHEN '' "
        "   THEN c.dst_ip || '  ->  ' || c.dst_port "
        "   ELSE c.dst_host || '  ->  ' || c.dst_port "
        "END AS destination, "
        "c.pid AS pid, "
        "c.process_args AS process_args, "
        "c.process_cwd AS cwd, "
        "c.rule AS rule "
        "FROM connections AS c "
        "WHERE c.process = ? "
        "GROUP BY c.src_ip, c.dst_ip, c.dst_host, c.dst_port, c.uid, c.action, c.node, c.pid, c.process_args "
        "ORDER BY time DESC "
        "LIMIT ?";

    TQValueList<TQVariant> binds;
    binds << TQVariant(procPath) << TQVariant(limit);
    TQSqlQuery q = db->query(sql, binds);

    m_procsConnModel->beginBatch();
    m_procsConnModel->clear();
    int loaded = 0;
    while (q.next() && loaded < limit) {
        const TQString ruleName = q.value(9).toString();
        if (isInternalUiRuleName(ruleName))
            continue;
        TQtRow r(10);
        r[0] = q.value(0).toString();
        r[1] = q.value(1).toString();
        r[2] = q.value(2).toString();
        r[3] = q.value(3).toString();
        r[4] = q.value(4).toString();
        r[5] = q.value(5).toString();
        r[6] = q.value(6).toString();
        r[7] = q.value(7).toString();
        r[8] = q.value(8).toString();
        r[9] = ruleName;
        m_procsConnModel->appendRow(r);
        ++loaded;
    }
    m_procsConnModel->endBatch();

    // Python parity: cmdProcDetails is visible only if there are rows in detail view.
    if (m_procsDetailsBtn) {
        const int hasRows = (m_procsConnModel->rowCount() > 0) ? 1 : 0;
        m_procsDetailsBtn->setShown(hasRows ? true : false);
        m_procsDetailsBtn->setEnabled(false);
    }

    if (m_filterEdit && m_procsConnView)
        m_procsConnView->setFilterText(m_filterEdit->text());
}

void MainWindow::onHostRowDoubleClicked(int modelRow, int col)
{
    (void)col;
    if (!m_hostsTable || !m_hostsModel)
        return;
    if (m_hostsInDetail)
        return;
    if (modelRow < 0)
        return;

    const TQString what = m_hostsModel->data(modelRow, 0).toString().stripWhiteSpace();
    if (what.isEmpty())
        return;

    m_hostsInDetail = 1;
    m_hostsDetailWhat = what;
    if (m_hostsDetailLabel)
        m_hostsDetailLabel->setText(what);
    if (m_hostsDetailBar)
        m_hostsDetailBar->show();
    if (m_hostsTable)
        m_hostsTable->hide();
    if (m_hostsConnView)
        m_hostsConnView->show();

    refreshHostConnections(what);
}

void MainWindow::onHostRowDoubleClicked4(int row, int col, int button, const TQPoint& p)
{
    (void)button;
    (void)p;
    if (!m_hostsTable)
        return;
    const int modelRow = m_hostsTable->modelRow(row);
    onHostRowDoubleClicked(modelRow, col);
}

void MainWindow::onHostDetailBackClicked()
{
    m_hostsInDetail = 0;
    m_hostsDetailWhat = TQString();
    if (m_hostsDetailLabel)
        m_hostsDetailLabel->setText(TQString());
    if (m_hostsDetailBar)
        m_hostsDetailBar->hide();
    if (m_hostsConnView)
        m_hostsConnView->hide();
    if (m_hostsTable)
        m_hostsTable->show();

    refreshHosts();
}

void MainWindow::onProcRowDoubleClicked(int modelRow, int col)
{
    (void)col;
    if (!m_procsTable || !m_procsModel)
        return;
    if (m_procsInDetail)
        return;
    if (modelRow < 0)
        return;

    const TQString what = m_procsModel->data(modelRow, 0).toString().stripWhiteSpace();
    if (what.isEmpty())
        return;

    m_procsInDetail = 1;
    m_procsDetailWhat = what;
    if (m_procsDetailLabel)
        m_procsDetailLabel->setText(what);
    if (m_procsDetailBar)
        m_procsDetailBar->show();
    if (m_procsDetailsBtn)
        m_procsDetailsBtn->setEnabled(false);
    if (m_procsTable)
        m_procsTable->hide();
    if (m_procsConnView)
        m_procsConnView->show();

    refreshProcConnections(what);
}

void MainWindow::onProcConnSelectionChanged(int modelRow)
{
    if (!m_procsInDetail)
        return;
    if (!m_procsDetailsBtn)
        return;
    if (!m_procsConnModel)
        return;

    const int ok = (modelRow >= 0 && modelRow < m_procsConnModel->rowCount()) ? 1 : 0;
    m_procsDetailsBtn->setEnabled(ok ? true : false);
}

void MainWindow::onProcConnClicked4(int row, int col, int button, const TQPoint& p)
{
    (void)col;
    (void)button;
    (void)p;
    if (!m_procsConnView)
        return;
    const int modelRow = m_procsConnView->modelRow(row);
    onProcConnSelectionChanged(modelRow);
}

void MainWindow::onProcRowDoubleClicked4(int row, int col, int button, const TQPoint& p)
{
    (void)button;
    (void)p;
    if (!m_procsTable)
        return;
    const int modelRow = m_procsTable->modelRow(row);
    onProcRowDoubleClicked(modelRow, col);
}

void MainWindow::onProcDetailBackClicked()
{
    m_procsInDetail = 0;
    m_procsDetailWhat = TQString();
    if (m_procsDetailLabel)
        m_procsDetailLabel->setText(TQString());
    if (m_procsDetailBar)
        m_procsDetailBar->hide();
    if (m_procsDetailsBtn)
        m_procsDetailsBtn->setEnabled(false);
    if (m_procsConnView)
        m_procsConnView->hide();
    if (m_procsTable)
        m_procsTable->show();

    refreshProcs();
}

void MainWindow::onAddrRowDoubleClicked(int modelRow, int col)
{
    (void)col;
    if (!m_addrsTable || !m_addrsModel)
        return;
    if (m_addrsInDetail)
        return;
    if (modelRow < 0)
        return;

    const TQString what = m_addrsModel->data(modelRow, 0).toString().stripWhiteSpace();
    if (what.isEmpty())
        return;

    m_addrsInDetail = 1;
    m_addrsDetailWhat = what;
    if (m_addrsDetailLabel)
        m_addrsDetailLabel->setText(what);
    if (m_addrsDetailBar)
        m_addrsDetailBar->show();
    if (m_addrsTable)
        m_addrsTable->hide();
    if (m_addrsConnView)
        m_addrsConnView->show();

    refreshAddrConnections(what);
}

void MainWindow::onAddrRowDoubleClicked4(int row, int col, int button, const TQPoint& p)
{
    (void)button;
    (void)p;
    if (!m_addrsTable)
        return;
    const int modelRow = m_addrsTable->modelRow(row);
    onAddrRowDoubleClicked(modelRow, col);
}

void MainWindow::onAddrDetailBackClicked()
{
    m_addrsInDetail = 0;
    m_addrsDetailWhat = TQString();
    if (m_addrsDetailLabel)
        m_addrsDetailLabel->setText(TQString());
    if (m_addrsDetailBar)
        m_addrsDetailBar->hide();
    if (m_addrsConnView)
        m_addrsConnView->hide();
    if (m_addrsTable)
        m_addrsTable->show();

    refreshAddrs();
}

void MainWindow::onPortRowDoubleClicked(int modelRow, int col)
{
    (void)col;
    if (!m_portsTable || !m_portsModel)
        return;
    if (m_portsInDetail)
        return;
    if (modelRow < 0)
        return;

    const TQString what = m_portsModel->data(modelRow, 0).toString().stripWhiteSpace();
    if (what.isEmpty())
        return;

    m_portsInDetail = 1;
    m_portsDetailWhat = what;
    if (m_portsDetailLabel)
        m_portsDetailLabel->setText(what);
    if (m_portsDetailBar)
        m_portsDetailBar->show();
    if (m_portsTable)
        m_portsTable->hide();
    if (m_portsConnView)
        m_portsConnView->show();

    refreshPortConnections(what);
}

void MainWindow::onPortRowDoubleClicked4(int row, int col, int button, const TQPoint& p)
{
    (void)button;
    (void)p;
    if (!m_portsTable)
        return;
    const int modelRow = m_portsTable->modelRow(row);
    onPortRowDoubleClicked(modelRow, col);
}

void MainWindow::onPortDetailBackClicked()
{
    m_portsInDetail = 0;
    m_portsDetailWhat = TQString();
    if (m_portsDetailLabel)
        m_portsDetailLabel->setText(TQString());
    if (m_portsDetailBar)
        m_portsDetailBar->hide();
    if (m_portsConnView)
        m_portsConnView->hide();
    if (m_portsTable)
        m_portsTable->show();

    refreshPorts();
}

void MainWindow::onUserRowDoubleClicked(int modelRow, int col)
{
    (void)col;
    if (!m_usersTableMvc || !m_usersModel)
        return;
    if (m_usersInDetail)
        return;
    if (modelRow < 0)
        return;

    const TQString what = m_usersModel->data(modelRow, 0).toString().stripWhiteSpace();
    if (what.isEmpty())
        return;

    m_usersInDetail = 1;
    m_usersDetailWhat = what;
    if (m_usersDetailLabel)
        m_usersDetailLabel->setText(what);
    if (m_usersDetailBar)
        m_usersDetailBar->show();
    if (m_usersTableMvc)
        m_usersTableMvc->hide();
    if (m_usersConnView)
        m_usersConnView->show();

    refreshUserConnections(what);
}

void MainWindow::onUserRowDoubleClicked4(int row, int col, int button, const TQPoint& p)
{
    (void)button;
    (void)p;
    if (!m_usersTableMvc)
        return;
    const int modelRow = m_usersTableMvc->modelRow(row);
    onUserRowDoubleClicked(modelRow, col);
}

void MainWindow::onUserDetailBackClicked()
{
    m_usersInDetail = 0;
    m_usersDetailWhat = TQString();
    if (m_usersDetailLabel)
        m_usersDetailLabel->setText(TQString());
    if (m_usersDetailBar)
        m_usersDetailBar->hide();
    if (m_usersConnView)
        m_usersConnView->hide();
    if (m_usersTableMvc)
        m_usersTableMvc->show();

    refreshUsers();
}

void MainWindow::onProcDetailsClicked()
{
    if (!m_procsInDetail)
        return;
    if (!m_procsConnView || !m_procsConnModel)
        return;
    if (!m_procDetailsDlg)
        m_procDetailsDlg = new ProcessDetailsDialog(m_server, this);
    if (!m_procDetailsDlg)
        return;

    // Python parity: pass all visible PIDs of this app (pid -> node).
    // Button is enabled only when a row is selected, but the dialog receives all pids.
    TQMap<TQString, TQString> pids;
    const int rows = m_procsConnModel->rowCount();
    for (int i = 0; i < rows; ++i) {
        const TQString node = m_procsConnModel->data(i, 1).toString().stripWhiteSpace();
        const TQString pid  = m_procsConnModel->data(i, 6).toString().stripWhiteSpace();
        if (node.isEmpty() || pid.isEmpty())
            continue;
        if (!pids.contains(pid))
            pids.insert(pid, node);
    }
    if (pids.isEmpty())
        return;

    m_procDetailsDlg->monitor(pids);
}

void MainWindow::onNotificationReply(const TQString& peer, unsigned long long id, int code, const TQString& data)
{
    if (m_procDetailsDlg)
        m_procDetailsDlg->handleNotificationReply(peer, id, code, data);
}

void MainWindow::setDaemonConnected(bool connected)
{
    m_daemonConnected = connected;
    if (connected) {
        m_stateValueLabel->setText("Active");
        TQPalette pal = m_stateValueLabel->palette();
        pal.setColor(TQColorGroup::Foreground, TQColor(0, 0x99, 0));
        m_stateValueLabel->setPalette(pal);
    } else {
        m_stateValueLabel->setText("Disconnected");
        TQPalette pal = m_stateValueLabel->palette();
        pal.setColor(TQColorGroup::Foreground, TQColor(0xCC, 0, 0));
        m_stateValueLabel->setPalette(pal);
    }
}

void MainWindow::setInterceptionEnabled(bool enabled)
{
    m_interceptionEnabled = enabled;
    if (enabled) {
        m_stateValueLabel->setText("Active");
        TQPalette pal = m_stateValueLabel->palette();
        pal.setColor(TQColorGroup::Foreground, TQColor(0, 0x99, 0));
        m_stateValueLabel->setPalette(pal);
        m_pauseBtn->setIconSet(TQIconSet());
        applyEmbeddedIcon(m_pauseBtn, media_playback_pause_png, (int)media_playback_pause_png_len);
    } else {
        m_stateValueLabel->setText("Paused");
        TQPalette pal = m_stateValueLabel->palette();
        pal.setColor(TQColorGroup::Foreground, TQColor(0xCE, 0x5C, 0));
        m_stateValueLabel->setPalette(pal);
        m_pauseBtn->setIconSet(TQIconSet());
        applyEmbeddedIcon(m_pauseBtn, media_playback_start_png, (int)media_playback_start_png_len);
    }
}

void MainWindow::addEvent(const TQString& time, const TQString& node,
                          const TQString& action, const TQString& procPath,
                          const TQString& dstHost, const TQString& dstPort,
                          const TQString& proto)
{
    TQtRow row(EVT_COL_COUNT);
    row[EVT_COL_TIME]    = time;
    row[EVT_COL_NODE]    = node;
    row[EVT_COL_ACTION]  = action;
    row[EVT_COL_DEST]    = dstHost;
    row[EVT_COL_PROTO]   = proto;
    row[EVT_COL_PROCESS] = procPath;
    row[EVT_COL_RULE]    = TQString();
    m_eventsModel->appendRow(row);
}

void MainWindow::updateStats(int connections, int dropped, const TQString& uptime, int rules)
{
    m_connectionsCount = connections;
    m_droppedCount = dropped;
    m_uptimeStr = uptime;
    m_rulesCount = rules;

    TQString s = TQString("Connections <b>%1</b>  Dropped <b>%2</b>  Uptime <b>%3</b>  Rules <b>%4</b>")
                     .arg(connections).arg(dropped).arg(uptime).arg(rules);
    if (m_logsFrozen)
        s += " - <span style=\"color:#D07000\"><b>Logs Paused</b></span>";
    m_statsLabel->setText(s);
}

void MainWindow::onTabChanged(TQWidget*)
{
    int idx = m_tabs->currentPageIndex();

    // Action combo only visible on Events tab (matches Python behavior)
    if (idx == TAB_EVENTS)
        m_actionCombo->show();
    else
        m_actionCombo->hide();

    // Python parity: hide Clean button on Rules tab.
    if (m_clearStatsBtn)
        m_clearStatsBtn->setShown(idx == TAB_RULES ? false : true);

    switch (idx) {
    case TAB_EVENTS:  fullRefreshEvents(); break;
    case TAB_NODES:
        if (m_nodesInDetail)
            refreshNodeConnections(m_nodesDetailNode);
        else
            refreshNodes();
        break;
    case TAB_RULES:   refreshRules(); break;
    case TAB_HOSTS:
        if (m_hostsInDetail)
            refreshHostConnections(m_hostsDetailWhat);
        else
            refreshHosts();
        break;
    case TAB_PROCS:
        if (m_procsInDetail)
            refreshProcConnections(m_procsDetailWhat);
        else
            refreshProcs();
        break;
    case TAB_ADDRS:
        if (m_addrsInDetail)
            refreshAddrConnections(m_addrsDetailWhat);
        else
            refreshAddrs();
        break;
    case TAB_PORTS:
        if (m_portsInDetail)
            refreshPortConnections(m_portsDetailWhat);
        else
            refreshPorts();
        break;
    case TAB_USERS:
        if (m_usersInDetail)
            refreshUserConnections(m_usersDetailWhat);
        else
            refreshUsers();
        break;
    }
}

void MainWindow::onRefreshTimer()
{
    // Periodic stats label update only.
    // Full table refresh is driven by onStatsUpdated() from gRPC callbacks.
    // This timer ensures stats labels stay current even if no new events arrive.
    // Counters are updated from daemon stats via onStatsUpdated(), not from DB.

    // Update daemon version (Python UI shows daemon version, not kernel).
    // Prefer the value from DB nodes table because Nodes::NodeData only stores ClientConfig.
    {
        Database* db = Database::instance();
        if (db && db->isOpen()) {
            TQValueList<TQVariant> binds;
            TQSqlQuery q = db->query("SELECT daemon_version FROM nodes ORDER BY last_connection DESC LIMIT 1", binds);
            if (q.next()) {
                const TQString dv = q.value(0).toString().stripWhiteSpace();
                if (!dv.isEmpty()) {
                    m_versionLabel->setText(TQString("Daemon %1").arg(dv));
                    return;
                }
            }
        }
    }
    m_versionLabel->setText("Daemon -");
}

void MainWindow::onDeleteRuleClicked()
{
    if (!m_rulesTable || !m_rulesModel)
        return;

    int mrow = m_rulesTable->selectedModelRow();
    if (mrow < 0 || mrow >= m_rulesModel->rowCount())
        return;

    TQString name = m_rulesModel->data(mrow, RULEMVC_COL_NAME).toString();
    TQString node = m_rulesModel->data(mrow, RULEMVC_COL_NODE).toString();

    if (TQMessageBox::question(this, "Delete Rule",
            TQString("Delete rule '%1'?").arg(name),
            TQMessageBox::Yes, TQMessageBox::No) == TQMessageBox::Yes) {
        if (m_server) {
            protocol::Notification notif;
            notif.set_id((uint64_t)time(0));
            notif.set_type(protocol::DELETE_RULE);
            protocol::Rule* r = notif.add_rules();
            if (r) {
                r->set_name(name.latin1());
            }
            m_server->sendNotification(node, notif);
        }
        Rules::instance()->remove(name, node);
    }
}

void MainWindow::onToggleRuleClicked()
{
    if (!m_rulesTable || !m_rulesModel)
        return;

    int mrow = m_rulesTable->selectedModelRow();
    if (mrow < 0 || mrow >= m_rulesModel->rowCount())
        return;

    TQString name = m_rulesModel->data(mrow, RULEMVC_COL_NAME).toString();
    TQString node = m_rulesModel->data(mrow, RULEMVC_COL_NODE).toString();

    Rules::RuleRecord* r = Rules::instance()->get(name, node);
    if (r) {
        const bool wantEnabled = !r->enabled;

        if (m_server) {
            protocol::Notification notif;
            notif.set_id((uint64_t)time(0));
            notif.set_type(wantEnabled ? protocol::ENABLE_RULE : protocol::DISABLE_RULE);
            protocol::Rule* rr = notif.add_rules();
            if (rr)
                rr->set_name(name.latin1());
            m_server->sendNotification(node, notif);
        }

        Rules::instance()->setEnabled(name, node, wantEnabled);
    }
}

void MainWindow::onRulesUpdated()
{
    if (m_tabs->currentPageIndex() == TAB_RULES)
        refreshRules();
}

void MainWindow::onNodesUpdated(int total)
{
    if (m_rulesTree)
        updateRulesTreeNodes();

    if (m_tabs->currentPageIndex() == TAB_NODES) {
        if (m_nodesInDetail)
            refreshNodeConnections(m_nodesDetailNode);
        else
            refreshNodes();
    }
}

void MainWindow::onStatsUpdated(bool needRefresh, int connections, int dropped, int rules, const TQString& uptime)
{
    // Update counters from daemon stats (matches Python: consLabel.setText(stats.connections))
    updateStats(connections, dropped, uptime, rules);

    // Ensure internal DNS rule exists before any background resolution triggers prompts.
    // Retry here because the first Subscribe event can arrive before m_server is wired
    // or before Nodes list is populated.
    if (!m_internalDnsRuleCreated && m_daemonConnected && m_server) {
        Nodes* nodes = Nodes::instance();
        if (nodes && nodes->count() > 0) {
            TQMap<TQString, Nodes::NodeData>::ConstIterator it = nodes->nodes().begin();
            if (it != nodes->nodes().end())
                ensureInternalDnsRule(it.key());
        }
    }

    if (!needRefresh)
        return;

    // No throttle — Python UI refreshes on every stats update without delay.
    // Skipping the first refresh would lose initial events from the daemon.

    // Refresh the currently visible tab (matches Python: stats_dialog.update())
    int idx = m_tabs->currentPageIndex();
    switch (idx) {
    case TAB_EVENTS:  refreshEvents(); break;
    case TAB_NODES:
        if (m_nodesInDetail)
            refreshNodeConnections(m_nodesDetailNode);
        else
            refreshNodes();
        break;
    case TAB_RULES:   refreshRules(); break;
    case TAB_HOSTS:
        if (m_hostsInDetail)
            refreshHostConnections(m_hostsDetailWhat);
        else
            refreshHosts();
        break;
    case TAB_PROCS:   refreshProcs(); break;
    case TAB_ADDRS:   refreshAddrs(); break;
    case TAB_PORTS:   refreshPorts(); break;
    case TAB_USERS:   refreshUsers(); break;
    }
}

void MainWindow::onDaemonSubscribed(const TQString& peer, bool firewallRunning)
{
    // Matches Python: read isFirewallRunning from ClientConfig on Subscribe
    // and update the interception status accordingly
    m_daemonConnected = true;
    setInterceptionEnabled(firewallRunning);
    // Notify tray icon of firewall state
    emit interceptionToggled(firewallRunning);

    ensureInternalDnsRule(peer);
}

void MainWindow::onTrayToggle()
{
    // Matches Python: _on_tray_icon_activated(Trigger)
    if (isVisible() && !isMinimized()) {
        hide();
    } else if (isMinimized()) {
        // Restore to previous state (normal or maximized)
        if (isMaximized())
            showMaximized();
        else
            showNormal();
        raise();
    } else {
        show();
        raise();
    }
}

void MainWindow::onInterceptionToggled()
{
    if (!m_daemonConnected) {
        m_pauseBtn->setOn(false);
        return;
    }

    // Toggle: if currently enabled, we want to stop; if stopped, start
    bool wantEnable = !m_interceptionEnabled;

    // Update UI immediately
    setInterceptionEnabled(wantEnable);

    // Send notification to daemon
    if (m_server) {
        protocol::Notification notif;
        notif.set_id((uint64_t)time(0));
        if (wantEnable)
            notif.set_type(protocol::LOAD_FIREWALL);
        else
            notif.set_type(protocol::UNLOAD_FIREWALL);
        m_server->broadcastNotification(notif);
    }

    emit interceptionToggled(wantEnable);
}

void MainWindow::onPreferencesClicked()
{
    if (!m_prefsDlg)
        m_prefsDlg = new PreferencesDialog(this, this);
    m_prefsDlg->show();
    m_prefsDlg->raise();
}

void MainWindow::onSaveClicked()
{
    int idx = m_tabs->currentPageIndex();
    TQString tblName;
    if (idx == TAB_EVENTS)
        tblName = "events";
    else
        return;

    TQString filename = TQFileDialog::getSaveFileName(
        tblName + ".csv",
        "All Files (*);;CSV Files (*.csv)",
        this,
        0,
        "Save as CSV");
    filename = filename.stripWhiteSpace();
    if (filename.isEmpty())
        return;

    TQFile f(filename);
    if (!f.open(IO_WriteOnly | IO_Truncate))
        return;

    TQTextStream out(&f);

    if (idx == TAB_EVENTS) {
        auto csvEscape = [](const TQString& s) -> TQString {
            bool needQuotes = false;
            for (int i = 0; i < (int)s.length(); ++i) {
                const TQChar c = s[i];
                if (c == ',' || c == '"' || c == '\n' || c == '\r') {
                    needQuotes = true;
                    break;
                }
            }
            if (!needQuotes)
                return s;
            TQString t = s;
            t.replace("\"", "\"\"");
            return TQString("\"") + t + "\"";
        };

        TQStringList headers;
        for (int c = 0; c < EVT_COL_COUNT; ++c)
            headers << csvEscape(m_eventsModel->headerData(c));
        out << headers.join(",") << "\n";

        Database* db = Database::instance();
        if (!db || !db->isOpen()) {
            f.close();
            return;
        }

        TQSqlCursor* cur = db->select("connections", "*", TQString(), "time DESC", 0);
        if (!cur) {
            f.close();
            return;
        }

        TQString actionFilter;
        if (m_actionCombo && m_actionCombo->isVisible()) {
            int aidx = m_actionCombo->currentItem();
            if (aidx == 1) actionFilter = "allow";
            else if (aidx == 2) actionFilter = "deny";
        }

        TQString textFilter;
        if (m_filterEdit)
            textFilter = m_filterEdit->text().stripWhiteSpace().lower();

        while (cur->next()) {
            TQString timeStr = cur->value("time").toString();
            TQString nodeStr = cur->value("node").toString();
            TQString actionStr = cur->value("action").toString();
            TQString destStr = cur->value("dst_host").toString();
            TQString protoStr = cur->value("protocol").toString();
            TQString procStr = cur->value("process").toString();
            TQString ruleStr = cur->value("rule").toString();

            if (isInternalUiRuleName(ruleStr))
                continue;

            if (!actionFilter.isEmpty() && actionStr != actionFilter)
                continue;

            if (!textFilter.isEmpty()) {
                TQString hay = (timeStr + " " + nodeStr + " " + actionStr + " " + destStr + " " +
                               protoStr + " " + procStr + " " + ruleStr).lower();
                if (hay.find(textFilter) < 0)
                    continue;
            }

            TQStringList cols;
            cols << csvEscape(timeStr)
                 << csvEscape(nodeStr)
                 << csvEscape(actionStr)
                 << csvEscape(destStr)
                 << csvEscape(protoStr)
                 << csvEscape(procStr)
                 << csvEscape(ruleStr);
            out << cols.join(",") << "\n";
        }

        delete cur;
    }

    f.close();
}

void MainWindow::onNewClicked()
{
    if (!m_ruleDlg)
        m_ruleDlg = new RuleDialog(this);
    m_ruleDlg->show();
    m_ruleDlg->raise();
}

void MainWindow::onFilterTextChanged(const TQString&)
{
    int idx = m_tabs->currentPageIndex();
    if (idx == TAB_EVENTS) {
        // MVC live filter — no DB query needed
        m_eventsTable->setFilterText(m_filterEdit->text());
    } else if (idx == TAB_RULES) {
        if (m_rulesInDetail) {
            if (m_rulesConnView)
                m_rulesConnView->setFilterText(m_filterEdit->text());
        } else {
            if (m_rulesTable)
                m_rulesTable->setFilterText(m_filterEdit->text());
        }
    } else if (idx == TAB_NODES) {
        if (m_nodesInDetail) {
            if (m_nodesConnView)
                m_nodesConnView->setFilterText(m_filterEdit->text());
        } else {
            if (m_nodesTable)
                m_nodesTable->setFilterText(m_filterEdit->text());
        }
    } else if (idx == TAB_HOSTS) {
        if (m_hostsInDetail) {
            if (m_hostsConnView)
                m_hostsConnView->setFilterText(m_filterEdit->text());
        } else {
            if (m_hostsTable)
                m_hostsTable->setFilterText(m_filterEdit->text());
        }
    }
}

void MainWindow::onActionComboChanged(int)
{
    if (m_tabs->currentPageIndex() == TAB_EVENTS) {
        // MVC column filter — no DB query needed (DB stores lowercase)
        int actionIdx = m_actionCombo->currentItem();
        if (actionIdx == 1)
            m_eventsTable->setColumnFilter(EVT_COL_ACTION, "allow");
        else if (actionIdx == 2)
            m_eventsTable->setColumnFilter(EVT_COL_ACTION, "deny");
        else if (actionIdx == 3)
            m_eventsTable->setColumnFilter(EVT_COL_ACTION, "reject");
        else
            m_eventsTable->setColumnFilter(-1, TQString());
    }
}

void MainWindow::onLimitComboChanged(int)
{
    onTabChanged(0);
}

void MainWindow::onCleanStatsClicked()
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;

    const int idx = m_tabs->currentPageIndex();
    if (idx == TAB_RULES)
        return;

    const char* table = 0;
    const char* field = 0;
    bool inDetail = false;
    TQString label;

    if (idx == TAB_EVENTS) {
        table = "connections";
        inDetail = false;
    } else if (idx == TAB_NODES) {
        table = "nodes";
        field = "node";
        inDetail = (m_nodesInDetail != 0);
        label = m_nodesDetailNode;
        if (inDetail && !label.isEmpty() && label[0] == '/')
            label = TQString("unix:%1").arg(label);
    } else if (idx == TAB_HOSTS) {
        table = "hosts";
        field = "dst_host";
        inDetail = (m_hostsInDetail != 0);
        label = m_hostsDetailWhat;
    } else if (idx == TAB_PROCS) {
        table = "procs";
        field = "process";
        inDetail = (m_procsInDetail != 0);
        label = m_procsDetailWhat;
    } else if (idx == TAB_ADDRS) {
        table = "addrs";
        field = "dst_ip";
        inDetail = (m_addrsInDetail != 0);
        label = m_addrsDetailWhat;
    } else if (idx == TAB_PORTS) {
        table = "ports";
        field = "dst_port";
        inDetail = (m_portsInDetail != 0);
        label = m_portsDetailWhat;
    } else if (idx == TAB_USERS) {
        table = "users";
        field = "uid";
        inDetail = (m_usersInDetail != 0);
        label = m_usersDetailWhat;
        // Accept "name (uid)".
        int p1 = label.find('(');
        int p2 = label.find(')');
        if (p1 >= 0 && p2 > p1)
            label = label.mid(p1 + 1, p2 - p1 - 1).stripWhiteSpace();
        else
            label = label.stripWhiteSpace();
    }

    if (!table)
        return;

    if (inDetail) {
        if (!field)
            return;
        label = label.stripWhiteSpace();
        if (label.isEmpty())
            return;

        db->deleteRows(table, "what = ?", TQValueList<TQVariant>() << TQVariant(label));
        db->deleteRows("connections", TQString("%1 = ?").arg(field), TQValueList<TQVariant>() << TQVariant(label));
    } else {
        db->exec(TQString("DELETE FROM %1").arg(table));
    }

    onTabChanged(0);
}

void MainWindow::onHelpClicked()
{
    // Matches Python QuickHelp.show() — tooltip balloon at cursor position
    TQWhatsThis::display(
        "<p><b>Quick help</b></p>"
        "<p>- Use CTRL+c to copy selected rows.</p>"
        "<p>- Use Home, End, PgUp, PgDown, Up or Down keys to navigate rows.</p>"
        "<p>- Use right click on a row to stop refreshing the view.</p>"
        "<p>- Selecting more than one row also stops refreshing the view.</p>"
        "<p>- On the Events view, clicking on columns Node, Process or Rule<br>"
        "jumps to the view of the selected item.</p>"
        "<p>- On the rest of the views, double click on a row to get detailed<br>"
        "information.</p><br>"
        "<p>For more information visit the "
        "<a href=\"https://github.com/evilsocket/opensnitch/wiki\">wiki</a></p>");
}
