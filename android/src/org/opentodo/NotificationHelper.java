package org.opentodo;

import android.app.AlarmManager;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;
import java.util.Map;
import org.json.JSONException;
import org.json.JSONObject;

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
 *   cancelScheduledNotification  (Landroid/content/Context;I)V
 *   cancelAllNotifications       (Landroid/content/Context;)V
 *   requestNotificationPermission(Landroid/content/Context;)V
 *
 * The native callbacks called from Java:
 *   Java_org_opentodo_NotificationHelper_onPermissionResult(JNIEnv*, jclass, jboolean)
 *   Java_org_opentodo_NotificationHelper_nativeOnNotificationAction(JNIEnv*, jclass, jstring, jstring)
 *   Java_org_opentodo_NotificationHelper_nativeOnScheduledNotificationFired(JNIEnv*, jclass, jstring, jstring, jstring)
 *
 * Every alarm armed by scheduleNotification() is also persisted in the
 * "opentodo_alarms" SharedPreferences (key = notification id, value = JSON
 * {channelId, title, body, taskId, trigger}) and removed when it fires or is
 * cancelled, so BootReceiver can re-arm reminders after a reboot — AlarmManager
 * forgets every alarm on reboot.
 */
public class NotificationHelper {

    public static final String CHANNEL_TASKS     = "opentodo_tasks";
    public static final String CHANNEL_REMINDERS = "opentodo_reminders";
    public static final String CHANNEL_SYNC      = "opentodo_sync";

    // Request code used for POST_NOTIFICATIONS permission dialog.
    private static final int PERM_REQUEST_CODE = 42;

    // SharedPreferences file holding the persisted alarms (see class doc).
    private static final String ALARM_PREFS = "opentodo_alarms";
    // A reminder that came due while the device was off is still posted on
    // boot if it is at most this late — same 1-hour grace window the C++
    // backends use for past-due schedules (B-5.8/9).
    private static final long MISSED_GRACE_MS = 60L * 60L * 1000L;

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

    // Backward-compatible 6-arg overload for callers that don't supply action
    // labels (e.g. NotificationReceiver's alarm path). Delegates with null labels,
    // which the main method falls back to its Russian defaults for.
    public static void showNotification(Context context, String channelId,
                                        int id, String title, String body,
                                        String taskId) {
        showNotification(context, channelId, id, title, body, taskId, null, null);
    }

    // -----------------------------------------------------------------------
    // showNotification — C++ JNI signature:
    //   (Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V
    // Parameter order matches the C++ callStaticMethod call:
    //   (context, channelId, notifId, title, body, taskId, completeLabel, snoozeLabel)
    // The action-button labels are supplied by C++ (tr()'d) so they go through
    // Qt's translation system rather than being hardcoded here.
    // -----------------------------------------------------------------------
    public static void showNotification(Context context, String channelId,
                                        int id, String title, String body,
                                        String taskId, String completeLabel,
                                        String snoozeLabel) {
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

        // Reminder action buttons — Complete / Snooze (task reminders only).
        // Labels come from C++ (tr()'d); fall back to Russian defaults if absent.
        if (taskId != null && !taskId.isEmpty()) {
            String complete = (completeLabel != null && !completeLabel.isEmpty()) ? completeLabel : "Выполнить";
            String snooze   = (snoozeLabel   != null && !snoozeLabel.isEmpty())   ? snoozeLabel   : "Отложить";
            builder.addAction(android.R.drawable.checkbox_on_background, complete,
                actionPendingIntent(context, id, taskId, "complete"));
            builder.addAction(android.R.drawable.ic_popup_reminder, snooze,
                actionPendingIntent(context, id, taskId, "snooze"));
        }

        nm.notify(id, builder.build());
    }

    // Build a PendingIntent that routes a notification-action button press back
    // to NotificationReceiver carrying the action verb + task id.
    private static PendingIntent actionPendingIntent(Context context, int notifId,
                                                     String taskId, String action) {
        Intent intent = new Intent(context, NotificationReceiver.class);
        // Distinct action string per (verb, id) so the PendingIntents differ.
        intent.setAction("org.opentodo.NOTIF_ACTION." + action + "." + notifId);
        intent.putExtra("notifAction", action);
        intent.putExtra("taskId", taskId);
        intent.putExtra("notifId", notifId);
        int requestCode = notifId * 8 + (action.equals("complete") ? 1 : 2);
        return PendingIntent.getBroadcast(context, requestCode, intent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    // Route an action press to native (app process alive) or persist it to
    // SharedPreferences (app dead) for the C++ side to drain on next start.
    public static void dispatchAction(Context context, String taskId, String action) {
        try {
            nativeOnNotificationAction(taskId, action);
        } catch (Throwable t) {
            android.content.SharedPreferences sp =
                context.getSharedPreferences("opentodo_notif", Context.MODE_PRIVATE);
            String existing = sp.getString("pending_actions", "");
            String entry = taskId + "" + action;
            sp.edit().putString("pending_actions",
                existing.isEmpty() ? entry : existing + "\n" + entry).apply();
        }
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

        // Persist so BootReceiver can re-arm it after a reboot.
        rememberAlarm(context, id, channelId, title, body, taskId, triggerAtMillis);
    }

    // Cancel the pending AlarmManager alarm for `id` (if any).
    private static void cancelAlarm(Context context, int id) {
        AlarmManager am = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
        if (am != null) {
            // Matches the scheduled PendingIntent: same component, no action,
            // same request code (extras are not part of PendingIntent identity).
            Intent intent = new Intent(context, NotificationReceiver.class);
            PendingIntent pi = PendingIntent.getBroadcast(
                context, id, intent,
                PendingIntent.FLAG_NO_CREATE | PendingIntent.FLAG_IMMUTABLE);
            if (pi != null) {
                am.cancel(pi);
                pi.cancel();
            }
        }
        forgetAlarm(context, id);
    }

    // -----------------------------------------------------------------------
    // cancelNotification — cancels both the AlarmManager alarm and any
    // already-shown notification. Used when a reminder is gone for good
    // (deleted reminder, completed / deleted task).
    // C++ JNI signature: (Landroid/content/Context;I)V
    // -----------------------------------------------------------------------
    public static void cancelNotification(Context context, int id) {
        cancelAlarm(context, id);
        // Cancel already-shown notification
        NotificationManager nm =
            (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.cancel(id);
    }

    // -----------------------------------------------------------------------
    // cancelScheduledNotification — cancels ONLY the pending alarm and leaves
    // an already-posted notification in the shade. The C++ ReminderScheduler
    // calls this on every reschedule pass (each edit, sync apply, app start);
    // using cancelNotification() there dismissed reminders the user had not
    // acted on yet.
    // C++ JNI signature: (Landroid/content/Context;I)V
    // -----------------------------------------------------------------------
    public static void cancelScheduledNotification(Context context, int id) {
        cancelAlarm(context, id);
    }

    // -----------------------------------------------------------------------
    // cancelAllNotifications — matches C++ call site (renamed from cancelAll).
    // Dismisses every posted notification and cancels every persisted alarm.
    // C++ JNI signature: (Landroid/content/Context;)V
    // -----------------------------------------------------------------------
    public static void cancelAllNotifications(Context context) {
        NotificationManager nm =
            (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) nm.cancelAll();
        // The persisted alarm list is the only way to enumerate armed alarms.
        Map<String, ?> all = alarmPrefs(context).getAll();
        for (String key : all.keySet()) {
            try {
                cancelAlarm(context, Integer.parseInt(key));
            } catch (NumberFormatException nfe) {
                alarmPrefs(context).edit().remove(key).apply();
            }
        }
    }

    // -----------------------------------------------------------------------
    // Alarm persistence (reboot survival).
    // -----------------------------------------------------------------------
    private static SharedPreferences alarmPrefs(Context context) {
        return context.getSharedPreferences(ALARM_PREFS, Context.MODE_PRIVATE);
    }

    private static void rememberAlarm(Context context, int id, String channelId,
                                      String title, String body, String taskId,
                                      long triggerAtMillis) {
        try {
            JSONObject o = new JSONObject();
            o.put("channelId", channelId != null ? channelId : "");
            o.put("title", title != null ? title : "");
            o.put("body", body != null ? body : "");
            o.put("taskId", taskId != null ? taskId : "");
            o.put("trigger", triggerAtMillis);
            alarmPrefs(context).edit().putString(String.valueOf(id), o.toString()).apply();
        } catch (JSONException je) {
            // Not persisted → only reboot survival is lost; the alarm is armed.
        }
    }

    private static void forgetAlarm(Context context, int id) {
        alarmPrefs(context).edit().remove(String.valueOf(id)).apply();
    }

    // Called by NotificationReceiver when an alarm fires: drop it from the
    // persisted list and, when the app process is alive, tell C++ so the
    // reminder is logged in the in-app notification centre at the time it
    // really fires. `schedId` is the id C++ scheduled the alarm under.
    public static void onScheduledAlarmFired(Context context, int id, String schedId,
                                             String title, String body) {
        forgetAlarm(context, id);
        if (schedId == null || schedId.isEmpty()) return;
        try {
            nativeOnScheduledNotificationFired(schedId, title, body);
        } catch (Throwable t) {
            // Native library not loaded (the alarm started a bare process for
            // this broadcast) — nothing in-app to log into; the OS
            // notification itself has been posted.
        }
    }

    // Called by BootReceiver: re-arm every persisted alarm that is still in
    // the future, post the ones that came due while the device was off (within
    // MISSED_GRACE_MS), and drop older / unreadable entries.
    public static void rearmPersistedAlarms(Context context) {
        final long now = System.currentTimeMillis();
        Map<String, ?> all = alarmPrefs(context).getAll(); // a copy — safe to edit prefs below
        for (Map.Entry<String, ?> e : all.entrySet()) {
            final String key = e.getKey();
            int id;
            try {
                id = Integer.parseInt(key);
            } catch (NumberFormatException nfe) {
                alarmPrefs(context).edit().remove(key).apply();
                continue;
            }
            Object value = e.getValue();
            if (!(value instanceof String)) {
                forgetAlarm(context, id);
                continue;
            }
            try {
                JSONObject o = new JSONObject((String) value);
                String channelId = o.optString("channelId", "");
                if (channelId.isEmpty()) channelId = CHANNEL_REMINDERS;
                String title  = o.optString("title", "");
                String body   = o.optString("body", "");
                String taskId = o.optString("taskId", "");
                long trigger  = o.optLong("trigger", 0L);
                if (trigger > now) {
                    scheduleNotification(context, channelId, id, title, body, taskId, trigger);
                } else {
                    forgetAlarm(context, id);
                    if (trigger > 0 && now - trigger <= MISSED_GRACE_MS) {
                        createChannel(context, channelId, "OpenTodo Reminders",
                                      NotificationManager.IMPORTANCE_HIGH);
                        showNotification(context, channelId, id, title, body, taskId);
                    }
                }
            } catch (JSONException je) {
                forgetAlarm(context, id);
            }
        }
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

    // Native callback for a notification action button — implemented in
    // notificationservice_android.cpp
    // (Java_org_opentodo_NotificationHelper_nativeOnNotificationAction).
    private static native void nativeOnNotificationAction(String taskId, String action);

    // Native callback for a scheduled alarm that fired — implemented in
    // notificationservice_android.cpp
    // (Java_org_opentodo_NotificationHelper_nativeOnScheduledNotificationFired).
    private static native void nativeOnScheduledNotificationFired(String id, String title,
                                                                  String body);
}
