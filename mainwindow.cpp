#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QFileInfo>
#include <QHeaderView>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>


// -------------------- HasherWorker implementation --------------------
void HasherWorker::hashFile(int row, QString filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit hashError(row, "Cannot open file for hashing");
        return;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        hash.addData(f.read(1024 * 1024)); // 1MB chunks
    }
    const QString digest = hash.result().toHex();
    emit hashReady(row, digest);
}

// -------------------- MainWindow --------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // UI object names are: AddButton, chooseButton, startButton, lineEdit, label, tableWidget :contentReference[oaicite:1]{index=1}
    ui->tableWidget->setColumnCount(4);
    ui->tableWidget->setHorizontalHeaderLabels({"URL", "File", "Progress", "Status"});
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(COL_URL, QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(COL_FILE, QHeaderView::ResizeToContents);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(COL_PROGRESS, QHeaderView::ResizeToContents);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(COL_STATUS, QHeaderView::ResizeToContents);

    ui->startButton->setEnabled(false);

    connect(ui->chooseButton, &QPushButton::clicked, this, &MainWindow::onChooseFolderClicked);
    connect(ui->AddButton,    &QPushButton::clicked, this, &MainWindow::onAddClicked);
    connect(ui->startButton,  &QPushButton::clicked, this, &MainWindow::onStartAllClicked);

    // -------------------- Writer Thread setup --------------------
    writer = new FileWriterWorker();
    writer->moveToThread(&writerThread);

    connect(&writerThread, &QThread::finished, writer, &QObject::deleteLater);

    connect(this, &MainWindow::requestOpenFile,    writer, &FileWriterWorker::openFile,    Qt::QueuedConnection);
    connect(this, &MainWindow::requestAppendChunk, writer, &FileWriterWorker::appendChunk, Qt::QueuedConnection);
    connect(this, &MainWindow::requestCloseFile,   writer, &FileWriterWorker::closeFile,   Qt::QueuedConnection);

    connect(writer, &FileWriterWorker::writeError, this, &MainWindow::onWriterError, Qt::QueuedConnection);

    writerThread.start();

    // -------------------- Hasher Thread setup --------------------
    hasher = new HasherWorker();
    hasher->moveToThread(&hashThread);

    connect(&hashThread, &QThread::finished, hasher, &QObject::deleteLater);

    connect(this, &MainWindow::requestHash, hasher, &HasherWorker::hashFile, Qt::QueuedConnection);
    connect(hasher, &HasherWorker::hashReady, this, &MainWindow::onHashReady, Qt::QueuedConnection);
    connect(hasher, &HasherWorker::hashError, this, &MainWindow::onHashError, Qt::QueuedConnection);

    hashThread.start();
}

MainWindow::~MainWindow()
{
    if (pageReply) {
        pageReply->abort();
        pageReply->deleteLater();
        pageReply = nullptr;
    }

    writerThread.quit();
    writerThread.wait();

    hashThread.quit();
    hashThread.wait();

    delete ui;
}


QString MainWindow::fileNameFromUrl(const QString& urlStr) const
{
    QUrl url(urlStr);
    QString name = QFileInfo(url.path()).fileName();
    if (name.isEmpty()) name = "download.bin";
    return name;
}

void MainWindow::ensureRowCells(int row)
{
    auto ensure = [&](int col, const QString& textIfCreate) {
        if (!ui->tableWidget->item(row, col))
            ui->tableWidget->setItem(row, col, new QTableWidgetItem(textIfCreate));
    };

    ensure(COL_URL, "");
    ensure(COL_FILE, "");
    ensure(COL_PROGRESS, "0%");
    ensure(COL_STATUS, "Queued");
}

void MainWindow::setProgress(int row, int percent)
{
    ensureRowCells(row);
    ui->tableWidget->item(row, COL_PROGRESS)->setText(QString::number(percent) + "%");
}

void MainWindow::setStatus(int row, const QString& status)
{
    ensureRowCells(row);
    ui->tableWidget->item(row, COL_STATUS)->setText(status);
}

bool MainWindow::urlExistsInTable(const QString& urlStr) const
{
    for (int r = 0; r < ui->tableWidget->rowCount(); ++r) {
        auto *it = ui->tableWidget->item(r, COL_URL);
        if (it && it->text().trimmed() == urlStr.trimmed())
            return true;
    }
    return false;
}

void MainWindow::addUrlToTable(const QString& urlStr)
{
    if (urlStr.trimmed().isEmpty()) return;
    if (urlExistsInTable(urlStr)) return;

    const int row = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(row);

    ui->tableWidget->setItem(row, COL_URL, new QTableWidgetItem(urlStr));
    ui->tableWidget->setItem(row, COL_FILE, new QTableWidgetItem(fileNameFromUrl(urlStr)));
    ui->tableWidget->setItem(row, COL_PROGRESS, new QTableWidgetItem("0%"));
    ui->tableWidget->setItem(row, COL_STATUS, new QTableWidgetItem("Queued"));
}


void MainWindow::onChooseFolderClicked()
{
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QString dir = QFileDialog::getExistingDirectory(this, "Choose download folder", defaultDir);

    if (dir.isEmpty()) return;

    downloadDir = dir;
    ui->label->setText("folder: " + dir);
    ui->statusbar->showMessage("Folder selected: " + dir, 2500);

    ui->startButton->setEnabled(true);
}

bool MainWindow::looksLikeWebPage(const QUrl& u) const
{
    // Heuristic: if path ends with "/" or no extension, treat as webpage.
    // (also allow common ".html"/".htm")
    QString path = u.path();
    if (path.isEmpty() || path.endsWith('/')) return true;

    QString lower = path.toLower();
    if (lower.endsWith(".html") || lower.endsWith(".htm")) return true;

    QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty()) return true;

    return false;
}

static bool hasAllowedExtension(const QUrl& u)
{
    const QString path = u.path().toLower();

    // Add/remove types you want
    static const QStringList exts = {
        ".png",".jpg",".jpeg",".webp",".gif",".svg",".ico",
        ".pdf",".zip",".rar",".7z",
        ".bin",".hex",".txt",".csv",".json",".xml",
        ".mp4",".mp3",".wav"
    };

    for (const auto& e : exts)
        if (path.endsWith(e)) return true;

    return false;
}

QList<QUrl> MainWindow::extractLinksFromHtml(const QString& html, const QUrl& baseUrl)
{
    QList<QUrl> out;
    QSet<QString> seen;

    // Pull href/src values
    QRegularExpression re(
        R"((?:href|src)\s*=\s*["']([^"'#]+)["'])",
        QRegularExpression::CaseInsensitiveOption
        );

    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        const QString raw = m.captured(1).trimmed();
        if (raw.isEmpty()) continue;

        QUrl resolved = baseUrl.resolved(QUrl(raw));
        if (!resolved.isValid()) continue;

        if (resolved.scheme() != "http" && resolved.scheme() != "https") continue;

        const QString key = resolved.toString(QUrl::FullyDecoded);
        if (seen.contains(key)) continue;
        seen.insert(key);

        out.push_back(resolved);
    }
    return out;
}

bool MainWindow::allowedByFilter(const QUrl& u, const QUrl& baseUrl) const
{

    if (!u.host().isEmpty() && u.host() != baseUrl.host())
        return false;


    if (!hasAllowedExtension(u))
        return false;

    return true;
}

void MainWindow::onAddClicked()
{
    const QString input = ui->lineEdit->text().trimmed();
    if (input.isEmpty()) {
        ui->statusbar->showMessage("Paste a URL first.", 2000);
        return;
    }

    QUrl url(input);
    if (!url.isValid() || url.scheme().isEmpty()) {
        ui->statusbar->showMessage("Invalid URL. Include http/https.", 2500);
        return;
    }

    // If it's a webpage → fetch HTML and enqueue filtered links
    if (looksLikeWebPage(url)) {
        if (pageReply) {
            ui->statusbar->showMessage("Already fetching a page. Wait for it to finish.", 2500);
            return;
        }

        pageBaseUrl = url;
        ui->statusbar->showMessage("Fetching page HTML...", 2000);

        pageReply = net.get(QNetworkRequest(url));
        connect(pageReply, &QNetworkReply::finished, this, &MainWindow::onPageFetched);

        ui->lineEdit->clear();
        return;
    }

    // Otherwise treat as direct file URL
    addUrlToTable(url.toString());
    ui->lineEdit->clear();
}

void MainWindow::onPageFetched()
{
    if (!pageReply) return;

    QNetworkReply* r = pageReply;
    pageReply = nullptr;

    if (r->error() != QNetworkReply::NoError) {
        ui->statusbar->showMessage("Page fetch error: " + r->errorString(), 4000);
        r->deleteLater();
        return;
    }

    const QByteArray data = r->readAll();
    r->deleteLater();

    const QString html = QString::fromUtf8(data);

    QList<QUrl> links = extractLinksFromHtml(html, pageBaseUrl);

    // Apply filtering + limit
    const int MAX_FILES = 200;
    int added = 0;

    for (const QUrl& u : links) {
        if (added >= MAX_FILES) break;

        if (!allowedByFilter(u, pageBaseUrl))
            continue;

        const QString urlStr = u.toString();
        if (urlExistsInTable(urlStr))
            continue;

        addUrlToTable(urlStr);
        added++;
    }

    ui->statusbar->showMessage(
        QString("Page parsed. Added %1 file link(s) (filtered).").arg(added),
        4000
        );
}

void MainWindow::onStartAllClicked()
{
    if (downloadDir.isEmpty()) {
        ui->statusbar->showMessage("Choose a folder first.", 2500);
        return;
    }

    const int rows = ui->tableWidget->rowCount();
    if (rows == 0) {
        ui->statusbar->showMessage("Add at least one URL.", 2500);
        return;
    }

    for (int row = 0; row < rows; ++row) {
        ensureRowCells(row);
        const QString status = ui->tableWidget->item(row, COL_STATUS)->text();

        if (status == "Downloading" || status.startsWith("Done") || status.startsWith("Error"))
            continue;

        startDownloadForRow(row);
    }

    ui->statusbar->showMessage("Started downloads.", 2000);
}

// -------------------- Download logic (streaming to QThread writer) --------------------
void MainWindow::startDownloadForRow(int row)
{
    const QString urlStr = ui->tableWidget->item(row, COL_URL)->text().trimmed();
    QUrl url(urlStr);

    if (!url.isValid() || url.scheme().isEmpty()) {
        setStatus(row, "Error: invalid URL");
        return;
    }

    const QString fileName = fileNameFromUrl(urlStr);
    const QString fullPath = QDir(downloadDir).filePath(fileName);

    emit requestOpenFile(row, fullPath);

    QNetworkReply *reply = net.get(QNetworkRequest(url));
    replyToRow.insert(reply, row);
    replyToPath.insert(reply, fullPath);

    setStatus(row, "Downloading");
    setProgress(row, 0);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        const int row = replyToRow.value(reply, -1);
        if (row < 0) return;

        const QByteArray chunk = reply->readAll();
        if (!chunk.isEmpty())
            emit requestAppendChunk(row, chunk);
    });

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 rec, qint64 tot) { handleProgress(reply, rec, tot); });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleFinished(reply); });
}

void MainWindow::handleProgress(QNetworkReply* reply, qint64 received, qint64 total)
{
    const int row = replyToRow.value(reply, -1);
    if (row < 0) return;

    int percent = (total > 0) ? int((received * 100) / total) : 0;
    setProgress(row, percent);
}

void MainWindow::handleFinished(QNetworkReply* reply)
{
    const int row = replyToRow.value(reply, -1);
    const QString path = replyToPath.value(reply);

    replyToRow.remove(reply);
    replyToPath.remove(reply);

    if (row < 0) {
        reply->deleteLater();
        return;
    }

    // flush any remaining bytes
    const QByteArray lastChunk = reply->readAll();
    if (!lastChunk.isEmpty())
        emit requestAppendChunk(row, lastChunk);

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(row, "Error: " + reply->errorString());
        emit requestCloseFile(row);
        reply->deleteLater();
        return;
    }

    emit requestCloseFile(row);

    setProgress(row, 100);
    setStatus(row, "Downloaded (hashing...)");

    emit requestHash(row, path);

    reply->deleteLater();
}

// -------------------- Worker callbacks --------------------
void MainWindow::onWriterError(int row, const QString& message)
{
    setStatus(row, "Error: " + message);
}

void MainWindow::onHashReady(int row, const QString& digestHex)
{
    setStatus(row, "Done (SHA256: " + digestHex.left(12) + "...)");
}

void MainWindow::onHashError(int row, const QString& message)
{
    setStatus(row, "Done (hash error: " + message + ")");
}

void MainWindow::on_actioninfo_triggered()
{

}

