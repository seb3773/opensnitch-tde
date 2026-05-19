#ifndef OPENSNITCH_CONFIG_H
#define OPENSNITCH_CONFIG_H

#include <ntqstring.h>
#include <ntqvariant.h>
#include <ntqmap.h>

// Complexity: O(1) for gets/sets, O(n) for sync
// Dependencies: tqtsettings (tde-extended)
// Alignment: none required

class TQtSettings;

class Config {
public:
    static Config* init();
    static Config* get();

    // URL constants
    static const char* HELP_URL;
    static const char* HELP_RULES_URL;
    static const char* HELP_CONFIG_URL;

    // Action constants
    static const char* ACTION_ALLOW;
    static const char* ACTION_DENY;
    static const char* ACTION_REJECT;
    static const char* ACTION_DROP;

    // Duration constants
    static const char* DURATION_ONCE;
    static const char* DURATION_30S;
    static const char* DURATION_5M;
    static const char* DURATION_15M;
    static const char* DURATION_30M;
    static const char* DURATION_1H;
    static const char* DURATION_12H;
    static const char* DURATION_UNTIL_RESTART;
    static const char* DURATION_ALWAYS;

    // Duration index (default = until restart)
    enum DurationIndex {
        DUR_ONCE = 0,
        DUR_30S  = 1,
        DUR_5M   = 2,
        DUR_15M  = 3,
        DUR_30M  = 4,
        DUR_1H   = 5,
        DUR_12H  = 6,
        DUR_UNTIL_RESTART = 7,
        DUR_ALWAYS = 8,
        DEFAULT_DURATION_IDX = DUR_UNTIL_RESTART
    };

    // Action index
    enum ActionIndex {
        ACTION_DROP_IDX  = 0,
        ACTION_ALLOW_IDX = 1,
        ACTION_REJECT_IDX = 2
    };

    // Rule type constants
    static const char* RULE_TYPE_SIMPLE;
    static const char* RULE_TYPE_LIST;
    static const char* RULE_TYPE_LISTS;
    static const char* RULE_TYPE_REGEXP;
    static const char* RULE_TYPE_NETWORK;
    static const char* RULE_TYPE_RANGE;

    // Operand constants
    static const char* OPERAND_PROCESS_PATH;
    static const char* OPERAND_PROCESS_COMMAND;
    static const char* OPERAND_PROCESS_ID;
    static const char* OPERAND_DEST_IP;
    static const char* OPERAND_DEST_HOST;
    static const char* OPERAND_DEST_PORT;
    static const char* OPERAND_SOURCE_IP;
    static const char* OPERAND_SOURCE_PORT;
    static const char* OPERAND_PROTOCOL;
    static const char* OPERAND_USER_ID;
    static const char* OPERAND_IFACE_OUT;
    static const char* OPERAND_IFACE_IN;

    // List operands (match Python Debian UI strings)
    static const char* OPERAND_LIST_DOMAINS;
    static const char* OPERAND_LIST_DOMAINS_REGEXP;
    static const char* OPERAND_LIST_IPS;
    static const char* OPERAND_LIST_NETS;

    // Settings key constants
    static const char* KEY_DEFAULT_TIMEOUT;
    static const char* KEY_DEFAULT_ACTION;
    static const char* KEY_DEFAULT_DURATION;
    static const char* KEY_DEFAULT_TARGET;
    static const char* KEY_DISABLE_POPUPS;
    static const char* KEY_SERVER_ADDR;
    static const char* KEY_SERVER_MAX_WORKERS;
    static const char* KEY_SERVER_MAX_CLIENTS;
    static const char* KEY_SERVER_KEEPALIVE;
    static const char* KEY_SERVER_KEEPALIVE_TIMEOUT;
    static const char* KEY_SERVER_LOG_LEVEL;
    static const char* KEY_SERVER_LOG_FILE;
    static const char* KEY_SERVER_MAX_MESSAGE_LENGTH;
    static const char* KEY_DB_TYPE;
    static const char* KEY_DB_FILE;
    static const char* KEY_DB_PURGE_OLDEST;
    static const char* KEY_DB_MAX_DAYS;
    static const char* KEY_DB_PURGE_INTERVAL;
    static const char* KEY_DB_JRNL_WAL;
    static const char* KEY_NOTIFICATIONS_ENABLED;
    static const char* KEY_NOTIFICATIONS_TYPE;
    static const char* KEY_UI_DARK_MODE;
    static const char* KEY_UI_TOOLBAR_ICON_SIZE;
    static const char* KEY_UI_LOGS_TOOLBAR_ICON_SIZE;
    static const char* KEY_POPUP_POSITION;
    static const char* KEY_POPUP_ADVANCED;
    static const char* KEY_POPUP_ADVANCED_UID;
    static const char* KEY_POPUP_ADVANCED_DSTPORT;
    static const char* KEY_POPUP_ADVANCED_DSTIP;
    static const char* KEY_INTERCEPTION_ENABLED;
    static const char* KEY_PERSIST_INTERCEPTION_STATE;
    static const char* KEY_IGNORE_RULES;
    static const char* KEY_IGNORE_TEMPORARY_RULES;
    static const char* KEY_HIDE_SYSTRAY_WARN;
    static const char* KEY_STATS_SHOW_COLUMNS_CONNECTIONS;
    static const char* KEY_STATS_REFRESH_INTERVAL;
    static const char* KEY_AUTH_TYPE;
    static const char* KEY_AUTH_CA_CERT;
    static const char* KEY_AUTH_CERT;
    static const char* KEY_AUTH_CERTKEY;

    // Notification types
    enum NotificationType {
        NOTIF_TYPE_SYSTEM = 0,
        NOTIF_TYPE_QT     = 1
    };

    // DB types
    enum DBType {
        DB_TYPE_MEMORY = 0,
        DB_TYPE_FILE   = 1
    };

    // Default timeout in seconds
    static const int DEFAULT_TIMEOUT = 30;

    // Settings access
    TQVariant getSetting(const TQString& key, const TQVariant& def = TQVariant());
    void setSetting(const TQString& key, const TQVariant& value);
    bool getBool(const TQString& key, bool def = false);
    int getInt(const TQString& key, int def = 0);
    TQString getString(const TQString& key, const TQString& def = TQString());
    bool hasKey(const TQString& key);

    TQString getDefaultAction();
    int getMaxMsgLength();

    static void openUrl(const TQString& url);

    void sync();
    static void destroy();

private:
    Config();
    ~Config();

    static Config* s_instance;
    TQtSettings* m_settings;

    void initDefaults();
};

#endif // OPENSNITCH_CONFIG_H
