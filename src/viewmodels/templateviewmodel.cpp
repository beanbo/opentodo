#include "templateviewmodel.h"

#include "model/task.h"
#include "model/template.h"
#include "storage/datastore.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

TemplateViewModel::TemplateViewModel(DataStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    load();
}

int TemplateViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_templates.size();
}

QVariant TemplateViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_templates.size())
        return {};
    const Template *t = m_templates.at(index.row());
    switch (role) {
    case IdRole:        return t->id();
    case NameRole:      return t->name();
    case ItemCountRole: return t->itemCount();
    default:            return {};
    }
}

QHash<int, QByteArray> TemplateViewModel::roleNames() const
{
    return {
        { IdRole,        "tplId" },
        { NameRole,      "name" },
        { ItemCountRole, "itemCount" },
    };
}

Template *TemplateViewModel::templateById(const QString &id) const
{
    for (Template *t : m_templates)
        if (t && t->id() == id)
            return t;
    return nullptr;
}

// ── Persistence (device-local templates.json) ───────────────────────────────

QString TemplateViewModel::storePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/templates.json");
}

void TemplateViewModel::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray())
        return;
    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject()) continue;
        Template *t = Template::fromJson(v.toObject(), this);
        if (t && !t->isDeleted())
            m_templates.append(t);
    }
}

void TemplateViewModel::save() const
{
    QJsonArray arr;
    for (const Template *t : m_templates)
        arr.append(t->toJson());
    const QString path = storePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.commit();
}

// ── Capture ──────────────────────────────────────────────────────────────────

QJsonObject TemplateViewModel::specFromTask(const QString &taskId) const
{
    QJsonObject spec;
    Task *t = m_store->taskById(taskId);
    if (!t)
        return spec;
    // Dates are intentionally stripped — a template is reusable scaffolding,
    // not a dated plan. Content / priority / labels / subtree are kept.
    spec[QStringLiteral("content")]  = t->content();
    spec[QStringLiteral("priority")] = t->priority();
    QJsonArray labels;
    for (const QString &l : t->labels())
        labels.append(l);
    if (!labels.isEmpty())
        spec[QStringLiteral("labels")] = labels;
    QJsonArray children;
    const auto subs = m_store->subtasks(taskId);
    for (Task *c : subs) {
        if (c && !c->isDeleted())
            children.append(specFromTask(c->id()));
    }
    if (!children.isEmpty())
        spec[QStringLiteral("children")] = children;
    return spec;
}

QString TemplateViewModel::saveAsTemplate(const QString &name, const QStringList &taskIds)
{
    QJsonArray items;
    for (const QString &id : taskIds) {
        Task *t = m_store->taskById(id);
        if (t && !t->isDeleted() && t->parentId().isEmpty())   // top-level only
            items.append(specFromTask(id));
    }
    // Q-audit #16: if the selection was made up entirely of subtasks (the
    // top-level filter above produced nothing), capture them as template roots
    // anyway — otherwise "Сохранить как шаблон" on a subtask was a silent no-op.
    // specFromTask() still captures each one's own subtree.
    if (items.isEmpty()) {
        for (const QString &id : taskIds) {
            Task *t = m_store->taskById(id);
            if (t && !t->isDeleted())
                items.append(specFromTask(id));
        }
    }
    if (items.isEmpty())
        return {};

    auto *tpl = new Template(this);
    tpl->setName(name.trimmed().isEmpty() ? tr("Шаблон") : name.trimmed());
    tpl->setItems(items);

    beginInsertRows(QModelIndex(), m_templates.size(), m_templates.size());
    m_templates.append(tpl);
    endInsertRows();
    save();
    emit countChanged();
    return tpl->id();
}

// ── Insert ───────────────────────────────────────────────────────────────────

void TemplateViewModel::materialise(const QJsonObject &spec, const QString &projectId,
                                    const QString &sectionId, const QString &parentId)
{
    Task *t = m_store->addTask(spec.value(QStringLiteral("content")).toString(), projectId);
    if (!t)
        return;
    t->setPriority(spec.value(QStringLiteral("priority")).toInt(4));
    QStringList labels;
    for (const QJsonValue &v : spec.value(QStringLiteral("labels")).toArray())
        labels.append(v.toString());
    if (!labels.isEmpty())
        t->setLabels(labels);
    if (!sectionId.isEmpty())
        t->setSectionId(sectionId);
    if (!parentId.isEmpty())
        t->setParentId(parentId);

    // Stamp per-field CRDT _meta for everything we set directly on the Task after
    // addTask(). addTask() only stamps content/project_id, so without this the
    // labels/section_id/parent_id/priority fields keep an empty _meta and the
    // Merger falls back to whole-entity LWW — a concurrent (even older) peer edit
    // would then silently revert them (data loss). Mirrors TaskViewModel::addTask
    // and duplicateProject, which stamp these explicitly for the same reason.
    QStringList stampedFields{QStringLiteral("priority")};
    if (!labels.isEmpty())    stampedFields << QStringLiteral("labels");
    if (!sectionId.isEmpty()) stampedFields << QStringLiteral("section_id");
    if (!parentId.isEmpty())  stampedFields << QStringLiteral("parent_id");
    m_store->markTaskFieldsModified(t->id(), stampedFields);

    // Children become subtasks of this freshly-created task.
    for (const QJsonValue &cv : spec.value(QStringLiteral("children")).toArray())
        materialise(cv.toObject(), projectId, sectionId, t->id());
}

void TemplateViewModel::insertTemplate(const QString &templateId,
                                       const QString &projectId,
                                       const QString &sectionId)
{
    Template *tpl = templateById(templateId);
    if (!tpl)
        return;
    const QJsonArray items = tpl->items();
    for (const QJsonValue &v : items)
        materialise(v.toObject(), projectId, sectionId, QString());
    emit templateInserted(templateId, items.size());
}

// ── Manage ───────────────────────────────────────────────────────────────────

bool TemplateViewModel::deleteTemplate(const QString &templateId)
{
    for (int i = 0; i < m_templates.size(); ++i) {
        if (m_templates.at(i)->id() == templateId) {
            beginRemoveRows(QModelIndex(), i, i);
            m_templates.takeAt(i)->deleteLater();
            endRemoveRows();
            save();
            emit countChanged();
            return true;
        }
    }
    return false;
}

bool TemplateViewModel::renameTemplate(const QString &templateId, const QString &newName)
{
    for (int i = 0; i < m_templates.size(); ++i) {
        if (m_templates.at(i)->id() == templateId) {
            m_templates.at(i)->setName(newName.trimmed());
            m_templates.at(i)->setUpdatedAt(QDateTime::currentDateTimeUtc());
            emit dataChanged(index(i), index(i), {NameRole});
            save();
            return true;
        }
    }
    return false;
}
