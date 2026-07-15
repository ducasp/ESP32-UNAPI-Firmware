# Sample Certs

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/R6R2BRGX6)

Sample certificate bundle to use with ESP32 firmware. This has been generated based on:

- Mozilla May 2026 list
- Google Trust certificates not in that list
- Globalsign Root R1 certificate to overcome mBED TLS cross signed certificates misbehavior

*** NOTE ***
Google and some other hosts have certificates cross-signed with old and considered unreliable certificates from globalsign, they do this
for legacy devices that have not been updated, and most modern stuff understand the cross-signing and bypass the old cert and go to the
new cert, unfortunately mBED TLS, that is the base for this development, doesn't work well. If you don't like the idea of having an old
certificate loaded, create your own bundle and be aware that until a fix from ESPRESSIF and mBED comes you won't be able to connect to
such sites unless using unsecure connection.