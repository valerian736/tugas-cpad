#include "hasher.h"

#include <QFile>
#include <QCryptographicHash>

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
