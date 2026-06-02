#pragma once

#include <QObject>

class QLocalServer;
class QLocalSocket;
class DataStore;
class TaskViewModel;

// ─────────────────────────────────────────────────────────────────────────────
// CliBridge — lets the `opentodo` CLI talk to the *running* desktop app.
//
// It listens on a local socket ("OpenTodoCli"). Each connection sends one line
// of JSON ({cmd, ...}); we reply with one line of JSON ({ok, data|error}).
//
// Read commands (list/next/show) are answered straight from the live DataStore
// via clicore (identical JSON to the offline path). Write commands
// (done/add/comment) are routed through TaskViewModel so the real mutation path
// runs — karma, the 1-second autosave, and the Drive-sync trigger all fire just
// as if the user had acted in the UI. That is the whole point of the live path:
// no file race, and no behaviour skipped.
// ─────────────────────────────────────────────────────────────────────────────
class CliBridge : public QObject
{
    Q_OBJECT
public:
    CliBridge(DataStore *store, TaskViewModel *taskVM, QObject *parent = nullptr);

    bool isListening() const;

private slots:
    void onNewConnection();

private:
    QByteArray handleLine(const QByteArray &line) const;

    DataStore *m_store;
    TaskViewModel *m_taskVM;
    QLocalServer *m_server;
};
