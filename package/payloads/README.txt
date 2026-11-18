NorsePayload timing tables
==========================

Every *.ini file here names moments in animations. When a playing montage passes one, NorsePayload sends a Payload
event through NorseEvents (the unit, the montage, the payload's name, the time) - other parts of the overhaul act on it:
open a hit window, spend stamina, allow a dodge to cancel. Works on the game's own animations; nothing in the game's
files changes.

    [Drengr_Attack_Light_01]     ; a montage or animation name; * wildcards
    0.32 = HitStart              ; seconds into the montage
    0.50 = HitEnd
    60%  = CancelDodge           ; or a share of the montage's length

A montage made from a single animation is matched by that animation's name too. Finding names: set
bLogMontageStarts=1 in NorsePayload.ini (or console pl.log on) and every montage that starts is logged.
Console: pl.list, pl.reload, pl.log on|off.
