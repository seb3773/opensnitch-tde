#include "config.h"

#include <tqtsettings.h>

#include <stdlib.h>

// --- static constants ---
const char* Config::HELP_URL          = "https://github.com/evilsocket/opensnitch/wiki/";
const char* Config::HELP_RULES_URL    = "https://github.com/evilsocket/opensnitch/wiki/Rules";
const char* Config::HELP_CONFIG_URL   = "https://github.com/evilsocket/opensnitch/wiki/Configurations";

const char* Config::ACTION_ALLOW      = "allow";
const char* Config::ACTION_DENY       = "deny";
const char* Config::ACTION_REJECT     = "reject";
const char* Config::ACTION_DROP       = "drop";

const char* Config::DURATION_ONCE          = "once";
const char* Config::DURATION_30S           = "30s";
const char* Config::DURATION_5M            = "5m";
const char* Config::DURATION_15M           = "15m";
const char* Config::DURATION_30M           = "30m";
const char* Config::DURATION_1H            = "1h";
const char* Config::DURATION_12H           = "12h";
const char* Config::DURATION_UNTIL_RESTART = "until restart";
const char* Config::DURATION_ALWAYS        = "always";

const char* Config::RULE_TYPE_SIMPLE  = "simple";
const char* Config::RULE_TYPE_LIST    = "list";
const char* Config::RULE_TYPE_LISTS   = "lists";
const char* Config::RULE_TYPE_REGEXP = "regexp";
const char* Config::RULE_TYPE_NETWORK = "network";
const char* Config::RULE_TYPE_RANGE  = "range";

const char* Config::OPERAND_PROCESS_PATH    = "process.path";
const char* Config::OPERAND_PROCESS_COMMAND = "process.command";
const char* Config::OPERAND_PROCESS_ID      = "process.id";
const char* Config::OPERAND_DEST_IP         = "dest.ip";
const char* Config::OPERAND_DEST_HOST       = "dest.host";
const char* Config::OPERAND_DEST_PORT       = "dest.port";
const char* Config::OPERAND_SOURCE_IP       = "source.ip";
const char* Config::OPERAND_SOURCE_PORT     = "source.port";
const char* Config::OPERAND_PROTOCOL        = "protocol";
const char* Config::OPERAND_USER_ID         = "user.id";
const char* Config::OPERAND_IFACE_OUT       = "iface.out";
const char* Config::OPERAND_IFACE_IN        = "iface.in";

const char* Config::OPERAND_LIST_DOMAINS        = "lists.domains";
const char* Config::OPERAND_LIST_DOMAINS_REGEXP = "lists.domains_regexp";
const char* Config::OPERAND_LIST_IPS            = "lists.ips";
const char* Config::OPERAND_LIST_NETS           = "lists.nets";

const char* Config::KEY_DEFAULT_TIMEOUT          = "global/default_timeout";
const char* Config::KEY_DEFAULT_ACTION           = "global/default_action";
const char* Config::KEY_DEFAULT_DURATION         = "global/default_duration";
const char* Config::KEY_DEFAULT_TARGET           = "global/default_target";
const char* Config::KEY_DISABLE_POPUPS           = "global/disable_popups";
const char* Config::KEY_SERVER_ADDR              = "global/server_address";
const char* Config::KEY_SERVER_MAX_WORKERS       = "global/max_workers";
const char* Config::KEY_SERVER_MAX_CLIENTS       = "global/max_clients";
const char* Config::KEY_SERVER_KEEPALIVE         = "global/server_keepalive";
const char* Config::KEY_SERVER_KEEPALIVE_TIMEOUT = "global/server_keepalive_timeout";
const char* Config::KEY_SERVER_LOG_LEVEL         = "global/server_log_level";
const char* Config::KEY_SERVER_LOG_FILE          = "global/server_log_file";
const char* Config::KEY_SERVER_MAX_MESSAGE_LENGTH = "global/server_max_message_length";
const char* Config::KEY_DB_TYPE                  = "database/type";
const char* Config::KEY_DB_FILE                  = "database/file";
const char* Config::KEY_DB_PURGE_OLDEST          = "database/purge_oldest";
const char* Config::KEY_DB_MAX_DAYS              = "database/max_days";
const char* Config::KEY_DB_PURGE_INTERVAL        = "database/purge_interval";
const char* Config::KEY_DB_JRNL_WAL              = "database/jrnl_wal";
const char* Config::KEY_NOTIFICATIONS_ENABLED     = "notifications/enabled";
const char* Config::KEY_NOTIFICATIONS_TYPE       = "notifications/type";
const char* Config::KEY_UI_DARK_MODE             = "ui/dark_mode";
const char* Config::KEY_UI_TOOLBAR_ICON_SIZE     = "ui/toolbar_icon_size";
const char* Config::KEY_UI_LOGS_TOOLBAR_ICON_SIZE = "ui/logs_toolbar_icon_size";
const char* Config::KEY_POPUP_POSITION           = "global/default_popup_position";
const char* Config::KEY_POPUP_ADVANCED           = "global/default_popup_advanced";
const char* Config::KEY_POPUP_ADVANCED_UID       = "global/default_popup_advanced_uid";
const char* Config::KEY_POPUP_ADVANCED_DSTPORT   = "global/default_popup_advanced_dstport";
const char* Config::KEY_POPUP_ADVANCED_DSTIP     = "global/default_popup_advanced_dstip";
const char* Config::KEY_INTERCEPTION_ENABLED     = "global/interception_enabled";
const char* Config::KEY_PERSIST_INTERCEPTION_STATE = "global/persist_interception_state";
const char* Config::KEY_IGNORE_RULES             = "global/default_ignore_rules";
const char* Config::KEY_IGNORE_TEMPORARY_RULES   = "global/default_ignore_temporary_rules";
const char* Config::KEY_HIDE_SYSTRAY_WARN        = "global/hide_systray_warning";
const char* Config::KEY_STATS_SHOW_COLUMNS_CONNECTIONS = "statsDialog/show_columns_connections";
const char* Config::KEY_STATS_REFRESH_INTERVAL   = "statsDialog/refresh_interval";
const char* Config::KEY_AUTH_TYPE                = "auth/type";
const char* Config::KEY_AUTH_CA_CERT             = "auth/cacert";
const char* Config::KEY_AUTH_CERT                = "auth/cert";
const char* Config::KEY_AUTH_CERTKEY             = "auth/certkey";

Config* Config::s_instance = 0;

Config* Config::init()
{
    if (!s_instance)
        s_instance = new Config();
    return s_instance;
}

Config* Config::get()
{
    if (!s_instance)
        s_instance = new Config();
    return s_instance;
}

Config::Config()
    : m_settings(new TQtSettings("opensnitch", "settings"))
{
    initDefaults();
}

Config::~Config()
{
    delete m_settings;
}

void Config::initDefaults()
{
    if (!hasKey(KEY_DEFAULT_TIMEOUT))
        setSetting(KEY_DEFAULT_TIMEOUT, DEFAULT_TIMEOUT);
    if (!hasKey(KEY_DEFAULT_ACTION))
        setSetting(KEY_DEFAULT_ACTION, (int)ACTION_DROP_IDX);
    if (!hasKey(KEY_DEFAULT_DURATION))
        setSetting(KEY_DEFAULT_DURATION, (int)DEFAULT_DURATION_IDX);
    if (!hasKey(KEY_DEFAULT_TARGET))
        setSetting(KEY_DEFAULT_TARGET, 0);
    const bool hadDbType = hasKey(KEY_DB_TYPE);
    if (!hadDbType)
        setSetting(KEY_DB_TYPE, (int)DB_TYPE_MEMORY);
    if (!hasKey(KEY_DB_FILE))
        setSetting(KEY_DB_FILE, TQString(":memory:"));
    if (!hasKey(KEY_DB_JRNL_WAL))
        setSetting(KEY_DB_JRNL_WAL, false);

    // If we're initializing a fresh config (no DB type existed yet),
    // default to bounded in-memory history to prevent unbounded RAM growth.
    // These defaults are only written on first run.
    if (!hadDbType) {
        if (!hasKey(KEY_DB_PURGE_OLDEST))
            setSetting(KEY_DB_PURGE_OLDEST, true);
        if (!hasKey(KEY_DB_MAX_DAYS))
            setSetting(KEY_DB_MAX_DAYS, 1);
        if (!hasKey(KEY_DB_PURGE_INTERVAL))
            setSetting(KEY_DB_PURGE_INTERVAL, 5);
    }
}

TQVariant Config::getSetting(const TQString& key, const TQVariant& def)
{
    return m_settings->value(key, def);
}

void Config::setSetting(const TQString& key, const TQVariant& value)
{
    m_settings->setValue(key, value);
    m_settings->sync();
}

bool Config::getBool(const TQString& key, bool def)
{
    TQVariant v = m_settings->value(key, TQVariant(def));
    return v.toBool();
}

int Config::getInt(const TQString& key, int def)
{
    TQVariant v = m_settings->value(key, TQVariant(def));
    bool ok = false;
    int result = v.toInt(&ok);
    return ok ? result : def;
}

TQString Config::getString(const TQString& key, const TQString& def)
{
    TQVariant v = m_settings->value(key, TQVariant(def));
    return v.toString();
}

bool Config::hasKey(const TQString& key)
{
    // TQtSettings returns invalid QVariant for missing keys
    TQVariant v = m_settings->value(key);
    return v.isValid();
}

TQString Config::getDefaultAction()
{
    int idx = getInt(KEY_DEFAULT_ACTION, ACTION_DROP_IDX);
    if (idx == ACTION_ALLOW_IDX)
        return TQString(ACTION_ALLOW);
    return TQString(ACTION_DENY);
}

int Config::getMaxMsgLength()
{
    TQString val = getString("global/server_max_message_length", "4MiB");
    if (val == "8MiB")  return 8388608;
    if (val == "16MiB") return 16777216;
    return 4194304; // 4MiB default
}

void Config::sync()
{
    m_settings->sync();
}

void Config::openUrl(const TQString& url)
{
    if (url.isEmpty())
        return;
    TQString cmd = TQString("xdg-open '%1' &").arg(url);
    system(cmd.latin1());
}
