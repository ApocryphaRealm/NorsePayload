# NorsePayload

Timing tables on animations for the Norse overhaul (NORSE: Oath of Blood, UE4SS v4.0.0): "at this moment in this
animation, do that". When a playing montage passes a named moment, NorsePayload publishes a **Payload** event through
NorseEvents - subject the unit, asset the montage, text the payload's name, newValue the time in ms - and the overhaul's
modules act on it (open a hit window, spend stamina, allow a dodge cancel). Works on the game's own animations;
nothing in the paks changes. Survey: `RESEARCH-more-frameworks.md` (#5); modelled on Payload Interpreter and Behavior
Data Injector.

```ini
[A_GC_Dagger_Attack_01]     ; a montage or animation name, * wildcards
0.32 = HitStart             ; seconds into the montage
0.50 = HitEnd
60%  = CancelDodge          ; or a share of its length
```

Author guide: `package\payloads\README.txt`. Finding names: `bLogMontageStarts=1` (or console `pl.log on`).

## How

Montage_Play, Montage_PlayWithBlendIn and Montage_PlayWithBlendSettings - and so PlaySlotAnimationAsDynamicMontage -
all end in one virtual on the AnimInstance: vtable slot 95 (+0x2f8), Unreal's Montage_PlayInternal (read from the exe's
thunks with `NorseAddressLibrary/tools/nal_inspect.py` and `nal_callees.py`). NorsePayload hooks the function in that
slot of every AnimInstance class in play (one hook so far: the units' anim blueprints share the engine's function).
A montage with moments to come is followed with one Montage_GetPosition per frame until the last moment or the montage
stops; montages with no table entry cost nothing after their start. A dynamic montage is matched by the animation
inside it (read from its SlotAnimTracks).

## Status

0.1.0 tested in game (TEST-RESULT-0.1.0.txt): the hero's attacks fired their moments at exactly the montage times.
Not yet: enemy montages (none play in the real-time test fight yet), conditions on a moment (NorseConditions), and
the vtable slot as a Norse Address Library entry kind (checked per build). Build: `xmake f -p windows -a x64 -m release
--toolchain=msvc -y`, then `xmake` (needs `../NorsePatcher`, `../NorseManifest`, `../NorseEvents`; MinHook in
`third_party/`).

## Licence

GPL-3.0-or-later (see LICENSE). MinHook (BSD 2-Clause), compiled into the DLL, keeps its own notice in THIRD-PARTY-NOTICES.txt.
