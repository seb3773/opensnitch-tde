#include "prompt_dialog.h"
#include "config.h"
#include "desktop_parser.h"

#include <ntqapplication.h>
#include <ntqlayout.h>
#include <ntqframe.h>
#include <ntqtooltip.h>
#include <ntqcolor.h>
#include <ntqfont.h>
#include <ntqdesktopwidget.h>
#include <kiconloader.h>
#include <tdeglobal.h>
#include <ntqregexp.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>

// Labels matching the original Python UI
const char* PromptDialog::DURATION_LABELS[] = {
    "once", "30s", "5m", "15m", "30m", "1h", "12h", "until reboot", "forever"
};
int PromptDialog::DURATION_COUNT = 9;

const char* PromptDialog::TARGET_LABELS[] = {
    "from this executable", "from this command line",
    "this destination port", "this user",
    "this destination ip", "from this PID"
};
int PromptDialog::TARGET_COUNT = 6;

static inline TQString s2q(const std::string& s) { return TQString(s.c_str()); }

static inline TQString json_escape(const TQString& in)
{
    TQString out;
    out.reserve(in.length() + 8);
    for (int i = 0; i < (int)in.length(); i++) {
        const TQChar c = in[i];
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else out += c;
    }
    return out;
}

static inline void append_list_json(TQString* json, const char* type, const char* operand, const TQString& data)
{
    if (!json)
        return;
    if (json->length() > 1)
        (*json) += ",";
    (*json) += "{\"type\":\"";
    (*json) += type;
    (*json) += "\",\"operand\":\"";
    (*json) += operand;
    (*json) += "\",\"data\":\"";
    (*json) += json_escape(data);
    (*json) += "\"}";
}

static inline int is_ipv6_text(const TQString& s)
{
    return s.find(':') >= 0;
}

static inline int is_ipv4_text(const TQString& s)
{
    // minimal heuristic: 4 dotted parts
    return TQStringList::split(".", s).count() == 4;
}

static inline TQString wildcard_host_to_regex(const TQString& text)
{
    // Python UI builds: ^(|.*\.)example\.com
    // text is expected like "*.example.com" or "*example.com".
    TQString t = text;
    t.replace("\\", "\\\\");
    t.replace(".", "\\.");
    if (t.startsWith("*\\.")) {
        // drop "*\."
        t = t.mid(3);
        return TQString("^(|.*\\.)%1").arg(t);
    }
    if (t.startsWith("*")) {
        t = t.mid(1);
        return TQString("^.*%1").arg(t);
    }
    return t;
}

PromptDialog::PromptDialog(TQWidget* parent, const char* name)
    : TQDialog(parent, name,
               WType_Dialog | WStyle_StaysOnTop | WStyle_Customize |
               WStyle_NormalBorder | WStyle_Title | WStyle_SysMenu),
      m_tick(0),
      m_maxTick(Config::DEFAULT_TIMEOUT),
      m_timedOut(false),
      m_hasResult(false)
{
    setupUi();
    applyConfigDefaults();

    m_tickTimer = new TQTimer(this);
    connect(m_tickTimer, SIGNAL(timeout()), this, SLOT(onTick()));
}

PromptDialog::~PromptDialog()
{
}

static inline void move_prompt_popup(TQWidget* w)
{
    if (!w)
        return;

    Config* cfg = Config::get();
    const int pos = cfg ? cfg->getInt(Config::KEY_POPUP_POSITION, 0) : 0;
    TQDesktopWidget* dw = TQApplication::desktop();
    if (!dw)
        return;

    const TQRect r = dw->availableGeometry();
    const int ww = w->width();
    const int wh = w->height();

    int x = r.left() + (r.width() - ww) / 2;
    int y = r.top() + (r.height() - wh) / 2;

    switch (pos) {
    default:
    case 0: // center
        break;
    case 1: // top-left
        x = r.left();
        y = r.top();
        break;
    case 2: // top-right
        x = r.right() - ww;
        y = r.top();
        break;
    case 3: // bottom-left
        x = r.left();
        y = r.bottom() - wh;
        break;
    case 4: // bottom-right
        x = r.right() - ww;
        y = r.bottom() - wh;
        break;
    }

    if (x < r.left()) x = r.left();
    if (y < r.top()) y = r.top();
    w->move(x, y);
}

void PromptDialog::setupUi()
{
    setCaption("OpenSnitch-tde");
    setMinimumWidth(560);
    setMaximumWidth(800);
    resize(600, 400);

    TQVBoxLayout* mainLay = new TQVBoxLayout(this, 10, 6);

    // ================================================================
    // 1. Header: icon + app info
    // ================================================================
    TQHBoxLayout* headerLay = new TQHBoxLayout(mainLay, 4);

    // App icon (64x64)
    m_appIcon = new TQLabel(this);
    m_appIcon->setFixedSize(64, 64);
    m_appIcon->setScaledContents(true);
    m_appIcon->setAlignment(TQt::AlignTop | TQt::AlignLeft);
    headerLay->addWidget(m_appIcon);

    // Right side: VBox with app name, path, args
    TQVBoxLayout* infoLay = new TQVBoxLayout(headerLay, 2);

    // Line 1: App name (large bold)
    m_appName = new TQLabel(this);
    TQFont nameFont = m_appName->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 4);
    m_appName->setFont(nameFont);
    m_appName->setAlignment(TQt::AlignLeft | TQt::AlignVCenter);
    infoLay->addWidget(m_appName);

    // Line 2: Process path + info button
    TQHBoxLayout* pathLay = new TQHBoxLayout(infoLay, 2);
    m_appPathLabel = new TQLabel(this);
    m_appPathLabel->setAlignment(TQt::AlignLeft | TQt::AlignVCenter);
    m_appPathLabel->setMaximumWidth(420);
    // Elide long paths
    m_appPathLabel->setSizePolicy(TQSizePolicy(TQSizePolicy::Maximum, TQSizePolicy::Preferred));
    pathLay->addWidget(m_appPathLabel, 1);

    // Line 3: Process args
    m_argsLabel = new TQLabel(this);
    m_argsLabel->setAlignment(TQt::AlignLeft | TQt::AlignTop);
    m_argsLabel->setMaximumWidth(480);
    infoLay->addWidget(m_argsLabel);

    headerLay->addStretch();

    // ================================================================
    // 2. Separator
    // ================================================================
    m_separator = new TQFrame(this);
    m_separator->setFrameShape(TQFrame::HLine);
    m_separator->setFrameShadow(TQFrame::Sunken);
    m_separator->setMargin(10);
    mainLay->addWidget(m_separator);

    // ================================================================
    // 3. Connection summary (rich text)
    // ================================================================
    m_messageLabel = new TQLabel(this);
    m_messageLabel->setAlignment(TQt::AlignLeft | TQt::AlignTop);
    m_messageLabel->setTextFormat(TQt::RichText);
    m_messageLabel->setMargin(2);
    mainLay->addWidget(m_messageLabel);

    // ================================================================
    // 4. Details grid (2 columns x 6 rows)
    // ================================================================
    TQGridLayout* gridLay = new TQGridLayout(mainLay, 6, 4);

    // Row 0: Executed from
    m_lblCwd = new TQLabel("<b>Executed from</b>", this);
    m_lblCwd->setTextFormat(TQt::RichText);
    m_lblCwd->setAlignment(TQt::AlignLeft);
    m_valCwd = new TQLabel(this);
    m_valCwd->setAlignment(TQt::AlignRight);
    gridLay->addWidget(m_lblCwd, 0, 0);
    gridLay->addWidget(m_valCwd, 0, 1);

    // Row 1: Source IP
    m_lblSrcIP = new TQLabel("<b>Source IP</b>", this);
    m_lblSrcIP->setTextFormat(TQt::RichText);
    m_lblSrcIP->setAlignment(TQt::AlignLeft);
    m_valSrcIP = new TQLabel(this);
    m_valSrcIP->setAlignment(TQt::AlignRight);
    gridLay->addWidget(m_lblSrcIP, 1, 0);
    gridLay->addWidget(m_valSrcIP, 1, 1);

    // Row 2: Destination IP + advanced checkbox
    m_checkDstIP = new TQCheckBox(this);
    m_checkDstIP->hide();
    m_lblDstIP = new TQLabel("<b>Destination IP</b>", this);
    m_lblDstIP->setTextFormat(TQt::RichText);
    m_lblDstIP->setAlignment(TQt::AlignLeft);
    m_valDstIP = new TQLabel(this);
    m_valDstIP->setAlignment(TQt::AlignRight);
    m_whatIPCombo = new TQComboBox(this);
    m_whatIPCombo->hide();
    gridLay->addWidget(m_checkDstIP, 2, 0);
    gridLay->addWidget(m_lblDstIP, 2, 1);
    gridLay->addWidget(m_valDstIP, 2, 2);
    gridLay->addWidget(m_whatIPCombo, 2, 2);

    // Row 3: Destination port + advanced checkbox
    m_checkDstPort = new TQCheckBox(this);
    m_checkDstPort->hide();
    m_lblDstPort = new TQLabel("<b>Destination port</b>", this);
    m_lblDstPort->setTextFormat(TQt::RichText);
    m_lblDstPort->setAlignment(TQt::AlignLeft);
    m_valDstPort = new TQLabel(this);
    m_valDstPort->setAlignment(TQt::AlignRight);
    gridLay->addWidget(m_checkDstPort, 3, 0);
    gridLay->addWidget(m_lblDstPort, 3, 1);
    gridLay->addWidget(m_valDstPort, 3, 2);

    // Row 4: User ID + advanced checkbox
    m_checkUserID = new TQCheckBox(this);
    m_checkUserID->hide();
    m_lblUID = new TQLabel("<b>User ID</b>", this);
    m_lblUID->setTextFormat(TQt::RichText);
    m_lblUID->setAlignment(TQt::AlignLeft);
    m_valUID = new TQLabel(this);
    m_valUID->setAlignment(TQt::AlignRight);
    gridLay->addWidget(m_checkUserID, 4, 0);
    gridLay->addWidget(m_lblUID, 4, 1);
    gridLay->addWidget(m_valUID, 4, 2);

    // Row 5: Process ID
    m_lblPID = new TQLabel("<b>Process ID</b>", this);
    m_lblPID->setTextFormat(TQt::RichText);
    m_lblPID->setAlignment(TQt::AlignLeft);
    m_valPID = new TQLabel(this);
    m_valPID->setAlignment(TQt::AlignRight);
    gridLay->addWidget(m_lblPID, 5, 1);
    gridLay->addWidget(m_valPID, 5, 2);

    // ================================================================
    // 5. Bottom action bar
    // ================================================================
    TQHBoxLayout* actionLay = new TQHBoxLayout(mainLay, 3);

    // Target combo
    m_whatCombo = new TQComboBox(this);
    for (int i = 0; i < TARGET_COUNT; i++)
        m_whatCombo->insertItem(TQString(TARGET_LABELS[i]));
    actionLay->addWidget(m_whatCombo, 3);

    // Duration combo
    m_durationCombo = new TQComboBox(this);
    for (int i = 0; i < DURATION_COUNT; i++)
        m_durationCombo->insertItem(TQString(DURATION_LABELS[i]));
    actionLay->addWidget(m_durationCombo, 2);

    // Deny button (with countdown)
    m_denyBtn = new TQPushButton(this);
    m_denyBtn->setPaletteForegroundColor(TQColor(0xF4, 0x43, 0x36));
    TQFont denyFont = m_denyBtn->font();
    denyFont.setBold(true);
    m_denyBtn->setFont(denyFont);
    actionLay->addWidget(m_denyBtn, 2);

    // Allow button
    m_allowBtn = new TQPushButton(this);
    m_allowBtn->setPaletteForegroundColor(TQColor(0x4C, 0xAF, 0x50));
    m_allowBtn->setFont(denyFont);
    actionLay->addWidget(m_allowBtn, 2);

    // Advanced "+" button (small, checkable)
    m_advancedBtn = new TQPushButton("+", this);
    m_advancedBtn->setToggleButton(true);
    m_advancedBtn->setFixedWidth(30);
    m_advancedBtn->setFocusPolicy(NoFocus);
    actionLay->addWidget(m_advancedBtn);

    // Connections
    connect(m_allowBtn, SIGNAL(clicked()), this, SLOT(onAllow()));
    connect(m_denyBtn, SIGNAL(clicked()), this, SLOT(onDeny()));
    connect(m_advancedBtn, SIGNAL(toggled(bool)), this, SLOT(onAdvancedToggled(bool)));
}

void PromptDialog::applyConfigDefaults()
{
    Config* cfg = Config::get();
    m_maxTick = cfg->getInt(Config::KEY_DEFAULT_TIMEOUT, Config::DEFAULT_TIMEOUT);
    m_durationCombo->setCurrentItem(cfg->getInt(Config::KEY_DEFAULT_DURATION, Config::DEFAULT_DURATION_IDX));
    m_whatCombo->setCurrentItem(cfg->getInt(Config::KEY_DEFAULT_TARGET, 0));
}

TQPixmap PromptDialog::loadAppIcon(const TQString& procPath)
{
    // Extract binary name from path
    TQString binName = procPath;
    int lastSlash = binName.findRev('/');
    if (lastSlash >= 0)
        binName = binName.mid(lastSlash + 1);

    // Try loading from TDE icon theme using icon name from .desktop file
    DesktopParser* parser = DesktopParser::instance();
    const AppInfo* appInfo = parser->lookup(procPath);
    TQString iconName = appInfo ? appInfo->icon : TQString();

    TQPixmap pix;
    if (!iconName.isEmpty()) {
        pix = TDEGlobal::iconLoader()->loadIcon(iconName, TDEIcon::Desktop, 64,
                                                  TDEIcon::DefaultState, 0, true);
    }
    // Fallback: try binary name as icon name
    if (pix.isNull()) {
        pix = TDEGlobal::iconLoader()->loadIcon(binName, TDEIcon::Desktop, 64,
                                                  TDEIcon::DefaultState, 0, true);
    }
    if (pix.isNull()) {
        // Fallback: generic application icon
        pix = TDEGlobal::iconLoader()->loadIcon("application-x-executable",
                                                  TDEIcon::Desktop, 64,
                                                  TDEIcon::DefaultState, 0, true);
    }
    return pix;
}

protocol::Rule PromptDialog::promptUser(const protocol::Connection& conn, bool isLocal, const TQString& peer)
{
    m_currentConn = conn;
    m_currentPeer = peer;
    m_currentIsLocal = isLocal;
    m_hasResult = false;
    m_timedOut = false;
    m_tick = m_maxTick;

    populateFields(conn, isLocal);
    applyConfigDefaults();

    move_prompt_popup(this);

    // Reset advanced state
    if (m_advancedBtn->isOn())
        m_advancedBtn->toggle();
    if (m_checkDstIP) m_checkDstIP->setChecked(false);
    if (m_checkDstPort) m_checkDstPort->setChecked(false);
    if (m_checkUserID) m_checkUserID->setChecked(false);

    // Apply preferences: show detailed view by default + which fields to include.
    {
        Config* cfg = Config::get();
        const int wantAdv = (cfg && cfg->getBool(Config::KEY_POPUP_ADVANCED, false)) ? 1 : 0;
        const int wantUid = (cfg && cfg->getBool(Config::KEY_POPUP_ADVANCED_UID, false)) ? 1 : 0;
        const int wantPort = (cfg && cfg->getBool(Config::KEY_POPUP_ADVANCED_DSTPORT, false)) ? 1 : 0;
        const int wantIP = (cfg && cfg->getBool(Config::KEY_POPUP_ADVANCED_DSTIP, false)) ? 1 : 0;

        if (wantAdv) {
            if (!m_advancedBtn->isOn())
                m_advancedBtn->toggle();

            // If user enabled advanced view but no specific field is selected,
            // keep Python-like behavior: enable all.
            const int any = (wantUid || wantPort || wantIP) ? 1 : 0;
            if (m_checkUserID) m_checkUserID->setChecked(any ? (wantUid ? true : false) : true);
            if (m_checkDstPort) m_checkDstPort->setChecked(any ? (wantPort ? true : false) : true);
            if (m_checkDstIP) m_checkDstIP->setChecked(any ? (wantIP ? true : false) : true);
        } else {
            if (m_advancedBtn->isOn())
                m_advancedBtn->toggle();
        }
    }

    // Update deny button with countdown
    m_denyBtn->setText(TQString("Deny (%1)").arg(m_tick));
    m_allowBtn->setText("Allow");

    m_tickTimer->start(1000);

    show();
    raise();
    setActiveWindow();
    exec();

    m_tickTimer->stop();

    if (!m_hasResult) {
        m_resultRule = buildRule();
        m_resultRule.set_action(Config::get()->getDefaultAction().latin1());
        m_timedOut = true;
    }

    return m_resultRule;
}

void PromptDialog::populateFields(const protocol::Connection& conn, bool isLocal)
{
    TQString procPath = s2q(conn.process_path());
    TQString appName = procPath;
    int lastSlash = appName.findRev('/');
    if (lastSlash >= 0)
        appName = appName.mid(lastSlash + 1);

    // Try to get friendly app name from .desktop file
    DesktopParser* parser = DesktopParser::instance();
    const AppInfo* appInfo = parser->lookup(procPath);
    if (appInfo && !appInfo->name.isEmpty())
        appName = appInfo->name;

    // App icon
    TQPixmap icon = loadAppIcon(procPath);
    m_appIcon->setPixmap(icon);

    // App name (large bold)
    m_appName->setText(appName);

    // App path: show in parentheses if different from args, elide if too long
    TQString appArgs = conn.process_args_size() > 0
        ? s2q(conn.process_args(0))
        : TQString();
    TQString pathText;
    if (!procPath.isEmpty() && (appArgs.isEmpty() || procPath != appArgs))
        pathText = TQString("(%1)").arg(procPath);
    else
        pathText = procPath;

    // Elide long paths: keep first and last part
    if (pathText.length() > 60) {
        TQString binPart = procPath;
        int ls = binPart.findRev('/');
        if (ls >= 0) binPart = binPart.mid(ls);
        TQString dirPart = procPath.left(20);
        pathText = TQString("(%1...%2)").arg(dirPart).arg(binPart);
    }
    m_appPathLabel->setText(pathText);
    m_appPathLabel->setShown(!procPath.isEmpty());

    // Args label
    if (conn.process_args_size() > 0) {
        TQString argsText;
        for (int i = 0; i < conn.process_args_size(); i++) {
            if (i > 0) argsText += " ";
            argsText += s2q(conn.process_args(i));
        }
        // Elide long args
        if (argsText.length() > 80)
            argsText = argsText.left(40) + "..." + argsText.right(30);
        m_argsLabel->setText(argsText);
        m_argsLabel->setShown(true);
    } else {
        m_argsLabel->setText("");
        m_argsLabel->hide();
    }

    // Connection summary (rich text with bold)
    TQString dstHost = s2q(conn.dst_host());
    TQString dstIP = s2q(conn.dst_ip());
    TQString dstPort = TQString::number(conn.dst_port());
    TQString proto = s2q(conn.protocol()).upper();
    TQString target = dstHost.isEmpty() ? dstIP : dstHost;

    TQString message;
    if (!isLocal) {
        message = TQString("<b>Remote</b> process <b>%1</b> running on <b>%2</b>")
                      .arg(appName).arg(m_currentPeer);
    } else {
        message = TQString("<b>%1</b>").arg(appName);
    }

    if (conn.dst_port() == 0) {
        message += TQString(" is connecting to <b>%1</b>, %2")
                       .arg(target).arg(proto);
    } else if (conn.dst_port() == 53 && dstHost != dstIP && !dstHost.isEmpty()) {
        message += TQString(" is attempting to resolve <b>%1</b> via %2, %3 port %4")
                       .arg(dstHost).arg(dstIP).arg(proto).arg(dstPort);
    } else {
        message += TQString(" is connecting to <b>%1</b> on %2 port %3")
                       .arg(target).arg(proto).arg(dstPort);
    }

    m_messageLabel->setText(message);

    // Details grid values
    m_valCwd->setText(s2q(conn.process_cwd()));
    m_valSrcIP->setText(s2q(conn.src_ip()));
    m_valDstIP->setText(dstIP);
    m_valDstPort->setText(dstPort);

    m_whatIPCombo->clear();
    if (!dstHost.isEmpty() && dstHost != dstIP) {
        m_whatIPCombo->insertItem(dstHost);

        // Add host wildcards: to *.<rest> progressively (python behavior)
        TQStringList hparts = TQStringList::split(".", dstHost);
        if (hparts.count() >= 3) {
            // drop first element, keep at least 2 parts
            for (int i = 1; i < (int)hparts.count() - 1; i++) {
                TQString suffix;
                for (int j = i; j < (int)hparts.count(); j++) {
                    if (j != i)
                        suffix += ".";
                    suffix += hparts[j];
                }
                m_whatIPCombo->insertItem(TQString("to *.%1").arg(suffix));
            }
        }
    }

    if (!dstIP.isEmpty()) {
        m_whatIPCombo->insertItem(TQString("to %1").arg(dstIP));
        if (is_ipv4_text(dstIP)) {
            TQStringList parts = TQStringList::split(".", dstIP);
            const int nparts = (int)parts.count();
            for (int i = 1; i < nparts; i++) {
                TQString prefix;
                for (int j = 0; j < i; j++) {
                    if (j)
                        prefix += ".";
                    prefix += parts[j];
                }
                m_whatIPCombo->insertItem(TQString("to %1.*").arg(prefix));
            }
            m_whatIPCombo->insertItem(TQString("to %1.%2.%3.0/24").arg(parts[0]).arg(parts[1]).arg(parts[2]));
            m_whatIPCombo->insertItem(TQString("to %1.%2.0.0/16").arg(parts[0]).arg(parts[1]));
            m_whatIPCombo->insertItem(TQString("to %1.0.0.0/8").arg(parts[0]));
        } else if (is_ipv6_text(dstIP)) {
            m_whatIPCombo->insertItem(TQString("to %1/64").arg(dstIP));
            m_whatIPCombo->insertItem(TQString("to %1/128").arg(dstIP));
        }
    }

    // User ID with name resolution
    if (isLocal) {
        struct passwd* pw = getpwuid(conn.user_id());
        if (pw)
            m_valUID->setText(TQString("%1 (%2)").arg(conn.user_id()).arg(pw->pw_name));
        else
            m_valUID->setText(TQString::number(conn.user_id()));
    } else {
        m_valUID->setText(TQString::number(conn.user_id()));
    }

    m_valPID->setText(TQString::number(conn.process_id()));

    // Populate target combo with dynamic entries
    m_whatCombo->clear();
    for (int i = 0; i < TARGET_COUNT; i++)
        m_whatCombo->insertItem(TQString(TARGET_LABELS[i]));

    // Add destination host patterns if available
    if (!dstHost.isEmpty() && dstHost != dstIP) {
        TQStringList parts = TQStringList::split(".", dstHost);
        for (unsigned i = 1; i < parts.count(); i++) {
            TQString pattern = "*." + parts[i];
            for (unsigned j = i + 1; j < parts.count(); j++)
                pattern += "." + parts[j];
            m_whatCombo->insertItem(TQString("to %1").arg(pattern));
        }
    }
}

protocol::Rule PromptDialog::buildRule()
{
    protocol::Rule rule;

    TQString procPath = s2q(m_currentConn.process_path());
    TQString procName = procPath;
    int lastSlash = procName.findRev('/');
    if (lastSlash >= 0)
        procName = procName.mid(lastSlash + 1);

    rule.set_name(procName.latin1());
    rule.set_enabled(true);
    rule.set_precedence(false);

    // Duration
    int durIdx = m_durationCombo->currentItem();
    const char* durations[] = {
        Config::DURATION_ONCE, Config::DURATION_30S, Config::DURATION_5M,
        Config::DURATION_15M, Config::DURATION_30M, Config::DURATION_1H,
        Config::DURATION_12H, Config::DURATION_UNTIL_RESTART, Config::DURATION_ALWAYS
    };
    if (durIdx >= 0 && durIdx < 9)
        rule.set_duration(durations[durIdx]);
    else
        rule.set_duration(Config::DURATION_UNTIL_RESTART);

    // Operator based on target selection
    protocol::Operator* op = rule.mutable_operator_();
    op->set_sensitive(false);

    int targetIdx = m_whatCombo->currentItem();
    // First 6 items are the fixed TARGET_LABELS
    const char* operands[] = {
        Config::OPERAND_PROCESS_PATH, Config::OPERAND_PROCESS_COMMAND,
        Config::OPERAND_DEST_PORT, Config::OPERAND_USER_ID,
        Config::OPERAND_DEST_IP, Config::OPERAND_PROCESS_PATH
    };
    const char* types[] = {
        Config::RULE_TYPE_SIMPLE, Config::RULE_TYPE_SIMPLE,
        Config::RULE_TYPE_SIMPLE, Config::RULE_TYPE_SIMPLE,
        Config::RULE_TYPE_NETWORK, Config::RULE_TYPE_SIMPLE
    };

    if (targetIdx >= 0 && targetIdx < TARGET_COUNT) {
        op->set_type(types[targetIdx]);
        op->set_operand(operands[targetIdx]);

        TQString dataVal;
        switch (targetIdx) {
        case 0: dataVal = s2q(m_currentConn.process_path()); break;
        case 1: dataVal = ""; break; // process command - TODO
        case 2: dataVal = TQString::number(m_currentConn.dst_port()); break;
        case 3: dataVal = TQString::number(m_currentConn.user_id()); break;
        case 4: dataVal = s2q(m_currentConn.dst_ip()); break;
        case 5: dataVal = TQString::number(m_currentConn.process_id()); break;
        default: dataVal = s2q(m_currentConn.process_path()); break;
        }
        op->set_data(dataVal.latin1());
    } else if (targetIdx >= TARGET_COUNT) {
        // Dynamic entries (destination host patterns) - use DEST_HOST
        op->set_type(Config::RULE_TYPE_SIMPLE);
        op->set_operand(Config::OPERAND_DEST_HOST);
        TQString text = m_whatCombo->currentText();
        // Extract host pattern from "to *.example.com"
        if (text.startsWith("to "))
            text = text.mid(3);
        op->set_data(text.latin1());
    } else {
        op->set_type(Config::RULE_TYPE_SIMPLE);
        op->set_operand(Config::OPERAND_PROCESS_PATH);
        op->set_data(m_currentConn.process_path());
    }

    const int isList = (m_checkDstIP->isChecked() || m_checkDstPort->isChecked() || m_checkUserID->isChecked()) ? 1 : 0;
    if (isList) {
        TQString json = "[";

        const TQString baseType = s2q(op->type());
        const TQString baseOperand = s2q(op->operand());
        const TQString baseData = s2q(op->data());

        if (m_checkDstIP->isChecked() && baseOperand != Config::OPERAND_DEST_IP) {
            TQString text = m_whatIPCombo->currentText();
            if (text.startsWith("to "))
                text = text.mid(3);

            if (text.find('*') >= 0) {
                // host wildcards as regexp dest.host (python behavior)
                const TQString rx = wildcard_host_to_regex(text);
                append_list_json(&json, Config::RULE_TYPE_REGEXP, Config::OPERAND_DEST_HOST, rx);
            } else if (text.find('/') >= 0) {
                append_list_json(&json, Config::RULE_TYPE_NETWORK, "dest.network", text);
            } else if (is_ipv6_text(text)) {
                append_list_json(&json, Config::RULE_TYPE_NETWORK, Config::OPERAND_DEST_IP, text);
            } else {
                append_list_json(&json, Config::RULE_TYPE_NETWORK, Config::OPERAND_DEST_IP, text);
            }
        }

        if (m_checkDstPort->isChecked() && baseOperand != Config::OPERAND_DEST_PORT) {
            append_list_json(&json, Config::RULE_TYPE_SIMPLE, Config::OPERAND_DEST_PORT, TQString::number(m_currentConn.dst_port()));
        }

        if (m_checkUserID->isChecked() && baseOperand != Config::OPERAND_USER_ID) {
            append_list_json(&json, Config::RULE_TYPE_SIMPLE, Config::OPERAND_USER_ID, TQString::number(m_currentConn.user_id()));
        }

        append_list_json(&json, baseType.latin1(), baseOperand.latin1(), baseData);
        json += "]";

        op->set_data(json.latin1());
        op->set_type(Config::RULE_TYPE_LIST);
        op->set_operand(Config::RULE_TYPE_LIST);
    }

    return rule;
}

void PromptDialog::onAllow()
{
    m_resultRule = buildRule();
    m_resultRule.set_action(Config::ACTION_ALLOW);
    m_hasResult = true;
    m_tickTimer->stop();
    accept();
}

void PromptDialog::onDeny()
{
    m_resultRule = buildRule();
    m_resultRule.set_action(Config::ACTION_DENY);
    m_hasResult = true;
    m_tickTimer->stop();
    accept();
}

void PromptDialog::onReject()
{
    m_resultRule = buildRule();
    m_resultRule.set_action(Config::ACTION_REJECT);
    m_hasResult = true;
    m_tickTimer->stop();
    accept();
}

void PromptDialog::onTick()
{
    m_tick--;
    if (m_tick <= 0) {
        m_tick = 0;
        m_timedOut = true;
        m_tickTimer->stop();
        accept();
    }
    m_denyBtn->setText(TQString("Deny (%1)").arg(m_tick));
}

void PromptDialog::onAdvancedToggled(bool on)
{
    m_checkDstIP->setShown(on);
    m_checkDstPort->setShown(on);
    m_checkUserID->setShown(on);
    m_whatIPCombo->setShown(on);
    m_valDstIP->setShown(!on);

    if (on) {
        // Python UI: when advanced mode is shown, checkboxes are enabled by default.
        m_checkDstIP->setChecked(true);
        m_checkDstPort->setChecked(true);
        m_checkUserID->setChecked(true);
    } else {
        // Ensure hidden advanced criteria don't affect rule generation.
        m_checkDstIP->setChecked(false);
        m_checkDstPort->setChecked(false);
        m_checkUserID->setChecked(false);
    }
}

void PromptDialog::closeEvent(TQCloseEvent* e)
{
    if (!m_hasResult) {
        m_resultRule = buildRule();
        m_resultRule.set_action(Config::get()->getDefaultAction().latin1());
        m_hasResult = true;
    }
    m_tickTimer->stop();
    e->accept();
}

void PromptDialog::keyPressEvent(TQKeyEvent* e)
{
    if (e->key() == TQt::Key_Escape) {
        onDeny();
        return;
    }
    TQDialog::keyPressEvent(e);
}
