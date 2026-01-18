#ifndef FILEWRITERWORKER_H
#define FILEWRITERWORKER_H


#include <QObject>
#include <QHash>

class QFile;

class FileWriterWorker : public QObject {
    Q_OBJECT
public:
    explicit FileWriterWorker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void openFile(int row, QString path);
    void appendChunk(int row, QByteArray chunk);
    void closeFile(int row);

signals:
    void fileOpened(int row, QString path);
    void fileClosed(int row);
    void writeError(int row, QString message);

private:
    QHash<int, QFile*> files; // row -> file handle (worker thread only)
};

#endif
