/*-
 * Copyright (c) 2026 Nikolay Burkov <nbrk@linklevel.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <dev/usb/ucheppvar.h>

/* Chip I2C commands (bulk) */
#define UCHEPP_CMD_I2C_STM_START	0xaa	// I2C start command stream
#define UCHEPP_CMD_I2C_STM_END	0x00	// I2C end command stream
#define UCHEPP_CMD_I2C_STA	0x74	// I2C interface START condition
#define UCHEPP_CMD_I2C_STO	0x75	// I2C interface STOP condition
#define UCHEPP_CMD_I2C_OUT	0x80	// I2C interface write
#define UCHEPP_CMD_I2C_IN	0xc0	// I2C interface read
#define UCHEPP_CMD_I2C_SET	0x60	// I2C interface set speed


/* I2C related protocol constants */
#define UCHEPP_I2C_SPEED_20KHZ	0
#define UCHEPP_I2C_SPEED_100KHZ	1
#define UCHEPP_I2C_SPEED_400KHZ	2
#define UCHEPP_I2C_SPEED_750KHZ	3



void
uchepp_iic_attach(struct uchepp_softc *sc)
{
	// TODO
}
