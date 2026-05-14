package org.opentodo;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * BroadcastReceiver that fires when an AlarmManager-scheduled notification is due.
 * Registered in AndroidManifest.xml; invoked by NotificationHelper.scheduleNotification().
 */
public class NotificationReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(Context context, Intent intent) {
        int notifId     = intent.getIntExtra("notifId", 0);
        String title    = intent.getStringExtra("title");
        String body     = intent.getStringExtra("body");
        String channelId = intent.getStringExtra("channelId");
        String taskId   = intent.getStringExtra("taskId");

        if (title == null) title = "";
        if (body == null) body = "";
        if (channelId == null) channelId = NotificationHelper.CHANNEL_REMINDERS;
        if (taskId == null) taskId = "";

        // Ensure the channel exists before posting (process may have been killed).
        NotificationHelper.createChannel(
            context, channelId, "OpenTodo Reminders",
            android.app.NotificationManager.IMPORTANCE_HIGH);

        NotificationHelper.showNotification(context, channelId, notifId, title, body, taskId);
    }
}
