# SpotifyESP32 — ESP32 fork by maurigno

This library is a wrapper for the [Spotify Web API](https://developer.spotify.com/documentation/web-api/) designed to work with the [ESP32](https://www.espressif.com/en/products/socs/esp32/overview) microcontroller.

This version was developed by **maurigno**, with authentication and TLS changes for ESP32 devices. It has been tested by maurigno with a **LILYGO T-Display ESP32** and **ESP32-S3 devices**. The original library was created by Finian Landes and its upstream contributors.

## Differences from upstream

This fork builds on [FinianLandes/SpotifyEsp32](https://github.com/FinianLandes/SpotifyEsp32), compared against upstream commit [`f5e4515`](https://github.com/FinianLandes/SpotifyEsp32/commit/f5e451527caec45b88a9173521739c2a1b6e1ee0). The changes in `src/SpotifyEsp32.cpp` and `src/SpotifyEsp32.h` focus on authentication reliability after reboot and on keeping Spotify connections certificate-verified. The rationale below is based on the implementation and its comments.

| Change | Why it was made |
| --- | --- |
| Synchronize the clock in `begin()` when it is earlier than January 1, 2025. NTP uses `pool.ntp.org`, `time.nist.gov`, and `time.google.com`, with a wait of up to 15 seconds. | After a reset, an unset clock can make valid TLS certificates appear not yet valid, preventing token refresh even when a refresh token was saved. An already initialized clock skips this step. |
| Replace the single embedded CA certificate with a public CA bundle stored in flash, described in the source as generated from certifi/Mozilla roots in ESP-IDF v5.5.4 bundle format. | Allow Spotify certificate chains to validate against a broader trust store instead of depending on the one certificate included upstream. Most of the added source lines are this bundle's byte data. |
| Attach the CA bundle before each Spotify API or token connection. | Restore verification configuration across connection teardown. The code comments identify Arduino-ESP32 3.3.8 clearing the bundle callback on `stop()` as the reason. |
| Use a separate `_auth_client` for the Vercel OAuth helper and close it before exchanging the authorization code. | The helper uses `setInsecure()`; separating the clients prevents that setting from affecting Spotify Accounts/API connections. Access-token retrieval now also depends on the authorization-code exchange succeeding. |
| Initialize `_custom_scopes` to `nullptr`. | Avoid reading an uninitialized pointer when `begin()` checks whether custom scopes were supplied, which could otherwise cause invalid memory access. |
| Check the Base64 encoder result and terminate the credential buffer using its returned `out_len`. | Handle encoding failures and avoid relying on `strlen()` to locate the end of the output buffer before explicitly terminating it. |
| Parse HTTP headers line by line until the blank line, removing the extra delimiter search after `Content-Length`. | Avoid searching past the header boundary and potentially consuming response-body data or waiting unnecessarily, which can interfere with JSON token/API responses. |
| Add clock, CA-bundle, and token-connection TLS diagnostics; stop printing the authorization code in its receipt log. | Make connection failures easier to diagnose while reducing authorization-code exposure in debug output. |

### Using the authentication changes

Connect Wi-Fi before calling `sp.begin()`. If clock synchronization times out, initialization continues and logs an error; certificate-verified connections may still fail until the clock is set. Applications can set the clock themselves before `begin()`.

Refresh-token persistence is still the application's responsibility: save the token after initial authorization and supply it after reboot, for example with `Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN);`. These changes improve the connection setup used to refresh tokens; they do not add automatic flash storage.

The OAuth helper connection still disables certificate verification and carries a sensitive one-time authorization code. Only the Spotify Accounts/API client uses the CA bundle. The bundle adds firmware flash usage, and the fork requires support for `setCACertBundle(bundle, size)` and the embedded bundle format; compatibility with every core version supported by upstream has not been established. See the tested hardware below for the devices tested by maurigno.

⚠️ **Version 4 Notice:** This release may not be backward compatible with v3.x.x
Some of the API endpoints were removed or renamed, to fully align with the new API provided by Spotify ([Spotify API update Blog](https://developer.spotify.com/blog/2026-02-06-update-on-developer-access-and-platform-security)).

## Dependencies

- [ArduinoJson](https://arduinojson.org/)  
- [WiFiClientSecure](https://github.com/espressif/arduino-esp32/tree/release/v2.x/libraries/WiFiClientSecure) *(Note: In Arduino-ESP32 v3.x, `WiFiClientSecure` is a compatibility alias for `NetworkClientSecure`. This library uses `WiFiClientSecure` to ensure full compatibility with **PlatformIO**, where v3.x support is still unavailable.)*

## Setup

 **[YouTube authentication Tutorial for v3 & v4](https://youtu.be/Yy75KzIfqi4)**

### 1. Create a Spotify Application

1. Go to the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard/applications).
2. Create a new application and copy your **Client ID** and **Client Secret**.
3. Add the following redirect URI: <https://spotifyesp32.vercel.app/api/spotify/callback>
4. Enable the **Web API** option.

### 2. Example: Login without a saved refresh token

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include "SpotifyEsp32.h"

const char* SSID = "your_ssid";
const char* PASSWORD = "your_password";
const char* CLIENT_ID = "your_client_id";
const char* CLIENT_SECRET = "your_client_secret";

// Create an instance of the Spotify class (optional: specify retry count)
Spotify sp(CLIENT_ID, CLIENT_SECRET);

void setup() {
 Serial.begin(115200);
 connect_to_wifi();

 // Optionally set custom scopes the available scopes are listed below
 // sp.set_scopes("user-read-playback-state user-modify-playback-state");

 sp.begin();
 while (!sp.is_auth()) {
     sp.handle_client(); // Required for receiving the authorization code
 }

 Serial.printf("Authenticated! Refresh token: %s\n", sp.get_user_tokens().refresh_token);
}

void loop() {
 // Your code here
}

void connect_to_wifi() {
 WiFi.begin(SSID, PASSWORD);
 Serial.print("Connecting to WiFi...");
 while (WiFi.status() != WL_CONNECTED) {
     delay(1000);
     Serial.print(".");
 }
 Serial.println("\nConnected to WiFi!");
}
```

[List of available scopes](https://developer.spotify.com/documentation/web-api/concepts/scopes)

### 3. Save the Refresh Token

After logging in via the URL shown in the Serial Monitor, your ESP32 will print a refresh token.
Copy this token and pass it as the third parameter to the constructor.

```cpp
Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN);
```

This way, you won’t have to reauthenticate each time.

### 4. ESP32-S3 example: save the refresh token in NVM (NVS)

This example is based on **maurigno's working ESP32-S3 application** and works with ESP32-S3 devices. It uses the ESP32 [Preferences library](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html) to store the refresh token in NVS, the ESP32's flash-backed nonvolatile storage. No external memory is needed.

Replace the credential placeholders and configure the Spotify redirect URI as described above. On the first boot, open the authorization URL from the Serial Monitor at **115200 baud**. The sketch saves the refresh token; later boots load it automatically. Set `FORCE_NEW_LOGIN` to `true` to clear it and authorize again, then restore `false` and upload again.

The sketch waits for Wi-Fi, NTP, and initial browser authorization. Playback information is printed when it changes. A failed saved-token request preserves the token so that a temporary connection problem does not erase the login. This adaptation adds storage checks and frees the token copies returned by the library; the adapted sketch has not been independently hardware-tested here.

The complete sketch is also available as [nvsEsp32S3.ino](examples/nvsEsp32S3/nvsEsp32S3.ino).

```cpp
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
}```

### 5. Using the Library

Each API call returns a response object containing:
`response_obj.status_code` → the HTTP status code (or -1 if the request failed before sending)
`response_obj.reply` → the JSON response as a JsonDocument
To print a response: `print_response(response_obj);`
To reduce memory usage, GET requests support **filtered responses** ([Filter tutorial](https://arduinojson.org/news/2020/03/22/version-6-15-0/)):

```cpp
JsonDocument filter;
filter["item"]["name"] = true;
response res = sp.get_current_playback(filter);
```

See the [Spotify Web API Reference](https://developer.spotify.com/documentation/web-api/reference/) for all the possible endpoints.

## Optimization Options

To reduce flash usage, disable unneeded endpoints by defining macros before including the library:

```c++
#define DISABLE_LIBRARY         //Saved items & followed artists
#define DISABLE_PLAYLISTS       //Playlist management
#define DISABLE_METADATA        //Albums, artists, search, shows, tracks
#define DISABLE_PLAYER          //Playback control
#define DISABLE_USER            //Profile & top items
#define DISABLE_SIMPLIFIED      //Helper convenience functions
```

## Helper Functions

```c++
// Current playback info
String current_track_name();
String current_track_id();
String current_device_id();
String current_artist_names();

// Versions returning pointers (e.g for as parameters for other functions)
char* current_device_id(char* device_id);
char* current_track_id(char* track_id);
char* current_track_name(char* track_name);
char* current_artist_names(char* artist_names);

// Playback and device info
bool is_playing();
bool volume_modifyable();

// URI helpers
char* convert_id_to_uri(char* id, char* type, char* uri);

// Current album artwork url
String get_current_album_image_url(int image_size_idx);
```

You can also include the namespace:

```cpp
using namespace spotify_types;
```

## Token Management

- Retrieve stored tokens (useful for saving to flash memory):

    ```cpp
    user_tokens tokens = sp.get_user_tokens();
    ```

    This contains `client_id`, `client_secret`, and `refresh_token`.

- The library automatically refreshes expired access tokens before making API requests.
You can also refresh manually:

    ```cpp
    sp.get_token();
    ```

    Returns `true` if successfull.

## Debugging

The SpotifyEsp32 library features a custom logging system, independent of `esp_log.h`, to assist with effective issue diagnosis. You can configure the logging level using the following method:

```cpp
sp.set_log_level(spotify_log_level_t spotify_log_level);
```

### Available Logging Levels

The library provides the following logging options to control output verbosity:

- `SPOTIFY_LOG_NONE`: Disables all logging output.
- `SPOTIFY_LOG_ERROR`: Captures both fatal and non-fatal errors for critical issues.
- `SPOTIFY_LOG_WARN`: Includes warnings alongside error messages.
- `SPOTIFY_LOG_INFO`: Offers additional informational messages about general operation.
- `SPOTIFY_LOG_DEBUG`: Provides detailed debug information for troubleshooting.
- `SPOTIFY_LOG_VERBOSE`: Delivers the highest level of detail with extensive logging.

### Default Setting

The default logging level is `SPOTIFY_LOG_NONE`, meaning no logs are generated unless explicitly enabled.

## Asynchronous Spotify API Calls

The library supports running Spotify API calls asynchronously using FreeRTOS. This allows your main loop to continue without waiting for a request to finish.

### Usage

Define a callback to handle the response:

```cpp
void handle_callback(response resp) {
    print_response(resp); // Handle your response e.g. print it.
}
```

Wrap your API call in a lambda and pass it to `async()` along with the callback:

```cpp
// Example: call a function with arguments
sp.async([&]() {
    return sp.get_playlist_items(0, 50, filter_doc);
}, handle_callback);

// Example: call a function with no arguments
sp.async([&]() {
    return sp.get_users_saved_albums();
}, handle_callback);
```

Notes:

- Any arguments must be captured or bound inside the lambda.
- The callback receives the response object once the async task completes.
- Each async call runs on a separate FreeRTOS task, with a stack size of 8192 bytes by default.

## Troubleshooting

- Enable debug mode using the above mentioned function `set_log_level`.
- If requests fail, inspect the returned response or Serial output.
- Test individual endpoints in the [Spotify Web API Console](https://developer.spotify.com/console/). </br>
- Still having issues? Open an issue in [this fork](https://github.com/mauringo/SpotifyEsp32_fixedAuthReboot/issues).

## Supported and Tested Devices

This version is intended for the ESP32 family. Tested by maurigno with:

- LILYGO T-Display ESP32
- ESP32-S3 devices

Other ESP32 boards may work, but are not listed as tested for this fork.
