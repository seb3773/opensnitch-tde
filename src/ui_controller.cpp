#include "ui_controller.h"
#include "database.h"
#include "config.h"
#include "embedded_icons.h"

#include <ntqapplication.h>
#include <ntqdatetime.h>
#include <stdio.h>
#include <time.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static inline void purgeOldConnectionsIfNeeded(Database* db)
{
    if (!db || !db->isOpen())
        return;

    Config* cfg = Config::get();
    if (!cfg)
        return;

    // Only relevant for the in-memory DB mode.
    int dbType = cfg->getInt(Config::KEY_DB_TYPE, (int)Config::DB_TYPE_MEMORY);
    if (dbType != (int)Config::DB_TYPE_MEMORY)
        return;

    if (!cfg->getBool(Config::KEY_DB_PURGE_OLDEST, false))
        return;

    int maxDays = cfg->getInt(Config::KEY_DB_MAX_DAYS, 1);
    if (maxDays <= 0)
        return;

    int intervalMin = cfg->getInt(Config::KEY_DB_PURGE_INTERVAL, 5);
    if (intervalMin <= 0)
        intervalMin = 1;

    static time_t s_lastPurge = 0;
    time_t now = time(0);
    if (s_lastPurge != 0) {
        const time_t minDelta = (time_t)intervalMin * 60;
        if (now - s_lastPurge < minDelta)
            return;
    }
    s_lastPurge = now;

    // time column is stored as "YYYY-MM-DD HH:MM:SS".
    // SQLite datetime('now', ...) uses the same canonical format.
    db->exec(TQString("DELETE FROM connections WHERE time < datetime('now','-%1 days')")
                 .arg(maxDays));

    // Free pages inside SQLite (may not return memory to OS, but prevents growth).
    db->exec("PRAGMA shrink_memory");
}

static inline TQString s2q(const std::string& s) { return TQString(s.c_str()); }

typedef void NotifyNotification_tqt;
typedef NotifyNotification_tqt* (*p_notify_notification_new_tqt)(const char* summary, const char* body, const char* icon);
typedef int (*p_notify_init_tqt)(const char* app_name);
typedef void (*p_notify_uninit_tqt)(void);
typedef int (*p_notify_notification_show_tqt)(NotifyNotification_tqt* n, void* error);
typedef void (*p_notify_notification_set_timeout_tqt)(NotifyNotification_tqt* n, int timeout_ms);
typedef void (*p_g_object_unref_tqt)(void* obj);

static void* tqt_libnotify_handle = 0;
static void* tqt_libgobject_handle = 0;
static p_notify_init_tqt tqt_notify_init = 0;
static p_notify_uninit_tqt tqt_notify_uninit = 0;
static p_notify_notification_new_tqt tqt_notify_notification_new = 0;
static p_notify_notification_show_tqt tqt_notify_notification_show = 0;
static p_notify_notification_set_timeout_tqt tqt_notify_notification_set_timeout = 0;
static p_g_object_unref_tqt tqt_g_object_unref = 0;
static int tqt_libnotify_inited = 0;

static int tqt_try_init_libnotify()
{
    if (tqt_libnotify_inited)
        return (tqt_notify_init && tqt_notify_notification_new && tqt_notify_notification_show && tqt_g_object_unref) ? 1 : 0;
    tqt_libnotify_inited = 1;

    const char* cands[] = {
        "libnotify.so.4",
        "libnotify.so.5",
        "libnotify.so.0",
        "libnotify.so",
        0
    };
    for (int i = 0; cands[i]; i++) {
        tqt_libnotify_handle = dlopen(cands[i], RTLD_LAZY | RTLD_LOCAL);
        if (tqt_libnotify_handle)
            break;
    }
    if (!tqt_libnotify_handle)
        return 0;

    tqt_notify_init = (p_notify_init_tqt)dlsym(tqt_libnotify_handle, "notify_init");
    tqt_notify_uninit = (p_notify_uninit_tqt)dlsym(tqt_libnotify_handle, "notify_uninit");
    tqt_notify_notification_new = (p_notify_notification_new_tqt)dlsym(tqt_libnotify_handle, "notify_notification_new");
    tqt_notify_notification_show = (p_notify_notification_show_tqt)dlsym(tqt_libnotify_handle, "notify_notification_show");
    tqt_notify_notification_set_timeout = (p_notify_notification_set_timeout_tqt)dlsym(tqt_libnotify_handle, "notify_notification_set_timeout");

    // g_object_unref lives in libgobject-2.0, and may not be loaded yet.
    tqt_g_object_unref = (p_g_object_unref_tqt)dlsym(RTLD_DEFAULT, "g_object_unref");
    if (!tqt_g_object_unref) {
        const char* gcands[] = {
            "libgobject-2.0.so.0",
            "libgobject-2.0.so",
            0
        };
        for (int i = 0; gcands[i]; i++) {
            tqt_libgobject_handle = dlopen(gcands[i], RTLD_LAZY | RTLD_LOCAL);
            if (tqt_libgobject_handle)
                break;
        }
        if (tqt_libgobject_handle)
            tqt_g_object_unref = (p_g_object_unref_tqt)dlsym(tqt_libgobject_handle, "g_object_unref");
    }

    if (!tqt_notify_init || !tqt_notify_notification_new || !tqt_notify_notification_show || !tqt_g_object_unref)
        return 0;

    (void)tqt_notify_init("OpenSnitch-tde");
    return 1;
}

static const char* ensure_notify_icon_tmp()
{
    // Use XDG_RUNTIME_DIR (e.g. /run/user/1000) to avoid TOCTOU on /tmp.
    // Falls back to /tmp if not set.
    static char s_path[256] = {0};
    static int inited = 0;
    if (inited)
        return s_path[0] ? s_path : 0;
    inited = 1;

    const char* rtdir = getenv("XDG_RUNTIME_DIR");
    if (rtdir && rtdir[0])
        snprintf(s_path, sizeof(s_path), "%s/opensnitch_icon.png", rtdir);
    else
        snprintf(s_path, sizeof(s_path), "/tmp/opensnitch_icon_%d.png", (int)getuid());

    if (access(s_path, R_OK) == 0)
        return s_path;

    int fd = open(s_path, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0600);
    if (fd < 0) {
        // File may have been created between access() and open(); try without O_EXCL
        fd = open(s_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    }
    if (fd < 0)
        return 0;

    const unsigned char* data = opensnitch_icon_png;
    const int len = (int)opensnitch_icon_png_len;
    ssize_t wr = write(fd, (const void*)data, (size_t)len);
    (void)wr;
    close(fd);
    return s_path;
}

static void tqt_send_notify(const TQString& summary, const TQString& body)
{
    if (!tqt_try_init_libnotify())
        return;

    const char* iconPath = ensure_notify_icon_tmp();
    const TQCString s = summary.utf8();
    const TQCString b = body.utf8();

    NotifyNotification_tqt* n = tqt_notify_notification_new(s.data(), b.data(), iconPath);
    if (!n)
        return;
    if (tqt_notify_notification_set_timeout)
        tqt_notify_notification_set_timeout(n, 8000);
    (void)tqt_notify_notification_show(n, 0);
    tqt_g_object_unref(n);
}

static inline protocol::Rule build_default_rule_from_config(const protocol::Connection& conn)
{
    protocol::Rule rule;
    Config* cfg = Config::get();

    TQString procPath = s2q(conn.process_path());
    TQString procName = procPath;
    int lastSlash = procName.findRev('/');
    if (lastSlash >= 0)
        procName = procName.mid(lastSlash + 1);
    rule.set_name(procName.latin1());
    rule.set_enabled(true);
    rule.set_precedence(false);

    int actIdx = cfg ? cfg->getInt(Config::KEY_DEFAULT_ACTION, (int)Config::ACTION_DROP_IDX) : (int)Config::ACTION_DROP_IDX;
    if (actIdx == (int)Config::ACTION_ALLOW_IDX)
        rule.set_action(Config::ACTION_ALLOW);
    else if (actIdx == (int)Config::ACTION_REJECT_IDX)
        rule.set_action(Config::ACTION_REJECT);
    else
        rule.set_action(Config::ACTION_DENY);

    int durIdx = cfg ? cfg->getInt(Config::KEY_DEFAULT_DURATION, (int)Config::DEFAULT_DURATION_IDX) : (int)Config::DEFAULT_DURATION_IDX;
    const char* durations[] = {
        Config::DURATION_ONCE, Config::DURATION_30S, Config::DURATION_5M,
        Config::DURATION_15M, Config::DURATION_30M, Config::DURATION_1H,
        Config::DURATION_12H, Config::DURATION_UNTIL_RESTART, Config::DURATION_ALWAYS
    };
    if (durIdx >= 0 && durIdx < 9)
        rule.set_duration(durations[durIdx]);
    else
        rule.set_duration(Config::DURATION_UNTIL_RESTART);

    int targetIdx = cfg ? cfg->getInt(Config::KEY_DEFAULT_TARGET, 0) : 0;
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

    protocol::Operator* op = rule.mutable_operator_();
    op->set_sensitive(false);
    if (targetIdx < 0 || targetIdx >= 6)
        targetIdx = 0;
    op->set_type(types[targetIdx]);
    op->set_operand(operands[targetIdx]);

    TQString dataVal;
    switch (targetIdx) {
    case 0: dataVal = s2q(conn.process_path()); break;
    case 1: dataVal = s2q(conn.process_args_size() > 0 ? conn.process_args(0) : std::string()); break;
    case 2: dataVal = TQString::number(conn.dst_port()); break;
    case 3: dataVal = TQString::number(conn.user_id()); break;
    case 4: dataVal = s2q(conn.dst_ip()); break;
    case 5: dataVal = TQString::number(conn.process_id()); break;
    default: dataVal = s2q(conn.process_path()); break;
    }
    op->set_data(dataVal.latin1());
    return rule;
}

UIController::UIController(TQWidget* parent, const char* name)
    : TQWidget(parent, name),
      m_promptDialog(0)
{
    // This widget must be the main widget so TQApplication::mainWidget()
    // returns it, and postEvent(mainWidget(), ...) delivers here.
    hide(); // never visible
}

UIController::~UIController()
{
}

void UIController::customEvent(TQCustomEvent* ev)
{
    if ((int)ev->type() == PingStatsEventId) {
        PingStatsEvent* pse = static_cast<PingStatsEvent*>(ev);
        populateStats(pse->peer(), pse->request().stats());
        return;
    }

    if ((int)ev->type() == SubscribeEventId) {
        SubscribeEvent* se = static_cast<SubscribeEvent*>(ev);
        emit daemonSubscribed(se->peer(), se->firewallRunning());
        return;
    }

    if ((int)ev->type() == NotificationReplyEventId) {
        NotificationReplyEvent* ne = static_cast<NotificationReplyEvent*>(ev);
        const protocol::NotificationReply& r = ne->reply();
        emit notificationReply(ne->peer(), (unsigned long long)r.id(), (int)r.code(), TQString(r.data().c_str()));
        return;
    }

    if ((int)ev->type() != AskRuleEventId)
        return;

    AskRuleEvent* askEv = static_cast<AskRuleEvent*>(ev);
    AskRuleCallData* callData = askEv->callData();
    if (!callData || !m_promptDialog)
        return;

    const protocol::Connection& conn = callData->connection();
    TQString peer = callData->peerAddr();

    // Determine if the connection is from a local node
    bool isLocal = true;
    Nodes* nodes = Nodes::instance();
    if (nodes->count() > 1) {
        TQString proto, addr;
        Nodes::splitPeer(peer, proto, addr);
        Nodes::NodeData* nd = nodes->node(addr);
        if (nd && !addr.isEmpty())
            isLocal = false;
    }

    Config* cfg = Config::get();
    const int popupsDisabled = (cfg && cfg->getBool(Config::KEY_DISABLE_POPUPS, false)) ? 1 : 0;
    protocol::Rule rule;

    if (!popupsDisabled) {
        // Alert tray icon when prompt is shown
        emit alertRequested();
        // Show prompt dialog and get user response
        rule = m_promptDialog->promptUser(conn, isLocal, peer);
        // Restore tray icon after prompt closes
        emit alertFinished();
    } else {
        // Apply default action and notify.
        rule = build_default_rule_from_config(conn);

        TQString proc = s2q(conn.process_path());
        int ls = proc.findRev('/');
        if (ls >= 0)
            proc = proc.mid(ls + 1);
        TQString dst = s2q(conn.dst_host());
        if (dst.isEmpty())
            dst = s2q(conn.dst_ip());
        const TQString proto = s2q(conn.protocol()).upper();
        const TQString port = TQString::number(conn.dst_port());

        TQString verb;
        {
            const TQString a = s2q(rule.action()).lower();
            if (a == "allow")
                verb = "allowed";
            else if (a == "reject")
                verb = "rejected";
            else
                verb = "denied";
        }

        // Keep it short.
        const TQString summary = TQString("Connection %1 automatically").arg(verb);
        const TQString body = TQString("%1 -> %2:%3 (%4)").arg(proc).arg(dst).arg(port).arg(proto);
        tqt_send_notify(summary, body);
    }

    // Normalize peer address (matches Python get_addr: "unix:" -> "unix:/local")
    TQString nPeer = peer;
    {
        TQString p, a;
        Nodes::splitPeer(peer, p, a);
        nPeer = p + ":" + a;
    }

    // Save the rule to the database
    TQString time = TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    Rules::instance()->add(time, nPeer, rule);

    // Log the connection event to the database
    Database* db = Database::instance();
    if (db && db->isOpen()) {
        TQValueList<TQVariant> vals;
        vals << TQVariant(time)
             << TQVariant(nPeer)
             << TQVariant(s2q(rule.action()))
             << TQVariant(s2q(conn.protocol()))
             << TQVariant(s2q(conn.src_ip()))
             << TQVariant(TQString::number(conn.src_port()))
             << TQVariant(s2q(conn.dst_ip()))
             << TQVariant(s2q(conn.dst_host()))
             << TQVariant(TQString::number(conn.dst_port()))
             << TQVariant(TQString::number(conn.user_id()))
             << TQVariant(TQString::number(conn.process_id()))
             << TQVariant(s2q(conn.process_path()))
             << TQVariant(TQString())
             << TQVariant(s2q(conn.process_cwd()))
             << TQVariant(s2q(rule.name()));
        db->insert("connections",
                   "(time, node, action, protocol, src_ip, src_port, "
                   "dst_ip, dst_host, dst_port, uid, pid, process, "
                   "process_args, process_cwd, rule)",
                   vals);
    }

    // Send the rule back to the gRPC thread
    callData->finishWithRule(rule);
    callData->releaseFromGui();
}

// Matches Python _populate_stats: insert events from daemon stats into DB,
// update nodes table, and update detail tables (hosts, procs, addrs, ports, users).
// Complexity: O(n) where n = number of events in stats
// Thread safety: called from GUI thread only (via customEvent)
void UIController::populateStats(const TQString& peer, const protocol::Statistics& stats)
{
    Database* db = Database::instance();
    if (!db || !db->isOpen())
        return;

    purgeOldConnectionsIfNeeded(db);

    const int inTx = db->exec("BEGIN TRANSACTION") ? 1 : 0;
    int okAll = 1;

    // Normalize peer address (matches Python get_addr: "unix:" -> "unix:/local")
    TQString proto, addr;
    Nodes::splitPeer(peer, proto, addr);
    TQString nodeAddr = proto + ":" + addr;

    // Update nodes table (matches Python: db.insert("nodes", ...))
    Nodes* nodes = Nodes::instance();
    Nodes::NodeData* nd = nodes->node(nodeAddr);
    TQString hostname = nd && nd->hasConfig ? s2q(nd->config.name()) : TQString();
    TQString version  = nd && nd->hasConfig ? s2q(nd->config.version()) : TQString();

    // Format uptime as "Xh Ym Zs" (matches Python timedelta)
    uint64_t uptimeSecs = stats.uptime();
    uint64_t hours   = uptimeSecs / 3600;
    uint64_t minutes = (uptimeSecs % 3600) / 60;
    uint64_t seconds = uptimeSecs % 60;
    TQString uptimeStr;
    if (hours > 24) {
        uint64_t days = hours / 24;
        hours = hours % 24;
        uptimeStr = TQString("%1d, %2h %3m %4s").arg(days).arg(hours).arg(minutes).arg(seconds);
    } else {
        uptimeStr = TQString("%1h %2m %3s").arg(hours).arg(minutes).arg(seconds);
    }

    TQString lastConn = TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    {
        TQValueList<TQVariant> nvals;
        nvals << TQVariant(nodeAddr)
              << TQVariant(TQString("online"))
              << TQVariant(hostname)
              << TQVariant(s2q(stats.daemon_version()))
              << TQVariant(uptimeStr)
              << TQVariant(TQString::number(stats.rules()))
              << TQVariant(TQString::number(stats.connections()))
              << TQVariant(TQString::number(stats.dropped()))
              << TQVariant(version)
              << TQVariant(lastConn);
        okAll = (db->insert("nodes",
                            "(addr, status, hostname, daemon_version, daemon_uptime, "
                            "daemon_rules, cons, cons_dropped, version, last_connection)",
                            nvals, "REPLACE") ? 1 : 0) & okAll;
    }

    // Insert events from stats into connections table (matches Python loop over stats.events)
    bool needRefresh = false;
    for (int i = 0; i < stats.events_size(); i++) {
        const protocol::Event& ev = stats.events(i);

        // Dedup: skip if we've already seen this unixnano for this addr
        if (m_lastStats[addr].contains(ev.unixnano()))
            continue;

        needRefresh = true;

        // Convert unixnano to datetime string
        time_t evTime = static_cast<time_t>(ev.unixnano() / 1000000000LL);
        struct tm tm_buf;
        struct tm* tm_val = localtime_r(&evTime, &tm_buf);
        char timeBuf[32];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_val);

        const protocol::Connection& conn = ev.connection();
        const protocol::Rule& rule = ev.rule();

        // Join process_args
        TQString procArgs;
        for (int j = 0; j < conn.process_args_size(); j++) {
            if (j > 0) procArgs += " ";
            procArgs += s2q(conn.process_args(j));
        }

        TQValueList<TQVariant> vals;
        vals << TQVariant(TQString(timeBuf))
             << TQVariant(nodeAddr)
             << TQVariant(s2q(rule.action()))
             << TQVariant(s2q(conn.protocol()))
             << TQVariant(s2q(conn.src_ip()))
             << TQVariant(TQString::number(conn.src_port()))
             << TQVariant(s2q(conn.dst_ip()))
             << TQVariant(s2q(conn.dst_host()))
             << TQVariant(TQString::number(conn.dst_port()))
             << TQVariant(TQString::number(conn.user_id()))
             << TQVariant(TQString::number(conn.process_id()))
             << TQVariant(s2q(conn.process_path()))
             << TQVariant(procArgs)
             << TQVariant(s2q(conn.process_cwd()))
             << TQVariant(s2q(rule.name()));
        okAll = (db->insert("connections",
                            "(time, node, action, protocol, src_ip, src_port, "
                            "dst_ip, dst_host, dst_port, uid, pid, process, "
                            "process_args, process_cwd, rule)",
                            vals, "IGNORE") ? 1 : 0) & okAll;
    }

    // Update detail tables (hosts, procs, addrs, ports, users)
    // Matches Python _populate_stats_details + _populate_stats_events
    struct DetailTable {
        const char* tableName;
        const google::protobuf::Map<std::string, uint64_t>& items;
    };
    // We need to iterate the protobuf maps
    {
        // hosts
        const google::protobuf::Map<std::string, uint64_t>& byHost = stats.by_host();
        TQValueList<TQVariant> fields, values;
        for (auto it = byHost.begin(); it != byHost.end(); ++it) {
            TQString what = s2q(it->first);
            if (m_lastItems["hosts"][addr].contains(what) &&
                m_lastItems["hosts"][addr][what] == it->second)
                continue;
            fields << TQVariant(what);
            values << TQVariant(TQString::number(it->second));
        }
        if (fields.count() > 0) {
            okAll = (db->insertBatch("hosts", "(what, hits)", fields, values) ? 1 : 0) & okAll;
            needRefresh = true;
            // Update lastItems
            for (auto it = byHost.begin(); it != byHost.end(); ++it)
                m_lastItems["hosts"][addr][s2q(it->first)] = it->second;
        }
    }
    {
        const google::protobuf::Map<std::string, uint64_t>& byExe = stats.by_executable();
        TQValueList<TQVariant> fields, values;
        for (auto it = byExe.begin(); it != byExe.end(); ++it) {
            TQString what = s2q(it->first);
            if (m_lastItems["procs"][addr].contains(what) &&
                m_lastItems["procs"][addr][what] == it->second)
                continue;
            fields << TQVariant(what);
            values << TQVariant(TQString::number(it->second));
        }
        if (fields.count() > 0) {
            okAll = (db->insertBatch("procs", "(what, hits)", fields, values) ? 1 : 0) & okAll;
            needRefresh = true;
            for (auto it = byExe.begin(); it != byExe.end(); ++it)
                m_lastItems["procs"][addr][s2q(it->first)] = it->second;
        }
    }
    {
        const google::protobuf::Map<std::string, uint64_t>& byAddr = stats.by_address();
        TQValueList<TQVariant> fields, values;
        for (auto it = byAddr.begin(); it != byAddr.end(); ++it) {
            TQString what = s2q(it->first);
            if (m_lastItems["addrs"][addr].contains(what) &&
                m_lastItems["addrs"][addr][what] == it->second)
                continue;
            fields << TQVariant(what);
            values << TQVariant(TQString::number(it->second));
        }
        if (fields.count() > 0) {
            okAll = (db->insertBatch("addrs", "(what, hits)", fields, values) ? 1 : 0) & okAll;
            needRefresh = true;
            for (auto it = byAddr.begin(); it != byAddr.end(); ++it)
                m_lastItems["addrs"][addr][s2q(it->first)] = it->second;
        }
    }
    {
        const google::protobuf::Map<std::string, uint64_t>& byPort = stats.by_port();
        TQValueList<TQVariant> fields, values;
        for (auto it = byPort.begin(); it != byPort.end(); ++it) {
            TQString what = s2q(it->first);
            if (m_lastItems["ports"][addr].contains(what) &&
                m_lastItems["ports"][addr][what] == it->second)
                continue;
            fields << TQVariant(what);
            values << TQVariant(TQString::number(it->second));
        }
        if (fields.count() > 0) {
            okAll = (db->insertBatch("ports", "(what, hits)", fields, values) ? 1 : 0) & okAll;
            needRefresh = true;
            for (auto it = byPort.begin(); it != byPort.end(); ++it)
                m_lastItems["ports"][addr][s2q(it->first)] = it->second;
        }
    }
    {
        const google::protobuf::Map<std::string, uint64_t>& byUid = stats.by_uid();
        TQValueList<TQVariant> fields, values;
        for (auto it = byUid.begin(); it != byUid.end(); ++it) {
            TQString what = s2q(it->first);
            if (m_lastItems["users"][addr].contains(what) &&
                m_lastItems["users"][addr][what] == it->second)
                continue;
            fields << TQVariant(what);
            values << TQVariant(TQString::number(it->second));
        }
        if (fields.count() > 0) {
            okAll = (db->insertBatch("users", "(what, hits)", fields, values) ? 1 : 0) & okAll;
            needRefresh = true;
            for (auto it = byUid.begin(); it != byUid.end(); ++it)
                m_lastItems["users"][addr][s2q(it->first)] = it->second;
        }
    }

    if (inTx) {
        if (okAll)
            db->exec("COMMIT");
        else
            db->exec("ROLLBACK");
    }

    // Update dedup list (matches Python: self._last_stats[addr] = [e.unixnano for e in stats.events])
    m_lastStats[addr].clear();
    for (int i = 0; i < stats.events_size(); i++)
        m_lastStats[addr].append(stats.events(i).unixnano());

    // Signal main window to refresh (matches Python: stats_dialog.update() with stats counters)
    emit statsUpdated(needRefresh, (int)stats.connections(), (int)stats.dropped(), (int)stats.rules(), uptimeStr);
}
