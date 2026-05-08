#include "database.h"

#include <ntqsqlerror.h>
#include <ntqsqlquery.h>
#include <ntqsqlindex.h>
#include <ntqsqldriver.h>
#include <ntqvariant.h>
#include <ntqdatetime.h>
#include <stdio.h>
#include <unistd.h>

Database* Database::s_instance = 0;

Database* Database::instance()
{
    if (!s_instance)
        s_instance = new Database();
    return s_instance;
}

void Database::destroy()
{
    delete s_instance;
    s_instance = 0;
}

Database::Database()
    : m_db(0),
      m_dbName("opensnitch"),
      m_dbFile("file::memory:?cache=shared"),
      m_dbType(0)
{
    pthread_mutex_init(&m_lock, 0);
}

Database::~Database()
{
    close();
    pthread_mutex_destroy(&m_lock);
}

bool Database::initialize(int dbtype, const TQString& dbfile, bool jrnl_wal)
{
    m_dbType = dbtype;

    // Match Python UI behavior:
    // - dbtype == 0: always use shared in-memory SQLite URI, ignore dbfile.
    // - dbtype != 0: use configured dbfile.
    if (dbtype == 0)
        m_dbFile = "file::memory:?cache=shared";
    else
        m_dbFile = dbfile;

    bool isNewFile = (access(m_dbFile.local8Bit(), F_OK) != 0);

    m_db = TQSqlDatabase::addDatabase("TQSQLITE3", m_dbName);
    if (!m_db) {
        fprintf(stderr, "Failed to add QSQLITE3 database driver\n");
        return false;
    }

    m_db->setDatabaseName(m_dbFile);

    // URI mode enabled via sqlite3_config(SQLITE_CONFIG_URI, 1) in main()
    // Matches Python: QSQLITE_OPEN_URI;QSQLITE_ENABLE_SHARED_CACHE
    if (dbtype == 0)
        m_db->setConnectOptions("QSQLITE_OPEN_URI;QSQLITE_ENABLE_SHARED_CACHE");

    if (!m_db->open()) {
        fprintf(stderr, "Error opening DB: %s\n", m_dbFile.local8Bit().data());
        fprintf(stderr, "  driver error: %s\n", m_db->lastError().driverText().local8Bit().data());
        fprintf(stderr, "  db error: %s\n", m_db->lastError().databaseText().local8Bit().data());
        return false;
    }

    // File DB: optional WAL mode.
    // Notes: This affects only the UI local SQLite DB, not daemon behavior.
    if (dbtype != 0 && jrnl_wal) {
        exec("PRAGMA journal_mode=WAL");
        exec("PRAGMA synchronous=NORMAL");
    }

    // In-memory DB: try to cap SQLite internal memory usage.
    // These settings do not change displayed data, only internal cache/index behavior.
    if (dbtype == 0) {
        exec("PRAGMA temp_store=MEMORY");
        exec("PRAGMA cache_size=-8192");
        exec("PRAGMA mmap_size=0");
        exec("PRAGMA journal_mode=OFF");
        exec("PRAGMA synchronous=OFF");
    }

    if (!isDbOk())
        return false;

    if (isNewFile || dbtype == 0)
        setSchemaVersion(DB_VERSION);

    createTables();
    upgradeSchema();
    return true;
}

void Database::close()
{
    pthread_mutex_lock(&m_lock);
    if (m_db && m_db->isOpen())
        m_db->close();
    m_db = 0;
    pthread_mutex_unlock(&m_lock);
}

TQSqlDatabase* Database::sqlDatabase()
{
    return m_db;
}

bool Database::exec(const TQString& sql)
{
    if (!m_db) return false;
    pthread_mutex_lock(&m_lock);
    TQSqlQuery q(m_db);
    bool ok = q.exec(sql);
    if (!ok)
        fprintf(stderr, "DB exec error: %s -> %s\n", sql.local8Bit().data(),
                q.lastError().text().local8Bit().data());
    pthread_mutex_unlock(&m_lock);
    return ok;
}

bool Database::insert(const TQString& table, const TQString& columns,
                      const TQValueList<TQVariant>& values, const TQString& actionOnConflict)
{
    if (!m_db) return false;
    TQString sql;
    if (!actionOnConflict.isEmpty())
        sql = TQString("INSERT OR %1 INTO %2 %3 VALUES (").arg(actionOnConflict).arg(table).arg(columns);
    else
        sql = TQString("INSERT INTO %1 %2 VALUES (").arg(table).arg(columns);

    TQStringList ph;
    for (unsigned i = 0; i < values.count(); i++)
        ph << "?";
    sql += ph.join(",") + ")";

    pthread_mutex_lock(&m_lock);
    TQSqlQuery q(m_db);
    q.prepare(sql);
    for (unsigned i = 0; i < values.count(); i++)
        q.bindValue(i, values[i]);
    bool ok = q.exec();
    if (!ok)
        fprintf(stderr, "DB insert error: %s -> %s\n", sql.local8Bit().data(),
                q.lastError().text().local8Bit().data());
    pthread_mutex_unlock(&m_lock);
    return ok;
}

bool Database::insertBatch(const TQString& table, const TQString& colnames,
                           const TQValueList<TQVariant>& fields, const TQValueList<TQVariant>& values)
{
    if (!m_db) return false;
    TQString sql = TQString("INSERT OR REPLACE INTO %1 %2 VALUES (?, ?)").arg(table).arg(colnames);

    pthread_mutex_lock(&m_lock);
    TQSqlQuery q(m_db);
    q.prepare(sql);

    unsigned count = TQMIN(fields.count(), values.count());
    for (unsigned i = 0; i < count; i++) {
        q.bindValue(0, fields[i]);
        q.bindValue(1, values[i]);
        if (!q.exec()) {
            fprintf(stderr, "DB insertBatch error: %s\n", q.lastError().text().local8Bit().data());
            pthread_mutex_unlock(&m_lock);
            return false;
        }
    }
    pthread_mutex_unlock(&m_lock);
    return true;
}

bool Database::update(const TQString& table, const TQString& setClause,
                      const TQValueList<TQVariant>& values, const TQString& whereClause)
{
    if (!m_db) return false;
    TQString sql = TQString("UPDATE %1 SET %2 WHERE %3").arg(table).arg(setClause).arg(whereClause);

    pthread_mutex_lock(&m_lock);
    TQSqlQuery q(m_db);
    q.prepare(sql);
    for (unsigned i = 0; i < values.count(); i++)
        q.bindValue(i, values[i]);
    bool ok = q.exec();
    if (!ok)
        fprintf(stderr, "DB update error: %s -> %s\n", sql.local8Bit().data(),
                q.lastError().text().local8Bit().data());
    pthread_mutex_unlock(&m_lock);
    return ok;
}

bool Database::deleteRows(const TQString& table, const TQString& whereClause,
                          const TQValueList<TQVariant>& values)
{
    if (!m_db) return false;
    TQString sql = TQString("DELETE FROM %1 WHERE %2").arg(table).arg(whereClause);

    pthread_mutex_lock(&m_lock);
    TQSqlQuery q(m_db);
    q.prepare(sql);
    for (unsigned i = 0; i < values.count(); i++)
        q.bindValue(i, values[i]);
    bool ok = q.exec();
    if (!ok)
        fprintf(stderr, "DB delete error: %s -> %s\n", sql.local8Bit().data(),
                q.lastError().text().local8Bit().data());
    pthread_mutex_unlock(&m_lock);
    return ok;
}

TQSqlQuery Database::query(const TQString& sql, const TQValueList<TQVariant>& binds)
{
    if (!m_db) return TQSqlQuery();
    pthread_mutex_lock(&m_lock);
    TQSqlQuery q(m_db);
    q.prepare(sql);
    for (unsigned i = 0; i < binds.count(); i++)
        q.bindValue(i, binds[i]);
    q.exec();
    pthread_mutex_unlock(&m_lock);
    return q;
}

TQSqlCursor* Database::select(const TQString& table, const TQString& /*fields*/,
                               const TQString& where, const TQString& orderBy, int /*limit*/)
{
    if (!m_db) return 0;
    pthread_mutex_lock(&m_lock);
    TQSqlCursor* cur = new TQSqlCursor(table, true, m_db);
    cur->setMode(TQSqlCursor::ReadOnly);
    // TQt3: select(filter, sort) where filter is SQL WHERE clause, sort is TQSqlIndex

    TQSqlIndex sort;
    if (!orderBy.isEmpty()) {
        TQStringList parts = TQStringList::split(",", orderBy);
        for (TQStringList::ConstIterator it = parts.begin(); it != parts.end(); ++it) {
            TQString term = (*it).stripWhiteSpace();
            if (term.isEmpty())
                continue;

            bool desc = false;
            int sp = term.find(' ');
            TQString fieldName = term;
            if (sp >= 0) {
                fieldName = term.left(sp).stripWhiteSpace();
                TQString dir = term.mid(sp + 1).stripWhiteSpace().lower();
                if (dir == "desc")
                    desc = true;
            }

            if (!fieldName.isEmpty()) {
                const TQSqlField* f = cur->field(fieldName);
                if (f)
                    sort.append(*f, desc);
            }
        }
    }

    if (!sort.isEmpty()) {
        if (!where.isEmpty())
            cur->select(where, sort);
        else
            cur->select(TQString(), sort);
    } else {
        if (!where.isEmpty())
            cur->select(where);
        else
            cur->select();
    }
    pthread_mutex_unlock(&m_lock);
    return cur;
}

void Database::vacuum()
{
    exec("VACUUM");
}

void Database::optimize()
{
    exec("PRAGMA optimization_cost=0");
}

bool Database::isOpen() const
{
    return m_db && m_db->isOpen();
}

void Database::setSchemaVersion(int version)
{
    if (!m_db) return;
    TQSqlQuery q(m_db);
    q.exec(TQString("PRAGMA user_version = %1").arg(version));
}

int Database::schemaVersion()
{
    if (!m_db) return 0;
    TQSqlQuery q(m_db);
    q.exec("PRAGMA user_version");
    if (q.next())
        return q.value(0).toInt();
    return 0;
}

bool Database::isDbOk()
{
    if (!m_db) return false;
    TQSqlQuery q(m_db);
    if (!q.exec("SELECT 1")) {
        fprintf(stderr, "DB integrity check failed: %s\n", q.lastError().text().local8Bit().data());
        return false;
    }
    return true;
}

bool Database::createTables()
{
    exec("CREATE TABLE IF NOT EXISTS connections ("
         "time TEXT, node TEXT, action TEXT, protocol TEXT, "
         "src_ip TEXT, src_port TEXT, dst_ip TEXT, dst_host TEXT, dst_port TEXT, "
         "uid TEXT, pid TEXT, process TEXT, process_args TEXT, process_cwd TEXT, rule TEXT, "
         "UNIQUE(node, action, protocol, src_ip, src_port, dst_ip, dst_port, uid, pid, process, process_args))");

    exec("CREATE TABLE IF NOT EXISTS nodes ("
         "addr TEXT PRIMARY KEY, status TEXT, hostname TEXT, "
         "daemon_version TEXT, daemon_uptime TEXT, daemon_rules INTEGER, "
         "cons INTEGER, cons_dropped INTEGER, version TEXT, last_connection TEXT)");

    exec("CREATE TABLE IF NOT EXISTS rules ("
         "time TEXT, "
         "node TEXT, "
         "name TEXT, "
         "enabled TEXT, "
         "precedence TEXT, "
         "action TEXT, "
         "duration TEXT, "
         "operator_type TEXT, "
         "operator_sensitive TEXT, "
         "operator_operand TEXT, "
         "operator_data TEXT, "
         "UNIQUE(node, name)"
         ")");

    exec("CREATE TABLE IF NOT EXISTS hosts (what TEXT PRIMARY KEY, hits INTEGER)");
    exec("CREATE TABLE IF NOT EXISTS procs (what TEXT PRIMARY KEY, hits INTEGER)");
    exec("CREATE TABLE IF NOT EXISTS addrs (what TEXT PRIMARY KEY, hits INTEGER)");
    exec("CREATE TABLE IF NOT EXISTS ports (what TEXT PRIMARY KEY, hits INTEGER)");
    exec("CREATE TABLE IF NOT EXISTS users (what TEXT PRIMARY KEY, hits INTEGER)");

    exec("CREATE TABLE IF NOT EXISTS alerts ("
         "time TEXT, node TEXT, type TEXT, action TEXT, priority TEXT, "
         "what TEXT, body TEXT, status INTEGER)");

    // Indexes
    // File DB: keep full index set.
    // In-memory DB: reduce indexes to limit RAM usage (does not affect stored columns).
    exec("CREATE INDEX IF NOT EXISTS time_index ON connections (time)");
    exec("CREATE INDEX IF NOT EXISTS rules_index ON rules (time)");
    if (m_dbType != 0) {
        exec("CREATE INDEX IF NOT EXISTS action_index ON connections (action)");
        exec("CREATE INDEX IF NOT EXISTS protocol_index ON connections (protocol)");
        exec("CREATE INDEX IF NOT EXISTS dst_host_index ON connections (dst_host)");
        exec("CREATE INDEX IF NOT EXISTS process_index ON connections (process)");
        exec("CREATE INDEX IF NOT EXISTS dst_ip_index ON connections (dst_ip)");
        exec("CREATE INDEX IF NOT EXISTS dst_port_index ON connections (dst_port)");
        exec("CREATE INDEX IF NOT EXISTS rule_index ON connections (rule)");
        exec("CREATE INDEX IF NOT EXISTS node_index ON connections (node)");
        exec("CREATE INDEX IF NOT EXISTS details_query_index ON connections "
             "(process, process_args, uid, pid, dst_ip, dst_host, dst_port, action, node, protocol)");
    } else {
        exec("CREATE INDEX IF NOT EXISTS action_index ON connections (action)");
        exec("CREATE INDEX IF NOT EXISTS protocol_index ON connections (protocol)");
        exec("CREATE INDEX IF NOT EXISTS process_index ON connections (process)");
        exec("CREATE INDEX IF NOT EXISTS dst_ip_index ON connections (dst_ip)");
        exec("CREATE INDEX IF NOT EXISTS dst_port_index ON connections (dst_port)");
    }

    return true;
}

bool Database::upgradeSchema()
{
    int ver = schemaVersion();
    if (ver >= DB_VERSION)
        return true;

    // Migration from schema < 4: connections table lacked UNIQUE constraint,
    // causing duplicate rows. Recreate with constraint + indexes.
    if (ver < 4) {
        exec("DROP TABLE IF EXISTS connections");
        exec("DROP INDEX IF EXISTS time_index");
        exec("DROP INDEX IF EXISTS action_index");
        exec("DROP INDEX IF EXISTS protocol_index");
        exec("DROP INDEX IF EXISTS dst_host_index");
        exec("DROP INDEX IF EXISTS process_index");
        exec("DROP INDEX IF EXISTS dst_ip_index");
        exec("DROP INDEX IF EXISTS dst_port_index");
        exec("DROP INDEX IF EXISTS rule_index");
        exec("DROP INDEX IF EXISTS node_index");
        exec("DROP INDEX IF EXISTS details_query_index");
        createTables();
    }

    setSchemaVersion(DB_VERSION);
    return true;
}
