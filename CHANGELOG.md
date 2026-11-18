# Changelog

## 0.1.0 - 2026-11-17 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Timing tables in payloads/*.ini ([animation] then time = payload, seconds or %); Payload events through NorseEvents.
- One hook on Montage_PlayInternal (AnimInstance vtable slot 95) sees every montage start; dynamic montages matched by their animation.
- Console pl.list, pl.reload, pl.log on|off; bLogMontageStarts in NorsePayload.ini.
