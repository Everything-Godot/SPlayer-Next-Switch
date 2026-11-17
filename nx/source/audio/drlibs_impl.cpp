// SPDX-License-Identifier: AGPL-3.0
// Single translation unit providing the dr_libs decoder implementations.
//
// IMPORTANT: never define DR_MP3_NO_STDIO / DR_FLAC_NO_STDIO /
// DR_WAV_NO_STDIO here — those are checked with #ifndef, so *defining* them
// (even to 0) disables drmp3_init_file() and friends, which we rely on.
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION

#include "dr_libs/dr_flac.h"
#include "dr_libs/dr_mp3.h"
#include "dr_libs/dr_wav.h"