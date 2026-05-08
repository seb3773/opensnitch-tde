#ifndef OPENSNITCH_DATABASE_H
#define OPENSNITCH_DATABASE_H

#include <ntqstring.h>
#include <ntqsqlquery.h>
#include <ntqsqlcursor.h>
#include <ntqsqldatabase.h>
#include <ntqstringlist.h>
#include <ntqvaluelist.h>
#include <ntqmap.h>
#include <pthread.h>

// Complexity: O(1) for single inserts, O(n) for batch
// Dependencies: TQSql, QSQLITE3 driver
// Alignment: none required
// Thread safety: mutex-protected

class Database {
public:
    static Database* instance();
    static void destroy();

    bool initialize(int dbtype, const TQString& dbfile, bool jrnl_wal);
    void close();

    TQSqlDatabase* sqlDatabase();

    // Generic operations
    bool exec(const TQString& sql);
    bool insert(const TQString& table, const TQString& columns, const TQValueList<TQVariant>& values,
                const TQString& actionOnConflict = TQString());
    bool insertBatch(const TQString& table, const TQString& colnames,
                     const TQValueList<TQVariant>& fields, const TQValueList<TQVariant>& values);
    bool update(const TQString& table, const TQString& setClause,
                const TQValueList<TQVariant>& values, const TQString& whereClause);
    bool deleteRows(const TQString& table, const TQString& whereClause,
                    const TQValueList<TQVariant>& values = TQValueList<TQVariant>());

    TQSqlQuery query(const TQString& sql, const TQValueList<TQVariant>& binds = TQValueList<TQVariant>());
    TQSqlCursor* select(const TQString& table, const TQString& fields = "*",
                        const TQString& where = TQString(),
                        const TQString& orderBy = TQString(),
                        int limit = 0);

    void vacuum();
    void optimize();

    bool isOpen() const;

    // Schema version
    static const int DB_VERSION = 5;

private:
    Database();
    ~Database();

    static Database* s_instance;

    TQSqlDatabase* m_db;
    TQString m_dbName;
    TQString m_dbFile;
    int m_dbType;
    pthread_mutex_t m_lock;

    bool createTables();
    bool upgradeSchema();
    void setSchemaVersion(int version);
    int schemaVersion();
    bool isDbOk();
};

#endif // OPENSNITCH_DATABASE_H
