package org.opentodo;

import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import org.qtproject.qt.android.QtActivityBase;

/**
 * Q-Android-Audit #2.1: subclass of QtActivityBase that forwards the runtime
 * permission result for POST_NOTIFICATIONS (API 33+) back to
 * {@link NotificationHelper#onPermissionsResult(boolean)}.
 *
 * Qt 6 replaced the old org.qtproject.qt.android.bindings.QtActivity with
 * org.qtproject.qt.android.QtActivityBase. The bindings package no longer
 * exists; extend QtActivityBase instead.
 *
 * The request code 42 is hard-coded in
 * {@link NotificationHelper#requestNotificationPermission(android.content.Context)}
 * (private static final int PERM_REQUEST_CODE = 42). If that constant ever
 * changes, the check below must change in lockstep.
 *
 * AndroidManifest.xml must declare this class as the launcher activity:
 *   <activity android:name="org.opentodo.MainActivity" ...>
 */
public class MainActivity extends QtActivityBase {

    // Mirrors PERM_REQUEST_CODE in NotificationHelper.java. Kept package-private
    // there; duplicated as a literal here so both files compile independently.
    private static final int PERM_REQUEST_CODE = 42;

    // Implemented natively in src/sync/oauth_client.cpp
    // (Java_org_opentodo_MainActivity_nativeOnOAuthRedirect). Called when Android
    // routes the Google OAuth redirect Intent (custom scheme
    // com.googleusercontent.apps.<id>:/oauth2redirect?code=…) to us so the C++
    // OAuthClient can finish the PKCE token exchange.
    private static native void nativeOnOAuthRedirect(String uri);

    // Forward a redirect-bearing Intent to native iff its scheme is ours.
    private void forwardOAuthRedirect(Intent intent) {
        if (intent == null) return;
        final Uri data = intent.getData();
        if (data == null) return;
        final String scheme = data.getScheme();
        if (scheme != null && scheme.startsWith("com.googleusercontent.apps.")) {
            nativeOnOAuthRedirect(data.toString());
        }
    }

    @Override
    public void onNewIntent(Intent intent) {
        // launchMode="singleTask": the already-running app receives the OAuth
        // redirect here. Android foregrounds us, which resumes the Qt event loop
        // — unlike the old loopback flow that hung while backgrounded.
        super.onNewIntent(intent);
        setIntent(intent);
        forwardOAuthRedirect(intent);
    }

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
