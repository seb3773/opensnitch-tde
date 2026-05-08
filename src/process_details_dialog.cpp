#include "process_details_dialog.h"

#include "grpc_server.h"
#include "desktop_parser.h"
#include "config.h"

#include "embedded_icons.h"
#include "icon_theme.h"

#include <tdeglobal.h>
#include <kiconloader.h>

#include <ntqlabel.h>
#include <ntqcombobox.h>
#include <ntqpushbutton.h>
#include <ntqtabwidget.h>
#include <ntqtextedit.h>
#include <ntqlayout.h>
#include <ntqframe.h>
#include <ntqfont.h>
#include <ntqstringlist.h>

#include <ntqsizepolicy.h>

#include "tqtjson.h"

static inline TQPixmap loadAppIcon64(const TQString& procPath)
{
    TQString binName = procPath;
    int lastSlash = binName.findRev('/');
    if (lastSlash >= 0)
        binName = binName.mid(lastSlash + 1);

    DesktopParser* parser = DesktopParser::instance();
    const AppInfo* appInfo = parser ? parser->lookup(procPath) : 0;
    TQString iconName = appInfo ? appInfo->icon : TQString();

    TQPixmap pix;
    if (!iconName.isEmpty()) {
        pix = TDEGlobal::iconLoader()->loadIcon(iconName, TDEIcon::Desktop, 64,
                                                TDEIcon::DefaultState, 0, true);
    }
    if (pix.isNull()) {
        pix = TDEGlobal::iconLoader()->loadIcon(binName, TDEIcon::Desktop, 64,
                                                TDEIcon::DefaultState, 0, true);
    }
    if (pix.isNull()) {
        pix = TDEGlobal::iconLoader()->loadIcon("application-x-executable", TDEIcon::Desktop, 64,
                                                TDEIcon::DefaultState, 0, true);
    }
    return pix;
}

TQString ProcessDetailsDialog::wrapAtSpaces(const TQString& s, int maxCols)
{
    // Complexity: O(n)
    // Dependencies: none
    // Notes: naive wrap, inserts newlines at spaces to prevent huge dialog widths.
    if (maxCols <= 16)
        return s;
    if (s.length() <= (uint)maxCols)
        return s;

    TQString out;
    out.reserve((int)s.length() + (int)(s.length() / maxCols) + 8);
    int col = 0;
    int lastSpaceOut = -1;

    for (int i = 0; i < (int)s.length(); ++i) {
        const TQChar ch = s.at(i);
        out += ch;
        ++col;
        if (ch == ' ' || ch == '\t')
            lastSpaceOut = out.length() - 1;

        if (col >= maxCols) {
            if (lastSpaceOut >= 0 && lastSpaceOut < (int)out.length()) {
                out[lastSpaceOut] = '\n';
                col = (int)(out.length() - lastSpaceOut - 1);
                lastSpaceOut = -1;
            } else {
                out += "\n";
                col = 0;
            }
        }
    }

    return out;
}

void ProcessDetailsDialog::renderTabFromJson(int tabIdx, const TQtJsonObject& proc)
{
    if (tabIdx == 0) {
        if (m_textStatus)
            m_textStatus->setText(proc.value("Status").toString(TQString()));
        return;
    }
    if (tabIdx == 1) {
        TQtJsonValue dv = proc.value("Descriptors");
        if (dv.isArray() && m_textOpenFiles) {
            TQtJsonArray d = dv.toArray();
            TQString text = "Size        Time                                    Name     -> Symlink\n\n";
            for (int i = 0; i < (int)d.size(); ++i) {
                if (!d.at(i).isObject())
                    continue;
                TQtJsonObject o = d.at(i).toObject();
                text += TQString("%1 %2 %3 -> %4\n")
                            .arg(o.value("Size").toString(TQString()), -12)
                            .arg(o.value("ModTime").toString(TQString()), -40)
                            .arg(o.value("Name").toString(TQString()), -8)
                            .arg(o.value("SymLink").toString(TQString()));
            }
            m_textOpenFiles->setText(text);
        }
        return;
    }
    if (tabIdx == 2) {
        TQtJsonValue iv = proc.value("IOStats");
        if (iv.isObject() && m_textIO) {
            TQtJsonObject io = iv.toObject();
            const int rCharMb = (int)(((io.value("RChar").toDouble(0.0) / 1024.0) / 1024.0));
            const int wCharMb = (int)(((io.value("WChar").toDouble(0.0) / 1024.0) / 1024.0));
            const int sysRead = (int)io.value("SyscallRead").toDouble(0.0);
            const int sysWrite = (int)io.value("SyscallWrite").toDouble(0.0);
            const int readMb = (int)(((io.value("ReadBytes").toDouble(0.0) / 1024.0) / 1024.0));
            const int writeMb = (int)(((io.value("WriteBytes").toDouble(0.0) / 1024.0) / 1024.0));

            TQString text;
            text += TQString("Chars read: %1MB\n").arg(rCharMb);
            text += TQString("Chars written: %1MB\n").arg(wCharMb);
            text += TQString("Syscalls read: %1\n").arg(sysRead);
            text += TQString("Syscalls write: %1\n").arg(sysWrite);
            text += TQString("KB read: %1MB\n").arg(readMb);
            text += TQString("KB written: %1MB\n").arg(writeMb);
            m_textIO->setText(text);
        }
        return;
    }
    if (tabIdx == 3) {
        if (m_textMaps)
            m_textMaps->setText(proc.value("Maps").toString(TQString()));
        return;
    }
    if (tabIdx == 4) {
        if (m_textStack)
            m_textStack->setText(proc.value("Stack").toString(TQString()));
        return;
    }
    if (tabIdx == 5) {
        TQtJsonValue ev = proc.value("Env");
        if (m_textEnv) {
            if (!ev.isObject()) {
                m_textEnv->setText("<no environment variables>");
            } else {
                TQtJsonObject env = ev.toObject();
                if (env.keys().isEmpty()) {
                    m_textEnv->setText("<no environment variables>");
                } else {
                    TQString text = "Name\tValue\n\n";
                    TQStringList keys = env.keys();
                    for (TQStringList::ConstIterator it = keys.begin(); it != keys.end(); ++it) {
                        text += *it + ":\t" + env.value(*it).toString(TQString()) + "\n";
                    }
                    m_textEnv->setText(text);
                }
            }
        }
        return;
    }
}

ProcessDetailsDialog::ProcessDetailsDialog(GRpcServer* srv, TQWidget* parent, const char* name)
    : TQDialog(parent, name),
      m_server(srv),
      m_icon(0),
      m_name(0),
      m_args(0),
      m_cwd(0),
      m_statm(0),
      m_tabs(0),
      m_textStatus(0),
      m_textOpenFiles(0),
      m_textIO(0),
      m_textMaps(0),
      m_textStack(0),
      m_textEnv(0),
      m_pidCombo(0),
      m_actionBtn(0),
      m_closeBtn(0),
      m_lastNid(0),
      m_running(0),
      m_dataLoaded(0)
{
    setCaption("Process details");
    resize(760, 520);

    TQVBoxLayout* mainLay = new TQVBoxLayout(this, 6, 4);

    {
        TQHBoxLayout* headerLay = new TQHBoxLayout(mainLay, 6);
        m_icon = new TQLabel(this);
        m_icon->setFixedSize(64, 64);
        headerLay->addWidget(m_icon);

        TQVBoxLayout* infoLay = new TQVBoxLayout(headerLay, 2);
        m_name = new TQLabel(this);
        TQFont f = m_name->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 2);
        m_name->setFont(f);
        infoLay->addWidget(m_name);

        m_args = new TQLabel(this);
        m_args->setAlignment(TQt::AlignLeft | TQt::AlignTop);
        m_args->setTextFormat(TQt::PlainText);
        m_args->setAlignment(TQt::AlignLeft | TQt::AlignTop);
        m_args->setSizePolicy(TQSizePolicy(TQSizePolicy::Ignored, TQSizePolicy::Fixed));
        infoLay->addWidget(m_args);

        headerLay->addStretch(1);
    }

    m_cwd = new TQLabel(this);
    m_cwd->setTextFormat(TQt::PlainText);
    mainLay->addWidget(m_cwd);

    m_statm = new TQLabel(this);
    m_statm->setTextFormat(TQt::PlainText);
    mainLay->addWidget(m_statm);

    {
        TQFrame* line = new TQFrame(this);
        line->setFrameShape(TQFrame::HLine);
        line->setFrameShadow(TQFrame::Sunken);
        mainLay->addWidget(line);
    }

    m_tabs = new TQTabWidget(this);
    mainLay->addWidget(m_tabs, 1);

    m_textStatus = new TQTextEdit(m_tabs);
    m_textStatus->setReadOnly(true);
    m_tabs->addTab(m_textStatus, "Status");

    m_textOpenFiles = new TQTextEdit(m_tabs);
    m_textOpenFiles->setReadOnly(true);
    {
        TQFont mono("monospace");
        m_textOpenFiles->setFont(mono);
    }
    m_tabs->addTab(m_textOpenFiles, "Open files");

    m_textIO = new TQTextEdit(m_tabs);
    m_textIO->setReadOnly(true);
    m_tabs->addTab(m_textIO, "I/O Statistics");

    m_textMaps = new TQTextEdit(m_tabs);
    m_textMaps->setReadOnly(true);
    m_tabs->addTab(m_textMaps, "Memory mapped files");

    m_textStack = new TQTextEdit(m_tabs);
    m_textStack->setReadOnly(true);
    m_tabs->addTab(m_textStack, "Stack");

    m_textEnv = new TQTextEdit(m_tabs);
    m_textEnv->setReadOnly(true);
    m_tabs->addTab(m_textEnv, "Environment variables");

    connect(m_tabs, SIGNAL(currentChanged(TQWidget*)), this, SLOT(onTabChanged(TQWidget*)));

    {
        TQHBoxLayout* bottomLay = new TQHBoxLayout(mainLay, 4);
        bottomLay->addWidget(new TQLabel("Application pids", this));
        m_pidCombo = new TQComboBox(this);
        bottomLay->addWidget(m_pidCombo);
        bottomLay->addStretch(1);

        m_actionBtn = new TQPushButton(this);
        m_actionBtn->setToggleButton(true);
        m_actionBtn->setText("");
        m_actionBtn->setFixedSize(28, 24);
        bottomLay->addWidget(m_actionBtn);

        m_closeBtn = new TQPushButton("Close", this);
        bottomLay->addWidget(m_closeBtn);

        connect(m_closeBtn, SIGNAL(clicked()), this, SLOT(onCloseClicked()));
        connect(m_actionBtn, SIGNAL(clicked()), this, SLOT(onActionToggled()));
        connect(m_pidCombo, SIGNAL(activated(int)), this, SLOT(onPidChanged(int)));
    }

    resetUi();
}

ProcessDetailsDialog::~ProcessDetailsDialog()
{
    stopMonitoring();
}

void ProcessDetailsDialog::resetUi()
{
    m_pids.clear();
    m_notificationsSent.clear();
    m_pid = TQString();
    m_node = TQString();
    m_lastJson = TQString();
    m_lastNid = 0;
    m_running = 0;
    m_dataLoaded = 0;

    if (m_pidCombo)
        m_pidCombo->clear();

    if (m_name)
        m_name->setText("loading...");
    if (m_args)
        m_args->setText("loading...");
    if (m_icon)
        m_icon->clear();
    if (m_cwd)
        m_cwd->setText(TQString());
    if (m_statm)
        m_statm->setText(TQString());

    if (m_textStatus) m_textStatus->setText("");
    if (m_textOpenFiles) m_textOpenFiles->setText("");
    if (m_textIO) m_textIO->setText("");
    if (m_textMaps) m_textMaps->setText("");
    if (m_textStack) m_textStack->setText("");
    if (m_textEnv) m_textEnv->setText("");

    setRunning(0);
}

void ProcessDetailsDialog::setRunning(int yes)
{
    m_running = yes ? 1 : 0;
    if (!m_actionBtn)
        return;

    TQPixmap pm;
    if (yes) {
        pm = IconTheme::loadEmbeddedPixmap(media_playback_pause_png,
                                           (int)media_playback_pause_png_len, 16);
    } else {
        pm = IconTheme::loadEmbeddedPixmap(media_playback_start_png,
                                           (int)media_playback_start_png_len, 16);
    }

    if (pm.isNull()) {
        const char* iconName = yes ? "media-playback-pause" : "media-playback-start";
        pm = TDEGlobal::iconLoader()->loadIcon(iconName, TDEIcon::Small, 16,
                                               TDEIcon::DefaultState, 0, true);
    }
    if (!pm.isNull())
        m_actionBtn->setIconSet(TQIconSet(pm, TQIconSet::Small));
    m_actionBtn->setOn(yes ? true : false);
}

void ProcessDetailsDialog::monitor(const TQMap<TQString, TQString>& pidToNode)
{
    stopMonitoring();

    resetUi();
    m_pids = pidToNode;

    for (TQMap<TQString, TQString>::ConstIterator it = m_pids.begin(); it != m_pids.end(); ++it) {
        if (!it.key().isEmpty())
            m_pidCombo->insertItem(it.key());
    }

    show();
    raise();
    setActiveWindow();

    startMonitoring();
}

void ProcessDetailsDialog::startMonitoring()
{
    // Parity with Python: don't send multiple MONITOR_PROCESS notifications
    // while a pid is already being monitored.
    if (!m_pid.isEmpty())
        return;
    if (!m_server)
        return;
    if (!m_pidCombo)
        return;

    m_pid = m_pidCombo->currentText();
    if (m_pid.isEmpty())
        return;
    if (!m_pids.contains(m_pid))
        return;

    m_node = m_pids[m_pid];
    if (m_node.isEmpty())
        return;

    setRunning(1);

    protocol::Notification notif;
    notif.set_id((unsigned long long)time(0));
    notif.set_type(protocol::MONITOR_PROCESS);
    notif.set_data(m_pid.latin1());
    m_lastNid = notif.id();
    m_notificationsSent.insert(m_lastNid, (int)protocol::MONITOR_PROCESS);
    m_server->sendNotification(m_node, notif);
}

void ProcessDetailsDialog::stopMonitoring()
{
    if (m_pid.isEmpty())
        return;
    if (!m_server)
        return;
    if (m_node.isEmpty())
        return;

    protocol::Notification notif;
    notif.set_id((unsigned long long)time(0));
    notif.set_type(protocol::STOP_MONITOR_PROCESS);
    notif.set_data(m_pid.latin1());
    m_lastNid = notif.id();
    m_notificationsSent.insert(m_lastNid, (int)protocol::STOP_MONITOR_PROCESS);
    m_server->sendNotification(m_node, notif);

    setRunning(0);
    // Python clears _pid immediately after sending STOP.
    m_pid = TQString();
}

void ProcessDetailsDialog::handleNotificationReply(const TQString& peer, unsigned long long id, int code, const TQString& data)
{
    if (id == 0)
        return;

    if (!m_notificationsSent.contains(id))
        return;

    // Basic peer/node sanity check (daemon can be multi-node)
    if (!m_node.isEmpty() && peer != m_node)
        return;

    const int notiType = m_notificationsSent[id];

    if (code == (int)protocol::ERROR) {
        setRunning(0);
        if (!m_dataLoaded) {
            if (m_pidCombo && m_pidCombo->count() <= 1)
                hide();
        }
        m_notificationsSent.remove(id);
        return;
    }

    if (notiType == (int)protocol::MONITOR_PROCESS) {
        if (!data.isEmpty())
            loadDataJson(data);
        // Keep MONITOR_PROCESS id registered for subsequent updates.
    } else if (notiType == (int)protocol::STOP_MONITOR_PROCESS) {
        // STOP replies are one-shot.
        m_notificationsSent.remove(id);
    }
}

void ProcessDetailsDialog::loadAppIconAndName(const TQString& procPath)
{
    TQString appName = procPath;
    int lastSlash = appName.findRev('/');
    if (lastSlash >= 0)
        appName = appName.mid(lastSlash + 1);

    DesktopParser* parser = DesktopParser::instance();
    const AppInfo* info = parser ? parser->lookup(procPath) : 0;
    if (info && !info->name.isEmpty())
        appName = info->name;

    if (m_name) {
        m_name->setText(appName);
    }

    if (m_icon) {
        TQPixmap pix = loadAppIcon64(procPath);
        if (!pix.isNull())
            m_icon->setPixmap(pix);
    }
}

void ProcessDetailsDialog::loadDataJson(const TQString& json)
{
    TQtJsonParseError err;
    TQtJsonDocument doc = TQtJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject())
        return;

    TQtJsonObject proc = doc.object();
    m_lastJson = json;
    const TQString procPath = proc.value("Path").toString(TQString());

    if (!procPath.isEmpty())
        loadAppIconAndName(procPath);

    {
        TQtJsonValue av = proc.value("Args");
        TQStringList args;
        if (av.isArray()) {
            TQtJsonArray a = av.toArray();
            for (int i = 0; i < (int)a.size(); ++i)
                args << a.at(i).toString(TQString());
        }
        const TQString argsText = args.join(" ");
        if (m_args) {
            m_args->setText(wrapAtSpaces(argsText, 120));
        }
    }

    {
        const TQString cwd = proc.value("CWD").toString(TQString());
        if (m_cwd) {
            const TQString t = "CWD: " + cwd;
            m_cwd->setText(t);
        }
    }

    {
        TQtJsonValue mv = proc.value("Statm");
        if (mv.isObject()) {
            TQtJsonObject m = mv.toObject();
            const double size = m.value("Size").toDouble(0.0);
            const double resident = m.value("Resident").toDouble(0.0);
            const double lib = m.value("Lib").toDouble(0.0);
            const double data = m.value("Data").toDouble(0.0);
            const double text = m.value("Text").toDouble(0.0);

            const double pagesize = 4096.0;
            const int virtMb = (int)(((size * pagesize) / 1024.0) / 1024.0);
            const int rssMb  = (int)(((resident * pagesize) / 1024.0) / 1024.0);
            const int libMb  = (int)(((lib * pagesize) / 1024.0) / 1024.0);
            const int dataMb = (int)(((data * pagesize) / 1024.0) / 1024.0);
            const int textMb = (int)(((text * pagesize) / 1024.0) / 1024.0);

            if (m_statm) {
                m_statm->setText(TQString("VIRT: %1MB, RSS: %2MB, Libs: %3MB, Data: %4MB, Text: %5MB")
                                     .arg(virtMb).arg(rssMb).arg(libMb).arg(dataMb).arg(textMb));
            }
        }
    }

    const int tabIdx = m_tabs ? m_tabs->currentPageIndex() : 0;
    renderTabFromJson(tabIdx, proc);

    m_dataLoaded = 1;
}

void ProcessDetailsDialog::onCloseClicked()
{
    stopMonitoring();
    hide();
}

void ProcessDetailsDialog::onActionToggled()
{
    if (m_running)
        stopMonitoring();
    else
        startMonitoring();
}

void ProcessDetailsDialog::onPidChanged(int)
{
    if (!m_pidCombo)
        return;

    const TQString newPid = m_pidCombo->currentText().stripWhiteSpace();
    if (newPid.isEmpty())
        return;

    // If the user selected the same pid, do nothing.
    if (!m_pid.isEmpty() && newPid == m_pid)
        return;

    // Clear cached/rendered data so the UI reflects the new selection immediately.
    m_lastJson = TQString();
    m_dataLoaded = 0;
    if (m_name)
        m_name->setText("loading...");
    if (m_args)
        m_args->setText("loading...");
    if (m_cwd)
        m_cwd->setText(TQString());
    if (m_statm)
        m_statm->setText(TQString());

    if (m_textStatus) m_textStatus->setText("");
    if (m_textOpenFiles) m_textOpenFiles->setText("");
    if (m_textIO) m_textIO->setText("");
    if (m_textMaps) m_textMaps->setText("");
    if (m_textStack) m_textStack->setText("");
    if (m_textEnv) m_textEnv->setText("");

    // Only restart monitoring if we are currently running.
    if (!m_running)
        return;

    stopMonitoring();
    startMonitoring();
}

void ProcessDetailsDialog::onTabChanged(TQWidget*)
{
    if (m_lastJson.isEmpty())
        return;
    loadDataJson(m_lastJson);
}
