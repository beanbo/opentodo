package org.opentodo;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Re-arms reminder alarms after a reboot or an app update.
 *
 * AlarmManager drops every alarm on reboot, so reminders scheduled while the
 * app was running were silently lost until the user next opened the app (the
 * C++ ReminderScheduler re-arms everything on start). NotificationHelper
 * persists each scheduled alarm in SharedPreferences; this receiver restores
 * them without starting the Qt runtime.
 *
 * Registered in AndroidManifest.xml for BOOT_COMPLETED (needs the
 * RECEIVE_BOOT_COMPLETED permission) and MY_PACKAGE_REPLACED.
 */
public class BootReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(Context context, Intent intent) {
        if (context == null || intent == null) return;
        final String action = intent.getAction();
        // Both are protected system broadcasts; ignore anything else that
        // might be aimed at this exported receiver.
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action)
                && !Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) {
            return;
        }
        NotificationHelper.rearmPersistedAlarms(context);
    }
}
