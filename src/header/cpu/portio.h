#ifndef _PORTIO_H
#define _PORTIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * out — Send a byte to an x86 I/O port.
 * Uses the x86 `outb` instruction (Port-Mapped I/O).
 *
 * @param port The I/O port address to write to
 * @param data The byte value to send
 */
void out(uint16_t port, uint8_t data);

/**
 * in — Read a byte from an x86 I/O port.
 * Uses the x86 `inb` instruction (Port-Mapped I/O).
 *
 * @param port The I/O port address to read from
 * @return The byte received from the port
 */
uint8_t in(uint16_t port);

/** out16:
 *  Sends the given 16-bit data to the given I/O port
 *
 *  @param port The I/O port to send the data to
 *  @param data The 16-bit data to send to the I/O port
 */
void out16(uint16_t port, uint16_t data);

/** in16:
 *  Read 16-bit data from the given I/O port
 *
 *  @param port The I/O port to request the data
 *  @return Recieved 16-bit data from the corresponding I/O port
 */
uint16_t in16(uint16_t port);

#endif
