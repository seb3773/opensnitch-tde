#include <tqapplication.h>
#include <tqstring.h>
#include <tqstringlist.h>
#include <tqtranslator.h>
#include <tqtextcodec.h>
#include <tdeapplication.h>
#include <tdeaboutdata.h>
#include <tdecmdlineargs.h>
#include <tdelocale.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sqlite3.h>

#include "config.h"
#include "database.h"
#include "grpc_server.h"
#include "systray.h"
#include "prompt_dialog.h"
#include "ui_controller.h"
#include "main_window.h"
#include "nodes.h"
#include "rules.h"

static GRpcServer* g_server = 0;
TQApplication* g_app = 0;

static void shutdown_runtime()
{
    static bool s_done = false;
    if (s_done)
        return;
    s_done = true;

    if (g_server) {
        g_server->stop();
        delete g_server;
        g_server = 0;
    }
    Database::instance()->close();
    Database::destroy();
    Nodes::destroy();
    Rules::destroy();
}

static void sigint_handler(int)
{
    shutdown_runtime();
    _exit(0);
}

static void create_socket_dirs()
{
    const char* paths[] = {
        "/tmp",
        "/run/user",
        NULL
    };
    // Ensure /tmp exists (it always does)
    // For user-specific sockets, create the directory
    TQString uid;
    uid.setNum(getuid());
    TQString userDir = TQString("/run/user/%1/opensnitch").arg(uid);
    mkdir(userDir.latin1(), 0700);
}

int main(int argc, char** argv)
{
    // Setup signal handlers
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    // Parse our custom args and strip them from argv before TDE sees them
    TQString socketAddr;
    int maxWorkers = 20;
    bool debug = false;
    bool startBg = false;
    int tde_argc = 1; // keep argv[0]

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socketAddr = TQString::fromLocal8Bit(argv[++i]);
        } else if (strcmp(argv[i], "--max-workers") == 0 && i + 1 < argc) {
            maxWorkers = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        } else if (strcmp(argv[i], "--background") == 0) {
            startBg = true;
        } else {
            argv[tde_argc++] = argv[i];
        }
    }
    argc = tde_argc;

    // Initialize config
    Config* cfg = Config::init();

    // Optional log redirection to file
    {
        TQString logFile = cfg->getString(Config::KEY_SERVER_LOG_FILE, TQString()).stripWhiteSpace();
        if (!logFile.isEmpty()) {
            int fd = ::open(logFile.latin1(), O_WRONLY | O_CREAT | O_APPEND, 0640);
            if (fd >= 0) {
                ::dup2(fd, 1);
                ::dup2(fd, 2);
                ::close(fd);
                setvbuf(stdout, 0, _IOLBF, 0);
                setvbuf(stderr, 0, _IOLBF, 0);
            }
        }
    }

    fprintf(stdout, " ~ OpenSnitch TQt3 UI ~\n");
    fprintf(stdout, "------------------------\n");

    // Default socket
    if (socketAddr.isEmpty()) {
        socketAddr = cfg->getString(Config::KEY_SERVER_ADDR, "unix:///tmp/osui.sock");
        if (socketAddr.startsWith("unix://")) {
            TQString path = socketAddr.mid(7);
            TQString dir = path.left(path.findRev('/'));
            if (!dir.isEmpty() && access(dir.latin1(), F_OK) != 0) {
                fprintf(stderr, "WARNING: unix socket path does not exist, using default\n");
                socketAddr = "unix:///tmp/osui.sock";
            }
        }
    }

    // Handle unix-abstract format
    if (socketAddr.startsWith("unix:@")) {
        TQString abstract = socketAddr.mid(7);
        socketAddr = TQString("unix-abstract:%1").arg(abstract);
    }

    create_socket_dirs();

    // Initialize TDE application
    TDEAboutData aboutData("opensnitch-tqt", I18N_NOOP("OpenSnitch"),
                         "1.0", I18N_NOOP("Application firewall GUI"),
                         TDEAboutData::License_GPL_V3,
                         "2025 OpenSnitch contributors");
    TDECmdLineArgs::init(argc, argv, &aboutData);

    // Enable SQLite URI mode BEFORE any DB call (required for file::memory:?cache=shared)
    // Matches Python: QSQLITE_OPEN_URI support via sqlite3_config
    sqlite3_config(SQLITE_CONFIG_URI, 1);

    TDEApplication app;
    g_app = &app;

    // Don't auto-quit on last window close - we live in the tray
    // TQt3: connect lastWindowClosed to quit only when desired
    // By default TDEApplication does NOT quit on last window close

    // Initialize database
    Database* db = Database::instance();
    int dbType = cfg->getInt(Config::KEY_DB_TYPE, 0);
    TQString dbFile = cfg->getString(Config::KEY_DB_FILE, ":memory:");
    bool dbWal = cfg->getBool(Config::KEY_DB_JRNL_WAL, false);

    if (!db->initialize(dbType, dbFile, dbWal)) {
        fprintf(stderr, "FATAL: database initialization failed\n");
        return -1;
    }

    // Initialize nodes and rules
    Nodes* nodes = Nodes::instance();
    Rules* rules = Rules::instance();

    // Create system tray
    SysTray* tray = new SysTray();
    tray->show();

    if (!startBg) {
        // If tray not available, show main window after delay
        // For MVP, we don't have a main window yet
    }

    // Create UI controller (receives AskRule events from gRPC thread)
    UIController* controller = new UIController();
    app.setMainWidget(controller);

    // Create main window
    MainWindow* mainWin = new MainWindow();
    // daemonConnected state is set by onDaemonSubscribed when daemon connects

    // Create prompt dialog
    PromptDialog* prompt = new PromptDialog();
    controller->setPromptDialog(prompt);

    // Start gRPC server
    g_server = new GRpcServer();
    if (!g_server->start(socketAddr, maxWorkers)) {
        fprintf(stderr, "FATAL: failed to start gRPC server\n");
        return -1;
    }

    // Wire gRPC server to main window for notifications
    mainWin->setGrpcServer(g_server);

    // Wire UIController stats signal to MainWindow refresh
    TQObject::connect(controller, SIGNAL(statsUpdated(bool, int, int, int, const TQString&)),
                     mainWin, SLOT(onStatsUpdated(bool, int, int, int, const TQString&)));
    TQObject::connect(controller, SIGNAL(daemonSubscribed(const TQString&, bool)),
                     mainWin, SLOT(onDaemonSubscribed(const TQString&, bool)));
    TQObject::connect(controller, SIGNAL(notificationReply(const TQString&, unsigned long long, int, const TQString&)),
                     mainWin, SLOT(onNotificationReply(const TQString&, unsigned long long, int, const TQString&)));

    // Wire UIController signals to SysTray for icon updates
    TQObject::connect(controller, SIGNAL(daemonSubscribed(const TQString&, bool)),
                     tray, SLOT(onDaemonSubscribed(const TQString&, bool)));
    TQObject::connect(controller, SIGNAL(alertRequested()),
                     tray, SLOT(setIconAlert()));
    TQObject::connect(controller, SIGNAL(alertFinished()),
                     tray, SLOT(restoreIcon()));

    fprintf(stdout, "OpenSnitch UI running on %s\n", socketAddr.latin1());

    // Connect tray signals
    TQObject::connect(tray, SIGNAL(quitRequested()), mainWin, SLOT(onQuitRequested()));
    TQObject::connect(tray, SIGNAL(toggleMainWindow()), mainWin, SLOT(onTrayToggle()));
    TQObject::connect(tray, SIGNAL(showMainWindow()), mainWin, SLOT(show()));
    TQObject::connect(tray, SIGNAL(showMainWindow()), mainWin, SLOT(raise()));
    TQObject::connect(tray, SIGNAL(enableInterception(bool)), mainWin, SLOT(onInterceptionToggled()));

    TQObject::connect(mainWin, SIGNAL(iconsThemeChanged()), tray, SLOT(reloadIcons()));

    // Connect MainWindow interception state to SysTray icon
    TQObject::connect(mainWin, SIGNAL(interceptionToggled(bool)), tray, SLOT(setFirewallEnabled(bool)));

    int ret = app.exec();

    // Cleanup
    shutdown_runtime();

    return ret;
}
