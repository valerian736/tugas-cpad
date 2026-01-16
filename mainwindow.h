#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHash>
#include <QThread>
#include <QUrl>

#include "dbmanager.h"
#include "filewriter.h"
#include "hasher.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void requestOpenFile(int row, QString path);
    void requestAppendChunk(int row, QByteArray chunk);
    void requestCloseFile(int row);

    void requestHash(int row, QString filePath);

private slots:
    void onChooseFolderClicked();
    void onAddClicked();
    void onStartAllClicked();

    void handleProgress(QNetworkReply* reply, qint64 received, qint64 total);
    void handleFinished(QNetworkReply* reply);

    void onPageFetched();

    void onWriterError(int row, const QString& message);

    void onHashReady(int row, const QString& digestHex);
    void onHashError(int row, const QString& message);

    // Tabs / history
    void onTabChanged(int index);
    void loadHistoryTable();

    void on_actioninfo_triggered();

    void on_tableWidget_2_cellDoubleClicked(int row, int column);

private:
    enum Col { COL_URL=0, COL_FILE=1, COL_PROGRESS=2, COL_STATUS=3 };

    QString fileNameFromUrl(const QString& urlStr) const;
    void ensureRowCells(int row);
    void setProgress(int row, int percent);
    void setStatus(int row, const QString& status);

    bool urlExistsInTable(const QString& urlStr) const;
    void addUrlToTable(const QString& urlStr);

    void startDownloadForRow(int row);

    bool looksLikeWebPage(const QUrl& u) const;
    static QList<QUrl> extractLinksFromHtml(const QString& html, const QUrl& baseUrl);
    bool allowedByFilter(const QUrl& u, const QUrl& baseUrl) const;

private:
    Ui::MainWindow *ui;

    QString downloadDir;

    // DB + stable mapping for DB updates
    DBManager db;
    QHash<int, QString> rowToUrl;
    QHash<int, QString> rowToPath;

    QNetworkAccessManager net;

    QHash<QNetworkReply*, int> replyToRow;
    QHash<QNetworkReply*, QString> replyToPath;

    QThread writerThread;
    FileWriterWorker* writer = nullptr;

    QThread hashThread;
    HasherWorker* hasher = nullptr;

    QNetworkReply* pageReply = nullptr;
    QUrl pageBaseUrl;
};
