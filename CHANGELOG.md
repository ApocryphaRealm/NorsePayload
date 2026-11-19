# Changelog

## 1.0.0 - 2026-11-18 - untested

### Added
- First release. It contains everything built and tested in 0.1.0, listed here and kept as history below:
- Timing tables in payloads/*.ini ([animation] then time = payload, seconds or %); Payload events through NorseEvents.
- One hook on Montage_PlayInternal (AnimInstance vtable slot 95) sees every montage start; dynamic montages matched by their animation.
- Console pl.list, pl.reload, pl.log on|off; bLogMontageStarts in NorsePayload.ini.
- main.dll carries a Windows version resource (file and product version 1.0.0, shown in its Properties > Details).

### Fixed
- Payload moments follow the montage that actually started (GetCurrentActiveMontage), so an animation replaced by NorseAnimSwap gets its own payloads.

## 0.1.0 - 2026-11-17 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Timing tables in payloads/*.ini ([animation] then time = payload, seconds or %); Payload events through NorseEvents.
- One hook on Montage_PlayInternal (AnimInstance vtable slot 95) sees every montage start; dynamic montages matched by their animation.
- Console pl.list, pl.reload, pl.log on|off; bLogMontageStarts in NorsePayload.ini.

### Fixed (2026-11-18, refinement)
- Payload moments follow the montage that actually started (GetCurrentActiveMontage), so an animation replaced by NorseAnimSwap gets its own payloads.
