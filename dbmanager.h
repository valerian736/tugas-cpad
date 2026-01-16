#pragma once

#include <QString>
#include <QSqlDatabase>
#include <QVector>

struct DownloadRecord {
    QString url;
    QString filePath;
    QString fileName;
    QString status;
    int progress = 0;
    QString sha256;
    QString updatedAt;
};

class DBManager {
public:
    DBManager();
    ~DBManager();

    bool openDefault();               // opens AppDataLocation/scraper.db
    bool openAtPath(const QString& dbPath);
    void close();

    bool ensureSchema();

    // Core functions you will call from MainWindow:
    bool addOrIgnoreQueued(const QString& url, const QString& filePath, const QString& fileName);
    bool updateProgress(const QString& url, const QString& filePath, int progress);
    bool updateStatus(const QString& url, const QString& filePath, const QString& status);
    bool setHashAndDone(const QString& url, const QString& filePath, const QString& sha256);

    // History
    QVector<DownloadRecord> fetchRecent(int limit = 200) const;
    bool clearAll();

private:
    QString connName;
    QSqlDatabase db;
};
