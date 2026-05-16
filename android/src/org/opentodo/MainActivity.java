package org.opentodo;

import android.content.pm.PackageManager;
import org.qtproject.qt.android.bindings.QtActivity;

/**
 * Q-Android-Audit #2.1: subclass of QtActivity that forwards the runtime
 * permission result for POST_NOTIFICATIONS (API 33+) back to
 * {@link NotificationHelper#onPermissionsResult(boolean)}.
 *
 * The stock QtActivity does not implement onRequestPermissionsResult in a way
 * that reaches our Java code, so the JNI permission bridge in
 * src/services/notificationservice_android.cpp never fires after the user taps
 * "Allow" or "Deny" in the system dialog.
 *
 * The request code 42 is hard-coded in
 * {@link NotificationHelper#requestNotificationPermission(android.content.Context)}
 * (private static final int PERM_REQUEST_CODE = 42). If that constant ever
 * changes, the check below must change in lockstep.
 *
 * AndroidManifest.xml must declare this class as the launcher activity:
 *   <activity android:name="org.opentodo.MainActivity" ...>
 */
public class MainActivity extends QtActivity {

    // Mirrors PERM_REQUEST_CODE in NotificationHelper.java. Kept package-private
    // there; duplicated as a literal here so both files compile independently.
    private static final int PERM_REQUEST_CODE = 42;

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        if (requestCode != PERM_REQUEST_CODE) return;

        // Multiple permissions can come back in one call; for POST_NOTIFICATIONS
        // we only ever request a single permission, so the first entry is the
        // canonical answer. Guard against an empty array (some OEM ROMs deliver
        // a zero-length grantResults when the user dismisses the dialog by
        // tapping outside).
        final boolean granted = grantResults != null
                                && grantResults.length > 0
                                && grantResults[0] == PackageManager.PERMISSION_GRANTED;

        NotificationHelper.onPermissionsResult(granted);
    }
}
