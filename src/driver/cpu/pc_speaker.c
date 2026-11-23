#include "header/driver/pc_speaker.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"   /* PIT_BASE_FREQUENCY / PIT_FREQUENCY / PIT_CHANNEL_0 / PIT_COMMAND */

/*
 * Number of PIT channel-0 counts in one millisecond.
 *
 * pit_initialize() programs channel 0 to PIT_FREQUENCY (1000 Hz), so a full
 * channel-0 period is (PIT_BASE_FREQUENCY / PIT_FREQUENCY) counts and equals
 * 1 ms.
 */
#define PC_SPEAKER_COUNTS_PER_MS \
    ((uint16_t)(PIT_BASE_FREQUENCY / PIT_FREQUENCY))

/* PIT channel 2 command: channel 2, lobyte/hibyte, mode 3 (square wave), binary */
#define PC_SPEAKER_CHANNEL_2_MODE_3   0xB6
/* Latch command for channel 0 (channel select = 00, access = 00) */
#define PC_SPEAKER_CHANNEL_0_LATCH    0x00

static void speaker_write_control(uint8_t value) {
    out(PC_SPEAKER_CONTROL_PORT, value);
}

static void speaker_enable(void) {
    uint8_t state = in(PC_SPEAKER_CONTROL_PORT);
    speaker_write_control((uint8_t)(state | PC_SPEAKER_ENABLE_BITS));
}

static void speaker_program_channel_2(uint32_t frequency_hz) {
    uint32_t divisor = PIT_BASE_FREQUENCY / frequency_hz;
    if (divisor < PC_SPEAKER_MIN_DIVISOR) {
        divisor = PC_SPEAKER_MIN_DIVISOR;
    }
    if (divisor > PC_SPEAKER_MAX_DIVISOR) {
        divisor = PC_SPEAKER_MAX_DIVISOR;
    }

    out(PC_SPEAKER_PIT_COMMAND, PC_SPEAKER_CHANNEL_2_MODE_3);
    out(PC_SPEAKER_PIT_CHANNEL_2, (uint8_t)(divisor & 0xFF));
    out(PC_SPEAKER_PIT_CHANNEL_2, (uint8_t)((divisor >> 8) & 0xFF));
}

/* Read the current channel-0 counter through the PIT latch (does not stop it). */
static uint16_t speaker_read_channel0(void) {
    out(PIT_COMMAND, PC_SPEAKER_CHANNEL_0_LATCH);
    uint8_t low = in(PIT_CHANNEL_0);
    uint8_t high = in(PIT_CHANNEL_0);
    return (uint16_t)(((uint16_t)high << 8) | (uint16_t)low);
}

/*
 * Busy-wait for `duration_ms` milliseconds using the channel-0 counter.
 *
 * Channel 0 counts down from PC_SPEAKER_COUNTS_PER_MS to 0 at ~1 kHz. We sample
 * it repeatedly and accumulate the elapsed counts, handling the wrap at 0.
 * This needs no interrupts, so it works during boot (IF = 0) and inside a
 * syscall handler alike.
 */
static void speaker_delay_ms(uint32_t duration_ms) {
    if (duration_ms == 0u) {
        return;
    }

    uint32_t target = duration_ms * (uint32_t)PC_SPEAKER_COUNTS_PER_MS;
    uint32_t elapsed = 0u;
    uint16_t previous = speaker_read_channel0();

    while (elapsed < target) {
        uint16_t current = speaker_read_channel0();
        uint16_t delta;
        if (current <= previous) {
            delta = (uint16_t)(previous - current);
        } else {
            delta = (uint16_t)(previous + PC_SPEAKER_COUNTS_PER_MS - current);
        }
        elapsed += delta;
        previous = current;
    }
}

void pc_speaker_init(void) {
    /* Leave the PIT channel 2 in a known state and start muted. */
    speaker_write_control(
        (uint8_t)(in(PC_SPEAKER_CONTROL_PORT) & ~PC_SPEAKER_ENABLE_BITS)
    );
}

void pc_speaker_stop(void) {
    speaker_write_control(
        (uint8_t)(in(PC_SPEAKER_CONTROL_PORT) & ~PC_SPEAKER_ENABLE_BITS)
    );
}

void pc_speaker_play_frequency(uint32_t frequency_hz, uint32_t duration_ms) {
    if (frequency_hz == 0u || duration_ms == 0u) {
        pc_speaker_stop();
        return;
    }

    speaker_program_channel_2(frequency_hz);
    speaker_enable();
    speaker_delay_ms(duration_ms);
    pc_speaker_stop();
}

void pc_speaker_play_melody(const uint16_t *frequencies,
                            const uint16_t *durations_ms,
                            uint32_t note_count) {
    if (frequencies == 0 || durations_ms == 0) {
        return;
    }
    for (uint32_t i = 0; i < note_count; ++i) {
        if (frequencies[i] == 0u) {
            speaker_delay_ms(durations_ms[i]);
        } else {
            pc_speaker_play_frequency(frequencies[i], durations_ms[i]);
        }
    }
}

void pc_speaker_boot_melody(void) {
    /* C5 - E5 - G5 - C6: a short "power on" jingle. */
    static const uint16_t notes[] = { 523u, 659u, 784u, 1047u };
    static const uint16_t durations[] = { 120u, 120u, 120u, 260u };
    pc_speaker_play_melody(notes, durations, 4u);
}

void pc_speaker_error_beep(void) {
    /* Two descending low notes: an unmistakable "error" buzz. */
    static const uint16_t notes[] = { 220u, 165u };
    static const uint16_t durations[] = { 90u, 150u };
    pc_speaker_play_melody(notes, durations, 2u);
}