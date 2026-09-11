// ESP32-S3 example by maurigno: retain Spotify login in NVS across reboots.
// Adapted from maurigno's working ESP32-S3 application.
#include <Arduino.h>
#include <WiFi.h>
#include <SpotifyEsp32.h>
#include <Preferences.h>
#include <time.h>
#include <stdlib.h> // free() releases the copies returned by get_user_tokens().

const char* SSID = "your_ssid";
const char* PASSWORD = "your_password";
const char* CLIENT_ID = "your_client_id";
const char* CLIENT_SECRET = "your_client_secret";

// Set TRUE once if you want to ignore/delete the saved token
// Then set it back to false and upload again to retain login on later boots.
// and perform a fresh Spotify browser login.
const bool FORCE_NEW_LOGIN = false;

Preferences prefs;

String refreshToken;
Spotify* sp = nullptr;

void connect_to_wifi();
void syncTime();
void saveRefreshToken();

void setup() {
    Serial.begin(115200);
    delay(500);

    connect_to_wifi();
    syncTime(); // TLS certificate validation needs a valid clock.

    // Open the flash-backed NVS namespace for reading and writing.
    // Values survive resets and power loss until removed or flash is erased.
    if (!prefs.begin("spotify", false)) {
        Serial.println("Cannot open NVS; stopping setup.");
        return;
    }

    // --------------------------------------------------
    // FORCE A NEW LOGIN
    // --------------------------------------------------

    if (FORCE_NEW_LOGIN) {
        Serial.println();
        Serial.println("FORCE_NEW_LOGIN enabled.");
        Serial.println("Deleting saved Spotify refresh token...");

        prefs.remove("refresh_token");
        refreshToken = "";
    } else {
        refreshToken = prefs.getString("refresh_token", "");
    }

    // --------------------------------------------------
    // CREATE SPOTIFY INSTANCE
    // --------------------------------------------------

    if (refreshToken.length() > 0) {
        Serial.println("Refresh token found.");
        Serial.println("Using saved Spotify login.");

        sp = new Spotify(
            CLIENT_ID,
            CLIENT_SECRET,
            refreshToken.c_str()
        );
    } else {
        Serial.println("Starting WITHOUT saved authentication.");
        Serial.println("Open the Spotify authorization URL.");

        sp = new Spotify(
            CLIENT_ID,
            CLIENT_SECRET
        );
    }

    // Empty scopes select the library defaults. Optional in this fork,
    // which initializes the custom-scopes pointer.
    sp->set_scopes("");

    // Enable if needed
    //sp->set_log_level(SPOTIFY_LOG_DEBUG);

    // --------------------------------------------------
    // START SPOTIFY
    // --------------------------------------------------

    sp->begin();

    // --------------------------------------------------
    // NO TOKEN -> WAIT FOR BROWSER AUTHENTICATION
    // --------------------------------------------------

    if (refreshToken.length() == 0) {

        Serial.println("Waiting for Spotify authentication...");

        while (!sp->is_auth()) {
            sp->handle_client();
            delay(10);
        }

        Serial.println("Browser authentication completed.");

        // Save the newly issued refresh token
        saveRefreshToken();

        // saveRefreshToken() reports whether the flash write succeeded.
    }

    // --------------------------------------------------
    // TOKEN EXISTS -> TRY IT
    // --------------------------------------------------

    else {
        if (!sp->get_access_token()) {

            Serial.println();
            // A network/TLS failure does not prove the token was revoked.
            // Keep it in NVS and retry after checking Wi-Fi and the clock.
            Serial.println("Token request failed; keeping the saved token.");
            Serial.println("Check connectivity, then restart to retry.");
            Serial.println("For a revoked token, use FORCE_NEW_LOGIN.");
            delete sp;
            sp = nullptr;
            prefs.end();
            return;
        }

        Serial.println("Spotify authenticated.");
    }

    prefs.end(); // Close the handle; the stored token remains in flash.
}

void loop() {
    static String lastArtist;
    static String lastTrackname;

    if (sp == nullptr) {
        delay(1000);
        return;
    }

    String currentArtist = sp->current_artist_names();
    String currentTrackname = sp->current_track_name();

    if (
        currentArtist != lastArtist &&
        currentArtist != "Something went wrong" &&
        !currentArtist.isEmpty()
    ) {
        lastArtist = currentArtist;

        Serial.println(
            "Artist: " + currentArtist
        );
    }

    if (
        currentTrackname != lastTrackname &&
        currentTrackname != "Something went wrong" &&
        currentTrackname != "null" &&
        !currentTrackname.isEmpty()
    ) {
        lastTrackname = currentTrackname;

        Serial.println(
            "Track: " + currentTrackname
        );
    }

    delay(500); // Poll playback; print only when artist or track changes.
}

void saveRefreshToken() {
    user_tokens tokens = sp->get_user_tokens();

    if (
        tokens.refresh_token != nullptr &&
        strlen(tokens.refresh_token) > 0
    ) {
        String newToken = tokens.refresh_token;

        // Write only when the token changes to avoid unnecessary flash writes.
        if (newToken != refreshToken) {
            if (prefs.putString("refresh_token", newToken) == newToken.length()) {
                refreshToken = newToken;
                Serial.println("Refresh token written to NVS.");
            } else {
                Serial.println("ERROR: Could not save the refresh token.");
            }
        }
    } else {
        Serial.println("ERROR: Spotify did not return a refresh token.");
    }

    // This library returns three strdup() allocations; release all three.
    free(tokens.client_id);
    free(tokens.client_secret);
    free(tokens.refresh_token);
}

void connect_to_wifi() {
    WiFi.begin(
        SSID,
        PASSWORD
    );

    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("Connected to WiFi");
}

void syncTime() {
    Serial.print("Synchronizing time");

    configTime(
        0,
        0,
        "pool.ntp.org",
        "time.nist.gov"
    );

    time_t now = time(nullptr);

    // Wait for UTC before starting Spotify; this waits until NTP succeeds.
    while (now < 1735689600) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }

    Serial.println();
    Serial.println("Time synchronized");
}