#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHash>
#include <QThread>
#include <QFile>
#include <QUrl>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// -------------------- FileWriterWorker (QThread) --------------------
class FileWriterWorker : public QObject {
    Q_OBJECT
public slots:
    void openFile(int row, QString path) {
        if (files.contains(row) && files[row]) {
            QFile* old = files[row];
            old->flush();
            old->close();
            delete old;
            files.remove(row);
        }

        QFile *f = new QFile(path);
        if (!f->open(QIODevice::WriteOnly)) {
            delete f;
            emit writeError(row, "Cannot open file for writing");
            return;
        }
        files[row] = f;
        emit fileOpened(row, path);
    }

    void appendChunk(int row, QByteArray chunk) {
        auto it = files.find(row);
        if (it == files.end() || !it.value()) return;

        QFile* f = it.value();
        if (f->write(chunk) < 0) {
            emit writeError(row, "Write failed");
        }
    }

    void closeFile(int row) {
        auto it = files.find(row);
        if (it == files.end() || !it.value()) return;

        QFile* f = it.value();
        f->flush();
        f->close();
        delete f;
        files.remove(row);
        emit fileClosed(row);
    }

signals:
    void fileOpened(int row, QString path);
    void fileClosed(int row);
    void writeError(int row, QString message);

private:
    QHash<int, QFile*> files; // row -> file handle (worker thread only)
};

// -------------------- HasherWorker (QThread) --------------------
class HasherWorker : public QObject {
    Q_OBJECT
public slots:
    void hashFile(int row, QString filePath);
signals:
    void hashReady(int row, QString digestHex);
    void hashError(int row, QString message);
};

// -------------------- MainWindow --------------------
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    // File writer requests
    void requestOpenFile(int row, QString path);
    void requestAppendChunk(int row, QByteArray chunk);
    void requestCloseFile(int row);

    // Hash requests
    void requestHash(int row, QString filePath);

private slots:
    void onChooseFolderClicked();
    void onAddClicked();
    void onStartAllClicked();

    void handleProgress(QNetworkReply* reply, qint64 received, qint64 total);
    void handleFinished(QNetworkReply* reply);

    // Webpage fetch → parse → enqueue files
    void onPageFetched();

    // Writer callbacks
    void onWriterError(int row, const QString& message);

    // Hasher callbacks
    void onHashReady(int row, const QString& digestHex);
    void onHashError(int row, const QString& message);

    void on_actioninfo_triggered();

private:
    enum Col { COL_URL=0, COL_FILE=1, COL_PROGRESS=2, COL_STATUS=3 };

    // --- helpers ---
    QString fileNameFromUrl(const QString& urlStr) const;
    void ensureRowCells(int row);
    void setProgress(int row, int percent);
    void setStatus(int row, const QString& status);

    bool urlExistsInTable(const QString& urlStr) const;
    void addUrlToTable(const QString& urlStr);

    void startDownloadForRow(int row);

    // --- webpage logic ---
    bool looksLikeWebPage(const QUrl& u) const;
    static QList<QUrl> extractLinksFromHtml(const QString& html, const QUrl& baseUrl);
    bool allowedByFilter(const QUrl& u, const QUrl& baseUrl) const;

private:
    Ui::MainWindow *ui;

    QString downloadDir;

    QNetworkAccessManager net;

    // reply -> row/path mapping (file downloads)
    QHash<QNetworkReply*, int> replyToRow;
    QHash<QNetworkReply*, QString> replyToPath;

    // QThread: file writer
    QThread writerThread;
    FileWriterWorker* writer = nullptr;

    // QThread: hasher
    QThread hashThread;
    HasherWorker* hasher = nullptr;

    // webpage fetch state
    QNetworkReply* pageReply = nullptr;
    QUrl pageBaseUrl;
};
