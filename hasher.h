#pragma once

#include <QObject>

class HasherWorker : public QObject {
    Q_OBJECT
public:
    explicit HasherWorker(QObject* parent=nullptr) : QObject(parent) {}

public slots:
    void hashFile(int row, QString filePath);

signals:
    void hashReady(int row, QString digestHex);
    void hashError(int row, QString message);
};
