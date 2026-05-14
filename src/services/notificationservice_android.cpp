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
    createNotificationChannel();
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
        3 // NotificationManager.IMPORTANCE_DEFAULT
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

    // B-5.6: use persistent allocator instead of qHash
    int notifId = taskId.isEmpty() ? 0 : allocateId(taskId);

    QJniObject::callStaticMethod<void>(
        k_helperClass,
        "showNotification",
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        context.object<jobject>(),
        channelId.object<jstring>(),
        notifId,
        jTitle.object<jstring>(),
        jBody.object<jstring>(),
        jTaskId.object<jstring>()
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
    QJniObject sdkInt = QJniObject::getStaticField<jint>(
        "android/os/Build$VERSION", "SDK_INT");
    const int apiLevel = sdkInt.object<jint>();
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
