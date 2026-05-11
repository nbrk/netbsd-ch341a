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

#include <sys/types.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <sys/gpio.h>
#include <dev/gpio/gpiovar.h>
#include <dev/i2c/i2cvar.h>

#define UCHEPP_DEBUG /* XXX */

#define UCHEPP_NUM_GPIO_LINES 19

struct uchepp_softc
{
	device_t sc_dev;
	struct usbd_device *sc_udev;

	bool sc_dying;
	kmutex_t sc_lock;
	uint8_t sc_version;

	/* Data bulk-in, bulk-out, interrupt endpoints */
	struct usbd_pipe *sc_bin_pipe;
	struct usbd_pipe *sc_bout_pipe;
	struct usbd_pipe *sc_intr_pipe;
	uint8_t sc_bin_pipe_maxsize;
	uint8_t sc_bout_pipe_maxsize;
	uint8_t sc_intr_pipe_maxsize;

	/* Preallocated DMA buffers and data xfers */
	struct usbd_xfer *sc_bin_xfer;
	struct usbd_xfer *sc_bout_xfer;
	struct usbd_xfer *sc_intr_xfer;

	/* GPIO functionality, pins softstate (hw doesn't maintain dirs config) */
	struct gpio_chipset_tag sc_gpio_gc;
	gpio_pin_t sc_gpio_pins[UCHEPP_NUM_GPIO_LINES];
	device_t sc_gpio_dev;
	uint8_t sc_gpio_hw_dir_mask; // dir settings for D[5:0]
	uint8_t sc_gpio_hw_out_mask; // val settings for out pins

};

int uchepp_usb_bulk_send(struct uchepp_softc *, void *, size_t);
int uchepp_usb_bulk_recv(struct uchepp_softc *, void *, size_t);

void uchepp_gpio_attach(struct uchepp_softc *);
void uchepp_iic_attach(struct uchepp_softc *);
