#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QList>
#include <QQmlEngine>
#include <QString>

class DataStore;
class Template;

// TemplateViewModel — the reusable task-template library.
//
// Templates are DEVICE-LOCAL: persisted to <appdata>/templates.json (they are a
// personal authoring convenience, not shared state, so they intentionally do
// NOT travel through the task sync). Exposes the list for a picker plus two
// operations:
//   saveAsTemplate(name, taskIds)  — capture the given top-level tasks (and
//                                    their subtasks) as a template. Dates are
//                                    stripped; content/priority/labels/tree kept.
//   insertTemplate(id, projectId, sectionId) — materialise the template's specs
//                                    into real Tasks under the target project.
class TemplateViewModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("TemplateViewModel is constructed in C++ and exposed via context property.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, ItemCountRole };

    explicit TemplateViewModel(DataStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_templates.size(); }

    // Capture `taskIds` (top-level tasks; subtasks pulled in recursively) as a
    // named template. Returns the new template id, or "" if nothing was saved.
    Q_INVOKABLE QString saveAsTemplate(const QString &name, const QStringList &taskIds);
    // Materialise a template into real tasks under projectId / optional sectionId.
    Q_INVOKABLE void insertTemplate(const QString &templateId,
                                    const QString &projectId,
                                    const QString &sectionId = QString());
    Q_INVOKABLE bool deleteTemplate(const QString &templateId);
    Q_INVOKABLE bool renameTemplate(const QString &templateId, const QString &newName);

signals:
    void countChanged();
    void templateInserted(const QString &templateId, int taskCount);

private:
    QString storePath() const;
    void load();
    void save() const;
    Template *templateById(const QString &id) const;
    QJsonObject specFromTask(const QString &taskId) const;
    void materialise(const QJsonObject &spec, const QString &projectId,
                     const QString &sectionId, const QString &parentId);

    DataStore *m_store;
    QList<Template *> m_templates;
};
