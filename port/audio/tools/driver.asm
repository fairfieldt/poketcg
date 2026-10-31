; Offline GBS harness. Original audio engine and sequences are included verbatim.
INCLUDE "macros.asm"
INCLUDE "constants.asm"

SECTION "GBS Harness", ROM0[$0400]
GBSInit::
    push af
    ld a, BANK("Audio 1")
    ld [rROMB], a
    ldh [hBankROM], a
    call _SetupSound
    pop af
    cp NUM_SONGS
    jr nc, .sfx
    call _PlaySong
    ret
.sfx
    sub NUM_SONGS
    call _PlaySFX
    ret
GBSPlay::
    ld a, [wCurSongBank]
    ld [rROMB], a
    ldh [hBankROM], a
    call SoundTimerHandler
    ret

INCLUDE "port/audio/build/audio_callback.asm"

SECTION "Audio 1", ROMX, BANK[1]
INCLUDE "audio/music1.asm"
SECTION "Audio 2", ROMX, BANK[2]
INCLUDE "audio/music2.asm"
SECTION "Audio 3", ROMX, BANK[3]
INCLUDE "audio/sfx.asm"
