#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlEngine>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/filter.h"
#include "model/reminder.h"
#include "model/comment.h"


/**
 * @brief In-memory source of truth for all application data.
 *
 * DataStore holds parallel QList (for ordered iteration) + QMap index
 * (for O(1) id lookup) per entity type.  All mutations:
 *   - set the entity's `updated_at` timestamp,
 *   - emit the type-specific signal (`tasksChanged`, `projectsChanged`, …),
 *   - emit the aggregate `dataChanged()` signal so FileManager can autosave.
 *
 * Deletions are always soft (set `is_deleted = true`; tombstones travel
 * through sync and all query methods filter them out).
 *
 * Access from QML uses the context-property name set in main.cpp
 * (`dataStore`), not the QML_SINGLETON import — do not try to access it
 * via the module import.
 *
 * @invariant Every entity stored here has a non-empty UUID id.
 * @invariant m_taskIndex.size() == m_tasks.size() (index kept in sync).
 */
class DataStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int taskCount READ taskCount NOTIFY tasksChanged)
    Q_PROPERTY(int projectCount READ projectCount NOTIFY projectsChanged)
    Q_PROPERTY(QJsonObject settings READ settings WRITE setSettings NOTIFY settingsChanged)

public:
    // Bump this when adding/renaming/restructuring JSON fields.
    // Migrations live in DataStore::migrateJson and run forward-only.
    //
    // v2: per-entity CRDT meta blob is now stored as a single-wrap
    //     entity["_meta"][field] = {ts, dev}. Earlier writers produced a
    //     double-wrap entity["_meta"]["_meta"][field] = {ts, dev} due to a
    //     bug in stampEntityField — migrateJson detects and flattens that.
    static constexpr int CURRENT_SCHEMA_VERSION = 2;

    // Returns the schema version stored in state.json that was loaded,
    // or 0 if no version was present (i.e. legacy file from before
    // versioning was added).
    int loadedSchemaVersion() const { return m_loadedSchemaVersion; }

    explicit DataStore(QObject *parent = nullptr);

    // --- Tasks ---
    Q_INVOKABLE Task *addTask(const QString &content, const QString &projectId);
    Q_INVOKABLE Task *taskById(const QString &id) const;
    Q_INVOKABLE bool removeTask(const QString &id);
    // Restore a soft-deleted task. Mirrors the cascade in removeTask:
    // any reminder/comment that was tombstoned by the same removeTask call
    // (identified by sharing the task's tombstone updatedAt and matching
    // taskId) is also restored. Returns true on success, false if the task
    // does not exist. Comments/reminders that were independently tombstoned
    // (different updatedAt) are intentionally left as tombstones.
    Q_INVOKABLE bool restoreTask(const QString &id);
    // "Delete permanently" (Trash view). Does NOT erase anything: an erased
    // entity would be resurrected by the next sync's union merge. The task
    // stays a tombstone (tombstoned first if it was live, no subtree cascade)
    // flagged is_purged. To callers it is gone — taskById() returns nullptr,
    // restoreTask() refuses it, the Trash (allTasksIncludingDeleted()) skips
    // it — while toJson() keeps it so the deletion keeps propagating;
    // vacuum() removes it after the tombstone safety window. Live reminders /
    // comments of the task are tombstoned. Returns false for an unknown or
    // already-purged id.
    Q_INVOKABLE bool purgeTask(const QString &id);
    Q_INVOKABLE QList<Task *> allTasks() const;
    // JS-friendly counterpart of allTasks() for QML. Returns QVariantList of
    // QVariantMap (camelCase keys mirroring Task's Q_PROPERTY names) because a
    // Q_INVOKABLE returning QList<Task*> does NOT convert to a usable JS array
    // in Qt 6.11 (`.length` is undefined, Array.isArray() is false), which
    // silently breaks every QML `for (i < x.length)` aggregation over it.
    Q_INVOKABLE QVariantList allTasksData() const;
    // JS-friendly counterparts of tasksForToday()/overdueTasks() — same shape
    // and rationale as allTasksData(). Used by the productivity daily briefing.
    Q_INVOKABLE QVariantList tasksForTodayData() const;
    Q_INVOKABLE QVariantList overdueTasksData() const;
    Q_INVOKABLE QVariantList tasksForProjectData(const QString &projectId) const;
    Q_INVOKABLE QVariantList searchTasksData(const QString &query,
                                             bool accentSensitive = false) const;
    // JS-friendly project list (see allTasksData rationale; QList<Project*>
    // likewise doesn't convert to a JS array in QML).
    Q_INVOKABLE QVariantList allProjectsData() const;
    // Returns all tasks including soft-deleted ones that are still in the
    // Trash (used by the Trash view). Purged tombstones (purgeTask) are left
    // out — they exist only so the deletion keeps winning sync merges.
    QList<Task *> allTasksIncludingDeleted() const;
    Q_INVOKABLE QList<Task *> tasksForProject(const QString &projectId) const;
    Q_INVOKABLE QList<Task *> tasksForSection(const QString &sectionId) const;
    Q_INVOKABLE QList<Task *> tasksForToday() const;
    Q_INVOKABLE QList<Task *> overdueTasks() const;
    Q_INVOKABLE QList<Task *> completedTasks() const;
    Q_INVOKABLE QList<Task *> subtasks(const QString &parentId) const;
    // Q2.26-B: substring search with Unicode normalization (NFKC + optional
    // Mn-strip + case fold) via TextSearch::contains. Default behaviour is
    // accent-INSENSITIVE so "café" matches "café" and "cafe". Pass
    // accentSensitive=true to preserve ё ≠ е / é ≠ e.
    Q_INVOKABLE QList<Task *> searchTasks(const QString &query,
                                          bool accentSensitive = false) const;
    int taskCount() const;
    void addExistingTask(Task *task);

    // --- Projects ---
    Q_INVOKABLE Project *addProject(const QString &name);
    Q_INVOKABLE Project *projectById(const QString &id) const;
    Q_INVOKABLE bool removeProject(const QString &id);
    Q_INVOKABLE QList<Project *> allProjects() const;
    Q_INVOKABLE Project *inboxProject() const;
    int projectCount() const;
    void addExistingProject(Project *project);

    // --- Sections ---
    Q_INVOKABLE Section *addSection(const QString &name, const QString &projectId);
    Q_INVOKABLE Section *sectionById(const QString &id) const;
    Q_INVOKABLE bool removeSection(const QString &id);
    Q_INVOKABLE QList<Section *> sectionsForProject(const QString &projectId) const;
    // night iter-160 (#216): HIDDEN (archived, non-deleted) sections of a project,
    // sorted by order. Counterpart to sectionsForProject (which excludes archived).
    // Used by the "показать скрытые разделы" reveal in Project/Board views.
    Q_INVOKABLE QList<Section *> archivedSectionsForProject(const QString &projectId) const;
    Q_INVOKABLE bool renameSection(const QString &id, const QString &newName);
    // Toggle is_collapsed / is_archived with a per-field CRDT stamp. Calling
    // s->setIsCollapsed() directly from QML and then markSectionModified() only
    // stamps updated_at; both peers end up with identical _meta.is_collapsed
    // (from addSection) and the merge falls back to deviceId tie-break, which
    // can silently revert the toggle after sync. Routing the mutation through
    // these methods ensures the per-field stamp advances on every flip.
    Q_INVOKABLE bool setSectionCollapsed(const QString &id, bool collapsed);
    Q_INVOKABLE bool setSectionArchived(const QString &id, bool archived);
    // QML reorder helper: writes the new order value AND stamps the order
    // field. Calling section.order = N from QML by itself only updates the
    // model — _meta.order keeps its addSection-time stamp, so the merger
    // can't tell the swap apart from a concurrent edit on another device.
    Q_INVOKABLE bool setSectionOrder(const QString &id, int order);
    void addExistingSection(Section *section);

    // Tell listeners that some data changed (autosave + UI).
    void markDataChanged() {
        emit sectionsChanged();
        emit dataChanged();
    }

    // --- Labels ---
    Q_INVOKABLE Label *addLabel(const QString &name);
    Q_INVOKABLE Label *labelById(const QString &id) const;
    Q_INVOKABLE Label *labelByName(const QString &name) const;
    Q_INVOKABLE bool removeLabel(const QString &id);
    // Iter-night-40: rename helper that keeps m_labelNameIndex consistent
    // AND cascades the new name across every task that referenced the old
    // name (Task::labels stores names — see task.cpp:84). Bare
    // label->setName() leaves the index pointing at the old key and tasks
    // still showing the old chip text until the next loadFromJson.
    Q_INVOKABLE bool renameLabel(const QString &id, const QString &newName);
    Q_INVOKABLE QList<Label *> allLabels() const;
    // JS-friendly label list (see allTasksData rationale; a QList<Label*> does
    // not convert to a JS array in QML, so allLabels().length is `undefined` —
    // visibility checks like `allLabels().length === 0` silently evaluate false).
    Q_INVOKABLE QVariantList allLabelsData() const;
    void addExistingLabel(Label *label);

    // --- Filters ---
    Q_INVOKABLE Filter *addFilter(const QString &name, const QString &query);
    Q_INVOKABLE Filter *filterById(const QString &id) const;
    Q_INVOKABLE bool removeFilter(const QString &id);
    Q_INVOKABLE QList<Filter *> allFilters() const;
    void addExistingFilter(Filter *filter);

    // --- Reminders ---
    Q_INVOKABLE Reminder *addReminder(const QString &taskId, const QString &type);
    Q_INVOKABLE Reminder *reminderById(const QString &id) const;
    Q_INVOKABLE bool removeReminder(const QString &id);
    Q_INVOKABLE QList<Reminder *> remindersForTask(const QString &taskId) const;

    // Maps the id a notification was scheduled under back to its live task.
    // Notification ids are not always task ids: ReminderScheduler keys OS
    // notifications by REMINDER id (so each reminder can be cancelled on its
    // own), snoozes use "snooze-<taskId>" and the mobile editor
    // "reminder-<taskId>". Returns "" when nothing live matches.
    Q_INVOKABLE QString taskIdForNotification(const QString &notificationId) const;
    // Returns ALL reminders, including soft-deleted ones. Used by the
    // reminder scheduler to cascade-cancel platform notifications for
    // reminders that were tombstoned by removeTask() — by the time the
    // scheduler runs, remindersForTask() filters tombstones out, so the
    // armed QTimer would otherwise fire for a task that no longer exists.
    QList<Reminder *> allRemindersIncludingDeleted() const;
    void addExistingReminder(Reminder *reminder);

    // --- Comments ---
    Q_INVOKABLE Comment *addComment(const QString &content, const QString &taskId, const QString &projectId, const QString &author = QString());
    Comment *commentById(const QString &id) const;
    Q_INVOKABLE bool removeComment(const QString &id);
    Q_INVOKABLE bool updateComment(const QString &id, const QString &content);
    Q_INVOKABLE QList<Comment *> commentsForTask(const QString &taskId) const;
    QList<Comment *> commentsForProject(const QString &projectId) const;
    void addExistingComment(Comment *comment);

    // --- Collaborators ---
    // First-class author identities for comments (and future attribution).
    // A comment's `author` field holds the collaborator id; an EMPTY author id
    // means the built-in "me" (all comments predate this field), so no data
    // migration is required. Profiles (display name / avatar colour / icon) are
    // persisted inside the synced `settings` blob under the "collaborators" key
    // — { "<id>": {name, color, icon}, ... } — riding the existing settings sync
    // (per-key LWW on settings_meta) with no new entity plumbing. Two built-ins always
    // resolve: "me" (the user) and "claude" (the OpenTodo→Claude bridge /
    // in-app assistant). Passing an unknown id resolves to sensible defaults so
    // extra collaborators can be introduced later without code changes.
    //
    // Resolvers apply stored overrides on top of the built-in defaults; an
    // empty colour/icon return means "no override — let the theme decide".
    Q_INVOKABLE QString collaboratorName(const QString &id) const;
    Q_INVOKABLE QString collaboratorColor(const QString &id) const;   // "" ⇒ theme default
    Q_INVOKABLE QString collaboratorIcon(const QString &id) const;    // "" ⇒ show initial
    Q_INVOKABLE bool    collaboratorIsAssistant(const QString &id) const;
    // Editable identities for a settings UI: [{id,name,color,icon,assistant}].
    Q_INVOKABLE QVariantList collaborators() const;
    // Persist an override. Empty name/color/icon clears that field back to the
    // built-in default. Emits collaboratorsChanged + settingsChanged + dataChanged.
    Q_INVOKABLE void setCollaboratorProfile(const QString &id, const QString &name,
                                            const QString &color, const QString &icon);

    // --- Settings / Karma ---
    QJsonObject settings() const;
    void setSettings(const QJsonObject &settings);
    Q_INVOKABLE QVariant setting(const QString &key) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

    // Export current settings to a JSON file.
    // Writes to <dir>/opentodo-settings-<timestamp>.json (Documents if dir is empty).
    // Returns the absolute path on success, or an empty string on failure.
    Q_INVOKABLE QString exportSettings(const QString &dir = QString());

    // Import settings from a JSON file exported by exportSettings().
    // Validates the "format" sentinel before applying.
    // Returns true on success, false on parse error or unrecognised format.
    Q_INVOKABLE bool importSettings(const QString &filePath);

    // --- Notifications ---
    // Called by ViewModels after they've mutated a model object directly
    // (e.g. Task::setIsCompleted) to notify subscribers (autosave, list views,
    // reminder scheduler) without making them break encapsulation by emitting
    // DataStore's own signals.
    void markTaskModified(const QString &id);
    void markProjectModified(const QString &id);
    void markLabelModified(const QString &id);
    // Stamps CRDT updated_at for a section mutated directly from QML (e.g.
    // order swap or isArchived toggle) and emits sectionsChanged + dataChanged.
    Q_INVOKABLE void markSectionModified(const QString &id);

    // Stamps CRDT per-field _meta for the listed field names AND updated_at,
    // emits the entity-type signal + dataChanged.
    //
    // Use these from ViewModels after directly mutating one or more fields on
    // a Task/Project/Label. The plain markXxxModified above only stamps
    // updated_at — sufficient for tombstones-only changes, but NOT for actual
    // field edits, because the per-field _meta stays at its addXxx-time stamp.
    // After sync, both peers end up with identical lFm/rFm for the field and
    // the Merger's whole-entity LWW fallback silently reverts one side's edit.
    // See setSectionCollapsed/Archived/Order for the section equivalent.
    //
    // `fields` must contain the JSON field names (snake_case — `is_completed`,
    // `project_id`, etc.), NOT the C++ property names.
    void markTaskFieldsModified(const QString &id, const QStringList &fields);
    void markProjectFieldsModified(const QString &id, const QStringList &fields);
    void markLabelFieldsModified(const QString &id, const QStringList &fields);
    // Iter-night-12: filters were the only first-class CRDT-tracked entity
    // missing a per-field stamping API on the OUTBOUND path. addFilter
    // stamped initial fields and applyCommit filter_update applied inbound
    // _meta, but local edits via FilterViewModel::updateFilter went through
    // bare setters with no stamp — so concurrent renames/recolors of the
    // same filter on two devices silently diverged after sync.
    void markFilterFieldsModified(const QString &id, const QStringList &fields);
    // Iter-night-14: reminders had the same outbound-stamping gap as filters
    // pre-iter-12, plus two extra wrinkles:
    //   (1) addReminder only stamps task_id / type / minute_offset /
    //       created_at / updated_at / is_deleted — the user-mutable "due",
    //       "location", and "recurrence_rule" fields were NEVER stamped
    //       initially either (also fixed in addReminder's initialFields).
    //   (2) No ReminderViewModel exists; QML mutates Reminder* directly via
    //       setDueDatetime / setLocationCoords / setRepeatRule /
    //       setMinuteOffset → markDataChanged. Those setters never wrote
    //       per-field _meta, so on conflict the Merger fell back to the
    //       empty-meta whole-entity LWW branch and silently lost the newly
    //       set due time / geofence / RRULE.
    // QML calls this helper after the setter chain, so it must be Q_INVOKABLE.
    Q_INVOKABLE void markReminderFieldsModified(const QString &id,
                                                const QStringList &fields);

    // --- Inbound CRDT meta application ---
    // Q2.18-A FIX: SyncEngine::applyCommit needs to push the merged per-field
    // _meta back into the corresponding meta map after applying an inbound
    // *_update. The semantics are PER-FIELD MERGE: every field key present
    // in `incomingMeta` overwrites the local stamp; fields not present are
    // left untouched. This preserves local-only stamps (the inbound commit
    // only carries stamps for the fields it modified) while accepting the
    // inbound stamps for the fields it touched.
    //
    // These methods also emit the corresponding <entity>Changed() + the
    // aggregate dataChanged() signals so FileManager autosaves and the UI
    // refreshes — but they do NOT re-stamp updated_at (which would clobber
    // the inbound updated_at stamp and silently downgrade per-field LWW).
    //
    // No-op if the entity does not exist locally.
    void applyInboundTaskMeta(const QString &id, const QJsonObject &incomingMeta);
    void applyInboundProjectMeta(const QString &id, const QJsonObject &incomingMeta);
    void applyInboundSectionMeta(const QString &id, const QJsonObject &incomingMeta);
    void applyInboundLabelMeta(const QString &id, const QJsonObject &incomingMeta);
    void applyInboundFilterMeta(const QString &id, const QJsonObject &incomingMeta);
    void applyInboundReminderMeta(const QString &id, const QJsonObject &incomingMeta);
    void applyInboundCommentMeta(const QString &id, const QJsonObject &incomingMeta);

    // --- Auto-archive ---
    // Iterates non-archived non-inbox projects. For each, computes the most
    // recent task `updated_at` timestamp. If all tasks are completed AND the
    // most recent timestamp is older than `daysThreshold`, archives the
    // project. Returns the count of newly archived projects.
    // No-op when daysThreshold <= 0.
    int autoArchiveStaleProjects(int daysThreshold);

    // --- Vacuum (Q3.10) ---
    //
    // Permanently removes (hard-purges) tombstoned entities whose
    // `updated_at` is older than `daysToKeep` days. Unlike the soft-delete
    // tombstone (which sync still propagates), vacuum erases the entity
    // from the in-memory store AND drops its per-entity CRDT _meta blob
    // so the next save writes a smaller state.json.
    //
    // Safety:
    //   * Tombstones younger than `daysToKeep` are preserved — peers that
    //     haven't synced for that long still see the deletion.
    //   * Entities referenced by any pending (unsynced) commit in
    //     `local_log/` are skipped — peer that receives that commit must
    //     still find a tombstone to merge against.
    //   * The default `daysToKeep` is conservatively large (90 days) to
    //     give every offline peer time to round-trip a sync.
    //   * Caller is responsible for ensuring a state.json safety backup
    //     before invoking — FileManager::createPreVacuumBackup().
    //
    // Returns count of entities removed across every collection. Emits
    // dataChanged() exactly once at the end so FileManager autosaves the
    // smaller state in a single coalesced write.
    Q_INVOKABLE int vacuum(int daysToKeep = 90);
    Q_INVOKABLE int vacuum(int daysToKeep, const QStringList &pendingEntityIds);

    // --- Serialization ---
    QJsonObject toJson() const;
    void loadFromJson(const QJsonObject &root);
    void clear();
    // True when the last loadFromJson() had to repair the loaded data (e.g.
    // merged duplicate Inboxes, D3), i.e. the in-memory state differs from the
    // JSON that was loaded and should be persisted.
    bool lastLoadModifiedState() const { return m_lastLoadModifiedState; }

    // --- CRDT field stamping ---
    /**
     * @brief Stamp CRDT per-field metadata on a JSON entity object for a
     *        specific field. Reads deviceId from QSettings.
     *
     * This is a static utility intentionally exposed here so ViewModels that
     * build SyncOperation data payloads can stamp fields they are sending.
     */
    static void stampField(QJsonObject &entity, const QString &fieldName);

signals:
    void aboutToReset();
    void tasksChanged();
    void projectsChanged();
    void sectionsChanged();
    void labelsChanged();
    void filtersChanged();
    void remindersChanged();
    void commentsChanged();
    void collaboratorsChanged();
    void settingsChanged();
    void dataChanged();

private:
    // Shared converter for the *Data() accessors: Task* list →
    // QVariantList<QVariantMap> with camelCase keys mirroring Task's
    // Q_PROPERTY names. Centralised so every accessor exposes one shape.
    static QVariantList tasksToData(const QList<Task *> &list);
    static QVariantList projectsToData(const QList<Project *> &list);
    static QVariantList labelsToData(const QList<Label *> &list);

    // Collaborator helpers (see the public --- Collaborators --- block).
    static QString normCollaboratorId(const QString &id);   // "" ⇒ "me"
    QJsonObject collaboratorOverride(const QString &id) const;

    // D12: m_labelNameIndex holds LIVE labels only. indexLabelName() ignores
    // tombstones; unindexLabelName() removes `key` if it maps to `label` and
    // re-points it at another live label with the same case-folded name.
    void indexLabelName(Label *label);
    void unindexLabelName(const QString &key, const Label *label);

    // D3: enforce a single live Inbox after a load (duplicates appear when two
    // fresh devices first sync). Returns true if anything was changed.
    bool normalizeInboxes();
    bool m_lastLoadModifiedState = false;

    // Stamp a single field in m_entityMeta[entityId] using the local deviceId.
    void stampEntityField(const QString &entityId,
                          QMap<QString, QJsonObject> &metaMap,
                          const QString &fieldName);

    // S1: stamp m_settingsMeta[key] = {ts: HLC::now, dev} so the Merger can
    // resolve settings per key (LWW) instead of taking the remote blob whole.
    void stampSettingKey(const QString &key);

    // Inject the stored _meta blob back into a serialised entity JSON.
    static QJsonObject injectMeta(QJsonObject entityJson,
                                  const QMap<QString, QJsonObject> &metaMap,
                                  const QString &entityId);

    // Extract and remove _meta from a loaded entity JSON, storing it in metaMap.
    static QJsonObject extractMeta(QJsonObject entityJson,
                                   QMap<QString, QJsonObject> &metaMap,
                                   const QString &entityId);

    // Apply forward-only migrations to bring `root` up to CURRENT_SCHEMA_VERSION.
    // Operates on the QJsonObject in-place. Returns the version after migration.
    static int migrateJson(QJsonObject &root, int fromVersion);

    // Return the local deviceId (from QSettings, same key as SyncEngine uses).
    static QString localDeviceId();

    int m_loadedSchemaVersion = 0;

    QList<Task *> m_tasks;
    QMap<QString, Task *> m_taskIndex;
    QMap<QString, QJsonObject> m_taskMeta;       ///< entityId → "_meta" blob

    // perf: lazy relational buckets so tasksForProject/tasksForSection/subtasks
    // are O(1) lookups instead of O(T) scans (which previously ran inside
    // per-edit O(P*T) UI loops). Built on demand and invalidated whenever any
    // mutation fires dataChanged() — so no per-mutation upkeep can be missed.
    // Order within each bucket mirrors m_tasks (the queries' original order).
    void ensureTaskBuckets() const;
    // Tasks are handed out as mutable pointers and callers change projectId /
    // sectionId / parentId / isDeleted directly (not always via a DataStore
    // method that emits dataChanged). Connect those field-change signals so a
    // direct mutation still invalidates the buckets. Called for every task as it
    // is registered into m_tasks.
    void watchTaskForBuckets(Task *t);
    mutable QHash<QString, QList<Task *>> m_tasksByProject;  ///< top-level only
    mutable QHash<QString, QList<Task *>> m_tasksBySection;  ///< any depth
    mutable QHash<QString, QList<Task *>> m_subtasksByParent;
    mutable bool m_taskBucketsDirty = true;

    QList<Project *> m_projects;
    QMap<QString, Project *> m_projectIndex;
    QMap<QString, QJsonObject> m_projectMeta;

    QList<Section *> m_sections;
    QMap<QString, Section *> m_sectionIndex;
    QMap<QString, QJsonObject> m_sectionMeta;

    QList<Label *> m_labels;
    QMap<QString, Label *> m_labelIndex;
    QMap<QString, Label *> m_labelNameIndex;
    QMap<QString, QJsonObject> m_labelMeta;

    QList<Filter *> m_filters;
    QMap<QString, Filter *> m_filterIndex;
    QMap<QString, QJsonObject> m_filterMeta;

    QList<Reminder *> m_reminders;
    QMap<QString, Reminder *> m_reminderIndex;
    QMap<QString, QJsonObject> m_reminderMeta;

    QList<Comment *> m_comments;
    QMap<QString, Comment *> m_commentIndex;
    QMap<QString, QJsonObject> m_commentMeta;

    QJsonObject m_settings;
    // S1: per-key LWW stamps for m_settings — { "<key>": {ts, dev} }. Serialised
    // as the top-level "settings_meta" object (deliberately NOT inside
    // "settings", which other code iterates). A stamp whose key is absent from
    // m_settings records that the key was removed on this device.
    QJsonObject m_settingsMeta;
};
