# Macros Bridge

Connects Geometry Dash to the [Macros](https://github.com/EthanRStokes/Macros) app over a
local TCP connection, so recording or playback is synced to exactly when a level
attempt starts and ends instead of relying on a manually-pressed hotkey.

On every attempt start (entering a level, or retrying after death) it tells Macros
to either start recording or run the current macro. On every attempt end (death,
retry, or completion) it can optionally tell Macros to stop, so input doesn't bleed
into the next attempt.

Configure the action and port to match your Macros app setup in the mod's settings.
