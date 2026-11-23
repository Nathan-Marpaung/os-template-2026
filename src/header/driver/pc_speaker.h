#ifndef _PC_SPEAKER_H
#define _PC_SPEAKER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * PC Speaker driver.
 *
 * The classic PC speaker is driven by two pieces of hardware:
 *
 *   1. PIT (Programmable Interval Timer) channel 2 generates a square wave.
 *      Its frequency is PIT_BASE_FREQUENCY / divisor, so the divisor fully
 *      determines the tone pitch.
 *
 *   2. I/O port 0x61 gates that square wave to the speaker:
 *        bit 0 = speaker output gate
 *        bit 1 = speaker data enable
 *      Both bits must be set for the tone to be audible.
 *
 * The driver only programs channel 2 (channel 0 is left to the scheduler's
 * timer) and uses channel 0's running counter for millisecond-accurate delays.
 * Reading that counter through the PIT latch does not disturb channel 0 and
 * works even when CPU interrupts are disabled, so the boot jingle is safe to
 * play before the scheduler is running.
 */

/* I/O ports */
#define PC_SPEAKER_CONTROL_PORT   0x61
#define PC_SPEAKER_PIT_CHANNEL_2  0x42
#define PC_SPEAKER_PIT_COMMAND    0x43

/* Bits inside port 0x61 */
#define PC_SPEAKER_GATE_BIT       0x01u
#define PC_SPEAKER_DATA_BIT       0x02u
#define PC_SPEAKER_ENABLE_BITS    (PC_SPEAKER_GATE_BIT | PC_SPEAKER_DATA_BIT)

/* PIT channel 2 divisor is 16-bit. */
#define PC_SPEAKER_MIN_DIVISOR    1u
#define PC_SPEAKER_MAX_DIVISOR    0xFFFFu

/**
 * Prepare the speaker for playback and guarantee it starts muted.
 * Must be called after pit_initialize() so channel 0 timing is available.
 */
void pc_speaker_init(void);

/**
 * Silence the speaker immediately (clears the port 0x61 enable bits).
 */
void pc_speaker_stop(void);

/**
 * Emit a single tone.
 *
 * @param frequency_hz Tone pitch in Hz (0 silences the speaker).
 * @param duration_ms  Playback length in milliseconds.
 */
void pc_speaker_play_frequency(uint32_t frequency_hz, uint32_t duration_ms);

/**
 * Play a sequence of notes.
 *
 * @param frequencies Note pitches in Hz, one per entry.
 * @param durations_ms Playback length in ms, one per entry.
 * @param note_count   Number of entries in both arrays.
 */
void pc_speaker_play_melody(const uint16_t *frequencies,
                            const uint16_t *durations_ms,
                            uint32_t note_count);

/**
 * Short ascending jingle played once while the system boots.
 */
void pc_speaker_boot_melody(void);

/**
 * Short descending "error" bleep used by the shell when a command fails.
 */
void pc_speaker_error_beep(void);

#endif