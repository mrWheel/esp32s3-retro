# Third-party provenance and lifecycle review

| Dependency | Exact version / upstream commit | License | Reason / local modifications |
| --- | --- | --- | --- |
| ESP-IDF | v6.0.2 | Primarily Apache-2.0; see upstream component licenses | Required native framework; not vendored or modified |
| floooh/chips `m6502.h` | `ee88c35ad6427341aa6999c3b07233e1f8bd2396` | zlib/libpng | NMOS 6502 cycle core for Apple II; unmodified source and license under `components/apple2Core/third_party/chips/` |
| michmich/esp-idf-wifi-provisioner | 0.4.0 / 2c7aaa6aa0e319a429186ee470e137b8579f632f | GPL-3.0-or-later | Mandatory connection/provisioning component; unmodified registry source bundled |
| joltwallet/littlefs | 1.20.3 / 8274371dc5912196f66ac3e71dbb6291760cb8b0 | MIT wrapper; bundled LittleFS BSD-3-Clause | LittleFS VFS and image generation; unmodified registry source bundled |
| littlefs-python | 0.15.0 | See its upstream distribution license | Build-only dependency pinned by LittleFS image-building-requirements.txt; downloaded during build, not bundled |

Primary references:

- https://github.com/espressif/esp-idf/tree/v6.0.2
- https://github.com/floooh/chips/tree/ee88c35ad6427341aa6999c3b07233e1f8bd2396
- https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/
- https://components.espressif.com/components/michmich/esp-idf-wifi-provisioner/versions/0.4.0/readme
- https://github.com/MichMich/esp-idf-wifi-provisioner/tree/2c7aaa6aa0e319a429186ee470e137b8579f632f
- https://components.espressif.com/components/joltwallet/littlefs/versions/1.20.3
- https://github.com/joltwallet/esp_littlefs/tree/8274371dc5912196f66ac3e71dbb6291760cb8b0
- https://github.com/jrast/littlefs-python

The complete runtime dependency source and license files are under `managed_components/`. The lock records registry content hashes. No third-party emulator, ROM or OS image was downloaded.

## Provisioner 0.4.0 source observations

`wifi_prov_start` can block while stored credentials are tried, so it runs in a separate worker. Its captive portal uses its own HTTP/DNS services. After successful portal provisioning, `on_credentials_set` stops those services and switches to STA mode. Our separate file server uses different HTTP and control ports anyway.

`wifi_prov_is_connected` is a cached state; it is not reset on all later disconnections. The host additionally observes IDF IP/disconnect events so it does not keep offering files based on that cached flag alone. Observation is not a second connection manager.

`wifi_sta_connect` unregisters its temporary WiFi/IP handlers when its initial attempt returns. Version 0.4.0 therefore does not guarantee automatic recovery after a later connection loss. A reset is the documented retry path rather than an unsupported custom reconnection implementation.

The AP path can retain the same default STA netif in both the AP module and the top-level provisioner. Calling the upstream stop lifecycle repeatedly after portal success warrants hardware regression testing (possible duplicate STA cleanup). HOST-M1 avoids that path by starting the component once per boot and retaining WiFi, as the specification explicitly permits. It always stops its own application HTTP server on ENTER. No upstream source was patched.

The provisioner uses some fatal `ESP_ERROR_CHECK` calls internally and may erase its NVS partition on certain initialization/version failures. Host-level errors are recoverable, but this package does not claim to eliminate upstream fatal paths without modifying the mandatory dependency. These behaviors must be included in hardware acceptance.

## Licensing

The bundled WiFi component is GPL-3.0-or-later. Preserve its license/source notices and assess the combined firmware distribution under that license. This delivery does not invent an ownership/license grant for user specifications or future historical software. No unrelated source license has been applied to the user's material. See upstream license files for exact terms.
