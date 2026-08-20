#pragma once
#include <cstdint>
#include <cstddef>

      ///< set by the GPS PPS interrupt, cleared once read

// Bits in `playerEvent`. An event group is used rather than a plain flag so the
// UI thread can poll state while the player task sets it, without a lock.
// @see https://www.freertos.org/Real-time-embedded-RTOS-Event-Groups.html
#define PLAYER_PLAY                 _BV(0)  ///< a play request is pending/in progress
#define PLAYER_END                  _BV(1)  ///< asks the decode loop to stop early
#define PLAYER_RUNNING              _BV(2)  ///< the task is currently decoding
static bool playMP3(uint8_t *src, size_t src_len);