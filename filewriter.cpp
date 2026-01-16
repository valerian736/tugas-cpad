#include "filewriter.h"

#include <QFile>

void FileWriterWorker::openFile(int row, QString path) {
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

void FileWriterWorker::appendChunk(int row, QByteArray chunk) {
    auto it = files.find(row);
    if (it == files.end() || !it.value()) return;

    QFile* f = it.value();
    if (f->write(chunk) < 0) {
        emit writeError(row, "Write failed");
    }
}

void FileWriterWorker::closeFile(int row) {
    auto it = files.find(row);
    if (it == files.end() || !it.value()) return;

    QFile* f = it.value();
    f->flush();
    f->close();
    delete f;
    files.remove(row);
    emit fileClosed(row);
}
