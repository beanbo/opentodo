package org.opentodo;

import android.app.AlarmManager;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

/**
 * Helper class for Android notifications.
 *
 * Method names are aligned exactly with the JNI call-sites in
 * notificationservice_android.cpp. Do not rename without updating both sides.
 *
 * JNI signatures used by C++:
 *   createChannel                (Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;I)V
 *   showNotification             (Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
 *   scheduleNotification         (Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V
 *   cancelNotification           (Landroid/content/Context;I)V
 *   cancelAllNotifications       (Landroid/content/Context;)V
 *   requestNotificationPermission(Landroid/content/Context;)V
 *
 * The native callback called from Java after a permission result:
 *   Java_org_opentodo_NotificationHelper_onPermissionResult(JNIEnv*, jclass, jboolean)
 */
public class NotificationHelper {

    public static final String CHANNEL_TASKS     = "opentodo_tasks";
    public static final String CHANNEL_REMINDERS = "opentodo_reminders";
    public static final String CHANNEL_SYNC      = "opentodo_sync";

    // Request code used for POST_NOTIFICATIONS permission dialog.
    private static final int PERM_REQUEST_CODE = 42;

    // -----------------------------------------------------------------------
    // createChannel — called from C++ createNotificationChannel().
    // Creates a single named channel with the given importance level.
    // NOTE: C++ always passes channelId="opentodo_channel" and importance=4
    // (IMPORTANCE_HIGH) so reminders pop as heads-up. The three named constants
    // above are kept for potential future use.
    // -----------------------------------------------------------------------
    public static void createChannel(Context context, String channelId,
                                     String channelName, int importance) {
        NotificationManager nm =
            (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;

        NotificationChannel ch = new NotificationChannel(channelId, channelName, importance);
        ch.setDescription("OpenTodo notifications");
        ch.enableVibration(true);
        ch.enableLights(true);
        nm.createNotificationChannel(ch);
    }

    // -----------------------------------------------------------------------
    // showNotification — C++ JNI signature:
    //   (Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
    // Parameter order matches the C++ callStaticMethod call:
    //   (context, channelId, notifId, title, body, taskId)
    // -----------------------------------------------------------------------
    public static void showNotification(Context context, String channelId,
                                        int id, String title, String body,
                                        String taskId) {
        NotificationManager nm =
            (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;

        Intent intent = context.getPackageManager()
                               .getLaunchIntentForPackage(context.getPackageName());
        if (intent == null) intent = new Intent();
        if (taskId != null && !taskId.isEmpty()) intent.putExtra("taskId", taskId);
        intent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);

        PendingIntent pi = PendingIntent.getActivity(
            context, id, intent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        NotificationCompat.Builder builder =
            new NotificationCompat.Builder(context, channelId)
                .setSmallIcon(android.R.drawable.ic_popup_reminder)
                .setContentTitle(title)
                .setContentText(body)
                .setAutoCancel(true)
                .setContentIntent(pi)
                .setPriority(NotificationCompat.PRIORITY_HIGH);

        nm.notify(id, builder.build());
    }

    // -----------------------------------------------------------------------
    // scheduleNotification — uses AlarmManager.setExactAndAllowWhileIdle()
    // C++ JNI signature:
    //   (Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V
    // Parameter order: (context, channelId, notifId, title, body, taskId, triggerAtMillis)
    // -----------------------------------------------------------------------
    public static void scheduleNotification(Context context, String channelId,
                                            int id, String title, String body,
                                            String taskId, long triggerAtMillis) {
        AlarmManager am = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
        if (am == null) return;

        Intent intent = new Intent(context, NotificationReceiver.class);
        intent.putExtra("notifId", id);
        intent.putExtra("title", title);
        intent.putExtra("body", body);
        intent.putExtra("channelId", channelId);
        intent.putExtra("taskId", taskId);

        PendingIntent pi = PendingIntent.getBroadcast(
            context, id, intent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        // ── B-9.4: exact-alarm permission guard (Android 12+/API 31+) ─────────
        // Apps targeting API 33+ (this app: targetSdk 34) are DENIED the
        // SCHEDULE_EXACT_ALARM special-access permission by default, so
        // setExactAndAllowWhileIdle() throws SecurityException unless the user
        // manually enabled "Alarms & reminders" in system settings. Called
        // unguarded from the JNI bridge that crashes the app (pending JNI
        // exception) or silently drops every reminder. Probe
        // canScheduleExactAlarms() and degrade to an inexact-but-Doze-friendly
        // alarm so the reminder still fires (just not to the exact second) when
        // exact alarms aren't allowed.
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                if (am.canScheduleExactAlarms()) {
                    am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMillis, pi);
                } else {
                    am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMillis, pi);
                }
            } else {
                // API 26–30: SCHEDULE_EXACT_ALARM is auto-granted; exact is safe.
                am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMillis, pi);
            }
        } catch (SecurityException se) {
            // Belt-and-suspenders: some OEM ROMs still reject exact alarms even
            // when canScheduleExactAlarms() reported true. Never crash a reminder.
            am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, triggerAtMillis, pi);
        }
    }

    // -----------------------------------------------------------------------
    // cancelNotification — cancels both the AlarmManager alarm and any
    // already-shown notification.
    // C++ JNI signature: (Landroid/content/Context;I)V
    // -----------------------------------------------------------------------
    public static void cancelNotification(Context context, int id) {
        // Cancel pending alarm
        AlarmManager am = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
        if (am != null) {
            Intent intent = new Intent(context, NotificationReceiver.class);
            PendingIntent pi = PendingIntent.getBroadcast(
                context, id, intent,
                PendingIntent.FLAG_NO_CREATE | PendingIntent.FLAG_IMMUTABLE);
            if (pi != null) am.cancel(pi);
        }
        // Cancel already-shown notification
        NotificationManager nm =
            (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.cancel(id);
    }

    // -----------------------------------------------------------------------
    // cancelAllNotifications — matches C++ call site (renamed from cancelAll).
    // C++ JNI signature: (Landroid/content/Context;)V
    // -----------------------------------------------------------------------
    public static void cancelAllNotifications(Context context) {
        NotificationManager nm =
            (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.cancelAll();
        // Note: individual AlarmManager intents are keyed by notification id,
        // which we cannot enumerate here. Apps that need full alarm cancellation
        // should call cancelNotification() per-id before calling cancelAllNotifications().
    }

    // -----------------------------------------------------------------------
    // requestNotificationPermission — API 33+ POST_NOTIFICATIONS runtime grant.
    // Below API 33 notifications are allowed by default; the native callback
    // is invoked with granted=true immediately so C++ keeps working uniformly.
    // C++ JNI signature: (Landroid/content/Context;)V
    //
    // After the user responds the Activity's onRequestPermissionsResult must
    // call NotificationHelper.onPermissionsResult(granted) which in turn
    // calls the native C++ callback Java_org_opentodo_NotificationHelper_onPermissionResult.
    // -----------------------------------------------------------------------
    public static void requestNotificationPermission(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            // Pre-API 33: no runtime permission needed; report granted immediately.
            onPermissionsResult(true);
            return;
        }

        if (context instanceof android.app.Activity) {
            ActivityCompat.requestPermissions(
                (android.app.Activity) context,
                new String[]{"android.permission.POST_NOTIFICATIONS"},
                PERM_REQUEST_CODE);
            // Result comes via onRequestPermissionsResult -> onPermissionsResult()
        } else {
            // Context is not an Activity (e.g. Service/Application); can't show a dialog.
            // Fall back to a permission check and report current state.
            int check = ContextCompat.checkSelfPermission(
                context, "android.permission.POST_NOTIFICATIONS");
            onPermissionsResult(check == android.content.pm.PackageManager.PERMISSION_GRANTED);
        }
    }

    // -----------------------------------------------------------------------
    // Called by the host Activity's onRequestPermissionsResult for request
    // code PERM_REQUEST_CODE.
    // -----------------------------------------------------------------------
    public static void onPermissionsResult(boolean granted) {
        onPermissionResult(granted);
    }

    // -----------------------------------------------------------------------
    // Native callback — implemented in notificationservice_android.cpp.
    // Called on the Android UI thread; C++ implementation must queue to the
    // Qt thread via QMetaObject::invokeMethod(…, Qt::QueuedConnection).
    // -----------------------------------------------------------------------
    private static native void onPermissionResult(boolean granted);
}
