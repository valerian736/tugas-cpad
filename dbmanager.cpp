#include "dbmanager.h"

#include <QSqlQuery>
#include <QVariant>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>

static QString defaultDbPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return QDir(base).filePath("scraper.db");
}

static QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

DBManager::DBManager()
{
    connName = QString("scraper_conn_%1").arg(reinterpret_cast<quintptr>(this));
}

DBManager::~DBManager()
{
    close();
}

bool DBManager::openDefault()
{
    return openAtPath(defaultDbPath());
}

bool DBManager::openAtPath(const QString& dbPath)
{
    if (db.isValid() && db.isOpen())
        return true;

    db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(dbPath);

    if (!db.open())
        return false;

    return ensureSchema();
}

void DBManager::close()
{
    if (db.isValid()) {
        if (db.isOpen()) db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connName);
    }
}

bool DBManager::ensureSchema()
{
    QSqlQuery q(db);

    q.exec("PRAGMA journal_mode=WAL;");
    q.exec("PRAGMA synchronous=NORMAL;");

    const char* sql =
        "CREATE TABLE IF NOT EXISTS downloads ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  url TEXT NOT NULL,"
        "  file_path TEXT NOT NULL,"
        "  file_name TEXT,"
        "  status TEXT NOT NULL DEFAULT 'Queued',"
        "  progress INTEGER NOT NULL DEFAULT 0,"
        "  sha256 TEXT,"
        "  created_at TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL,"
        "  UNIQUE(url, file_path)"
        ");";

    return q.exec(sql);
}

bool DBManager::addOrIgnoreQueued(const QString& url, const QString& filePath, const QString& fileName)
{
    QSqlQuery q(db);
    q.prepare(
        "INSERT OR IGNORE INTO downloads "
        "(url, file_path, file_name, status, progress, created_at, updated_at) "
        "VALUES (?, ?, ?, 'Queued', 0, ?, ?)"
        );
    q.addBindValue(url);
    q.addBindValue(filePath);
    q.addBindValue(fileName);
    q.addBindValue(nowIso());
    q.addBindValue(nowIso());
    return q.exec();
}

bool DBManager::updateProgress(const QString& url, const QString& filePath, int progress)
{
    QSqlQuery q(db);
    q.prepare("UPDATE downloads SET progress=?, updated_at=? WHERE url=? AND file_path=?");
    q.addBindValue(progress);
    q.addBindValue(nowIso());
    q.addBindValue(url);
    q.addBindValue(filePath);
    return q.exec();
}

bool DBManager::updateStatus(const QString& url, const QString& filePath, const QString& status)
{
    QSqlQuery q(db);
    q.prepare("UPDATE downloads SET status=?, updated_at=? WHERE url=? AND file_path=?");
    q.addBindValue(status);
    q.addBindValue(nowIso());
    q.addBindValue(url);
    q.addBindValue(filePath);
    return q.exec();
}

bool DBManager::setHashAndDone(const QString& url, const QString& filePath, const QString& sha256)
{
    QSqlQuery q(db);
    q.prepare(
        "UPDATE downloads "
        "SET sha256=?, status='Done', progress=100, updated_at=? "
        "WHERE url=? AND file_path=?"
        );
    q.addBindValue(sha256);
    q.addBindValue(nowIso());
    q.addBindValue(url);
    q.addBindValue(filePath);
    return q.exec();
}

QVector<DownloadRecord> DBManager::fetchRecent(int limit) const
{
    QVector<DownloadRecord> out;
    if (!db.isValid() || !db.isOpen())
        return out;

    QSqlQuery q(db);
    q.prepare(
        "SELECT url, file_path, file_name, status, progress, sha256, updated_at "
        "FROM downloads "
        "ORDER BY datetime(updated_at) DESC "
        "LIMIT ?"
        );
    q.addBindValue(limit);

    if (!q.exec())
        return out;

    while (q.next()) {
        DownloadRecord r;
        r.url = q.value(0).toString();
        r.filePath = q.value(1).toString();
        r.fileName = q.value(2).toString();
        r.status = q.value(3).toString();
        r.progress = q.value(4).toInt();
        r.sha256 = q.value(5).toString();
        r.updatedAt = q.value(6).toString();
        out.push_back(r);
    }

    return out;
}

bool DBManager::clearAll()
{
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    return q.exec("DELETE FROM downloads;");
}
