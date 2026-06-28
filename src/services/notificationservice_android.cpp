#include "notificationservice_android.h"

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QSettings>
#include <QMetaObject>

// JNI helper class name used for all notification operations
static constexpr const char *k_helperClass = "org/opentodo/NotificationHelper";

// ── B-5.4: global pointer for the JNI → Qt permission-result callback ──────
// Written on the Qt main thread (in requestPermission()), read on the Android
// UI thread (Java_org_opentodo_…) — protected by the fact that only one
// permission request can be in flight at a time.
static AndroidNotificationService *s_pendingService = nullptr;
// Set in the ctor; used by the notification-action JNI callback below to route
// a Complete/Snooze button press from the Android UI thread to the Qt thread.
static AndroidNotificationService *s_actionService = nullptr;

// ── B-5.4: static JNI callback — called by NotificationHelper.onPermissionResult()
// This runs on the Android UI thread.  We queue back to the Qt thread via
// QMetaObject::invokeMethod with Qt::QueuedConnection.
extern "C" JNIEXPORT void JNICALL
Java_org_opentodo_NotificationHelper_onPermissionResult(JNIEnv *, jclass, jboolean granted)
{
    AndroidNotificationService *svc = s_pendingService;
    if (!svc) return;
    s_pendingService = nullptr;

    const bool grantedBool = static_cast<bool>(granted);
    // Queue to Qt thread: signal emission is not safe from Android UI thread
    QMetaObject::invokeMethod(svc, [svc, grantedBool]() {
        if (grantedBool)
            emit svc->permissionGranted();
        else
            emit svc->permissionDenied();
        // Q-Android-Audit #2.1: single "we got an answer" signal so the QML
        // layer can flip notificationPermissionAsked AFTER the OS has actually
        // returned, instead of synchronously after requestPermission().
        emit svc->permissionRequestCompleted(grantedBool);
    }, Qt::QueuedConnection);
}

// ── Notification action callback (Complete / Snooze button) ─────────────────
// Called by NotificationHelper.dispatchAction() on the Android UI thread when
// the app process is alive. Queues to the Qt thread and emits
// notificationActionInvoked so main.cpp can apply the action.
extern "C" JNIEXPORT void JNICALL
Java_org_opentodo_NotificationHelper_nativeOnNotificationAction(
    JNIEnv *env, jclass, jstring jTaskId, jstring jAction)
{
    AndroidNotificationService *svc = s_actionService;
    if (!svc || jTaskId == nullptr || jAction == nullptr)
        return;
    const char *t = env->GetStringUTFChars(jTaskId, nullptr);
    const char *a = env->GetStringUTFChars(jAction, nullptr);
    if (!t || !a) {
        // OOM: GetStringUTFChars returned null and may have a pending exception.
        if (t) env->ReleaseStringUTFChars(jTaskId, t);
        if (a) env->ReleaseStringUTFChars(jAction, a);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    const QString taskId = QString::fromUtf8(t);
    const QString action = QString::fromUtf8(a);
    env->ReleaseStringUTFChars(jTaskId, t);
    env->ReleaseStringUTFChars(jAction, a);
    QMetaObject::invokeMethod(svc, [svc, taskId, action]() {
        emit svc->notificationActionInvoked(taskId, action);
    }, Qt::QueuedConnection);
}

// ── B-5.6: persistent notification-id allocator using QSettings ─────────────
// Replaces qHash(taskId) which collides for different taskIds.
static int allocateId(const QString &taskId)
{
    QSettings s;
    auto map = s.value(QStringLiteral("notif/idmap")).toMap();
    if (map.contains(taskId))
        return map.value(taskId).toInt();

    int next = s.value(QStringLiteral("notif/idnext"), 1).toInt();
    map[taskId] = next;
    s.setValue(QStringLiteral("notif/idnext"), next + 1);
    s.setValue(QStringLiteral("notif/idmap"), map);
    return next;
}

// ────────────────────────────────────────────────────────────────────────────

AndroidNotificationService::AndroidNotificationService(QObject *parent)
    : NotificationService(parent)
{
    s_actionService = this;
    createNotificationChannel();
    drainPendingActions();
}

AndroidNotificationService::~AndroidNotificationService()
{
    // Don't leave the JNI back-pointers dangling if a notification action fires
    // after this instance is gone (only resets them if they point at us).
    if (s_actionService == this)
        s_actionService = nullptr;
    if (s_pendingService == this)
        s_pendingService = nullptr;
}

void AndroidNotificationService::drainPendingActions()
{
    // Read + clear SharedPreferences("opentodo_notif")["pending_actions"], a
    // "taskIdaction" list (\n-separated) written by Java when an action
    // button was pressed while this process was dead.
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;
    QJniObject name = QJniObject::fromString(QStringLiteral("opentodo_notif"));
    QJniObject prefs = context.callObjectMethod(
        "getSharedPreferences",
        "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
        name.object<jstring>(), 0);
    if (!prefs.isValid())
        return;
    QJniObject key = QJniObject::fromString(QStringLiteral("pending_actions"));
    QJniObject def = QJniObject::fromString(QString());
    QJniObject val = prefs.callObjectMethod(
        "getString",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        key.object<jstring>(), def.object<jstring>());
    const QString pending = val.isValid() ? val.toString() : QString();
    if (pending.isEmpty())
        return;
    // Clear the pref so actions aren't replayed on every launch.
    QJniObject editor = prefs.callObjectMethod(
        "edit", "()Landroid/content/SharedPreferences$Editor;");
    if (editor.isValid()) {
        editor.callObjectMethod("remove",
            "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
            key.object<jstring>());
        editor.callMethod<jboolean>("commit", "()Z");
    }
    const QStringList entries = pending.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &e : entries) {
        const QStringList parts = e.split(QChar(0x0001));
        if (parts.size() != 2)
            continue;
        const QString taskId = parts.at(0);
        const QString action = parts.at(1);
        QMetaObject::invokeMethod(this, [this, taskId, action]() {
            emit notificationActionInvoked(taskId, action);
        }, Qt::QueuedConnection);
    }
}

void AndroidNotificationService::createNotificationChannel()
{
    if (m_channelCreated)
        return;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject channelId = QJniObject::fromString(QStringLiteral("opentodo_channel"));
    QJniObject channelName = QJniObject::fromString(QStringLiteral("OpenTodo Reminders"));

    QJniObject::callStaticMethod<void>(
        k_helperClass,
        "createChannel",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;I)V",
        context.object<jobject>(),
        channelId.object<jstring>(),
        channelName.object<jstring>(),
        // B-9.5: IMPORTANCE_HIGH (4), not DEFAULT (3). This channel is created
        // once at app startup; channel importance is immutable afterwards, so
        // THIS is the value that sticks. DEFAULT silently suppressed the
        // heads-up peek that NotificationReceiver.onReceive() (IMPORTANCE_HIGH)
        // and the PRIORITY_HIGH builder in showNotification() both intend for
        // reminders — leaving those two intents dead. Reminders heads-up now.
        4 // NotificationManager.IMPORTANCE_HIGH
    );

    m_channelCreated = true;
}

void AndroidNotificationService::showNotification(const QString &title, const QString &body, const QString &taskId)
{
    createNotificationChannel();

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject channelId = QJniObject::fromString(QStringLiteral("opentodo_channel"));
    QJniObject jTitle = QJniObject::fromString(title);
    QJniObject jBody = QJniObject::fromString(body);
    QJniObject jTaskId = QJniObject::fromString(taskId);
    // Q-audit #14: pass the action-button labels from C++ (via tr(), so they go
    // through Qt's translation system) rather than hardcoding them in Java.
    QJniObject jComplete = QJniObject::fromString(tr("Выполнить"));
    QJniObject jSnooze   = QJniObject::fromString(tr("Отложить"));

    // B-5.6: use persistent allocator instead of qHash
    int notifId = taskId.isEmpty() ? 0 : allocateId(taskId);

    QJniObject::callStaticMethod<void>(
        k_helperClass,
        "showNotification",
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        context.object<jobject>(),
        channelId.object<jstring>(),
        notifId,
        jTitle.object<jstring>(),
        jBody.object<jstring>(),
        jTaskId.object<jstring>(),
        jComplete.object<jstring>(),
        jSnooze.object<jstring>()
    );
}

void AndroidNotificationService::scheduleNotification(const QString &title, const QString &body,
                                                       const QDateTime &when, const QString &id)
{
    createNotificationChannel();

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 msUntil = now.msecsTo(when);

    // B-5.8/9: if target is more than 1 hour in the past, skip silently.
    if (msUntil <= 0) {
        if (when < now.addSecs(-3600)) {
            qDebug() << "[AndroidNotification] skipping stale past-due notification:" << id
                     << "target was" << when.toString(Qt::ISODate);
            return;
        }
        // Within the 1-hour grace window — show immediately.
        showNotification(title, body, id);
        return;
    }

    qint64 triggerAtMillis = when.toMSecsSinceEpoch();

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject channelId = QJniObject::fromString(QStringLiteral("opentodo_channel"));
    QJniObject jTitle = QJniObject::fromString(title);
    QJniObject jBody = QJniObject::fromString(body);
    QJniObject jId = QJniObject::fromString(id);

    // B-5.6: use persistent allocator
    int notifId = allocateId(id);

    QJniObject::callStaticMethod<void>(
        k_helperClass,
        "scheduleNotification",
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V",
        context.object<jobject>(),
        channelId.object<jstring>(),
        notifId,
        jTitle.object<jstring>(),
        jBody.object<jstring>(),
        jId.object<jstring>(),
        triggerAtMillis
    );
}

void AndroidNotificationService::cancelNotification(const QString &id)
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    // B-5.6: use persistent allocator to recover the same id we used when scheduling
    int notifId = allocateId(id);

    QJniObject::callStaticMethod<void>(
        k_helperClass,
        "cancelNotification",
        "(Landroid/content/Context;I)V",
        context.object<jobject>(),
        notifId
    );
}

void AndroidNotificationService::cancelAll()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();

    QJniObject::callStaticMethod<void>(
        k_helperClass,
        "cancelAllNotifications",
        "(Landroid/content/Context;)V",
        context.object<jobject>()
    );
}

bool AndroidNotificationService::hasPermission() const
{
    // B-5.5: below API 33, POST_NOTIFICATIONS did not exist — always granted.
    // QJniObject::getStaticField<jint>() returns a jint primitive directly;
    // wrapping it in a QJniObject and then unpacking via object<jint>() is
    // invalid (QJniObject::object<T> is jobject-only — Qt 6.11 enforces this
    // with a static_cast that fails to compile for primitive T).
    const jint apiLevel = QJniObject::getStaticField<jint>(
        "android/os/Build$VERSION", "SDK_INT");
    if (apiLevel < 33)
        return true;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject permission = QJniObject::fromString(
        QStringLiteral("android.permission.POST_NOTIFICATIONS"));

    jint result = QJniObject::callStaticMethod<jint>(
        "androidx/core/content/ContextCompat",
        "checkSelfPermission",
        "(Landroid/content/Context;Ljava/lang/String;)I",
        context.object<jobject>(),
        permission.object<jstring>()
    );

    // PackageManager.PERMISSION_GRANTED == 0
    return result == 0;
}

void AndroidNotificationService::requestPermission()
{
    // B-5.4: store 'this' so the JNI callback can emit the correct signal.
    // The Java side calls ActivityCompat.requestPermissions; when the user
    // responds, onRequestPermissionsResult calls NotificationHelper.onPermissionsResult()
    // which in turn calls the native Java_org_opentodo_NotificationHelper_onPermissionResult.
    // That JNI function queues permissionGranted/permissionDenied back to the Qt thread.
    s_pendingService = this;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        k_helperClass,
        "requestNotificationPermission",
        "(Landroid/content/Context;)V",
        context.object());
    // Do NOT emit permissionGranted() here — wait for the JNI callback.
}

#endif // Q_OS_ANDROID
