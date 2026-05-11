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

#define UCHEPP_NUM_D_BIDIR_LINES 6 // only D[5:0] are dedicated bidir

/* Chip GPIO/UIO commands (bulk) */
#define UCHEPP_CMD_UIO_STM_START	0xab // UIO start command stream
#define UCHEPP_CMD_UIO_STM_END	0x20 // UIO end command stream
#define UCHEPP_CMD_UIO_IN	0x00 // UIO interface IN  command (D0~D7)
#define UCHEPP_CMD_UIO_OUT	0x80 // UIO interface OUT command (D0~D5)
#define UCHEPP_CMD_UIO_DIR	0x40 // UIO interface DIR command (D0~D5)
#define UCHEPP_CMD_UIO_US	0xc0 // UIO interface US  command (delay?)
#define UCHEPP_CMD_PARA_STATUS	0xa0 // ??? extended pin status

/* GPIO related protocol constants (XXX not used?) */
#define UCHEPP_PIN_DIR_IN	0
#define UCHEPP_PIN_DIR_OUT	1
#define UCHEPP_PIN_LEVEL_LOW	0
#define UCHEPP_PIN_LEVEL_HIGH	1

static uint32_t const pin_caps[UCHEPP_NUM_GPIO_LINES] = {
	GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_PUSHPULL,
	GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_PUSHPULL,
	GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_PUSHPULL,
	GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_PUSHPULL,
	GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_PUSHPULL,
	GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_PUSHPULL,

	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
	GPIO_PIN_INPUT,
};

static char const *const pin_names[UCHEPP_NUM_GPIO_LINES] = {
	"D0_CS0",
	"D1_CS1",
	"D2_CS2",
	"D3_SCK_DCK",
	"D4_DOUT2_CS3",
	"D5_MOSI_DOUT_SDO",

	"D6_DIN2",
	"D7_MISO_DIN",
	"ERR",
	"PEMP",
	"INT",
	"SLCT",
	"L12_UNKN",
	"WT",
	"DS",
	"AS",
	"SCL",
	"SDA",
	"SCK",
};

static void set_pins_dirs_outs(struct uchepp_softc *, uint8_t, uint8_t);
static int pin_read(void *, int);
static void pin_write(void *, int, int);
static void pin_ctl(void *, int, int);

static void
set_pins_dirs_outs(struct uchepp_softc *sc, uint8_t dirmask, uint8_t outmask)
{
	uint8_t buf[4];

	// ensure only D[5:0] bits are used
	dirmask &= ~0xc0;
	outmask &= ~0xc0;

	buf[0] = UCHEPP_CMD_UIO_STM_START;
	buf[1] = UCHEPP_CMD_UIO_DIR | dirmask;
	buf[2] = UCHEPP_CMD_UIO_OUT | outmask;
	buf[3] = UCHEPP_CMD_UIO_STM_END;

	if (uchepp_usb_bulk_send(sc, buf, 4)) {
		aprint_error_dev(sc->sc_dev,
		    "failed to set pins dirs and outs\n");
		// NOTE: pins' softstate is left intact on failure
		return;
	}
	// update our software direction and out-value state
	sc->sc_gpio_hw_dir_mask = dirmask;
	sc->sc_gpio_hw_out_mask = outmask;

#ifdef UCHEPP_DEBUG
	aprint_normal_dev(sc->sc_dev,
	    "updated dirs and outs with dirs 0x%.2x, outs 0x%.2x\n",
	    sc->sc_gpio_hw_dir_mask, sc->sc_gpio_hw_out_mask);
#endif
}

static void
setup_gpio_layer(struct uchepp_softc *sc)
{
	int i;

	// NOTE: configure all bi-dir lines to a known initial direction
	set_pins_dirs_outs(sc, 0x00, 0x00);	// all inputs

	// Software pins representation
	for (i = 0; i < UCHEPP_NUM_GPIO_LINES; ++i) {
		gpio_pin_t *const pin = &sc->sc_gpio_pins[i];
		pin->pin_num = i;
		pin->pin_caps = pin_caps[i];
		pin->pin_intrcaps = 0; // TODO: only the INT# line supports intrs
		strncpy(pin->pin_defname, pin_names[i],
		    strlen(pin_names[i]) + 1);
		pin->pin_flags = GPIO_PIN_INPUT;	// initial dir was set earlier
	}

	// Our controller ops (chipset tag) passed to the gpio layer
	sc->sc_gpio_gc.gp_pin_read = pin_read;
	sc->sc_gpio_gc.gp_pin_write = pin_write;
	sc->sc_gpio_gc.gp_pin_ctl = pin_ctl;
	sc->sc_gpio_gc.gp_cookie = sc;
}

static int
pin_read(void *arg, int pin)
{
	struct uchepp_softc *sc = arg;
	int ret;

	mutex_enter(&sc->sc_lock);

#ifdef UCHEPP_DEBUG
	aprint_normal_dev(sc->sc_dev, "%s for pin %d\n", __FUNCTION__, pin);
#endif

	if (pin < UCHEPP_NUM_D_BIDIR_LINES
	    && (sc->sc_gpio_hw_dir_mask & (1 << pin))) {
		/*
		 * Bi-directional pin currently configured as output. Just return
		 * the in-core (cached) value.
		 */
		ret = (sc->sc_gpio_hw_out_mask & (1 << pin)) ? GPIO_PIN_HIGH
		    : GPIO_PIN_LOW;
	} else {
		/*
		 * Bidir pin currently set to input or an input-only shared
		 * function pin. Query the actual (volatile) value from the hw.
		 */
		uint8_t buf[6];
		int const pinbyteidx = pin / 8;
		uint8_t const pinbyteoff = pin % 8;
		int value;

		buf[0] = UCHEPP_CMD_PARA_STATUS;
		if (uchepp_usb_bulk_send(sc, buf, 1)) {
			aprint_error_dev(sc->sc_dev,
			    "failed to send cmd 0x%.2x\n", buf[0]);
			ret = -1;
		}

		if (uchepp_usb_bulk_recv(sc, buf, 6)) {
			aprint_error_dev(sc->sc_dev,
			    "failed to receive pins statuses\n");
			ret = -1;
		}

		value = (buf[pinbyteidx] & (1 << pinbyteoff)) ? GPIO_PIN_HIGH
		    : GPIO_PIN_LOW;
#ifdef UCHEPP_DEBUG
		aprint_normal_dev(sc->sc_dev,
		    "read pin %d in buf[%d] offset %d as value %d (buf[3:0]: 0x%.2x 0x%.2x 0x%.2x 0x%.2x)\n",
		    pin, pinbyteidx, pinbyteoff, value, buf[3], buf[2], buf[1],
		    buf[0]);
#endif

		ret = value;
	}

	mutex_exit(&sc->sc_lock);
	return ret;
}

static void
pin_write(void *arg, int pin, int value)
{
	struct uchepp_softc *sc = arg;

	mutex_enter(&sc->sc_lock);
#ifdef UCHEPP_DEBUG
	aprint_normal_dev(sc->sc_dev, "%s for pin %d, value %d\n",
	    __FUNCTION__, pin, value);
#endif

	/* Only D[5:0] may be in the output mode */
	if (pin < UCHEPP_NUM_D_BIDIR_LINES) {
		if (!(sc->sc_gpio_hw_dir_mask & (1 << pin))) {
			aprint_normal_dev(sc->sc_dev,
			    "ignoring pin_write on input-set pin %d\n", pin);
		} else {
			switch (value) {
			case GPIO_PIN_LOW:
				set_pins_dirs_outs(sc, sc->sc_gpio_hw_dir_mask,
				    sc->sc_gpio_hw_out_mask & ~(1 << pin));
#ifdef UCHEPP_DEBUG
				aprint_normal_dev(sc->sc_dev,
				    "output pin %d set to LOW\n", pin);
#endif
				break;
			case GPIO_PIN_HIGH:
				set_pins_dirs_outs(sc, sc->sc_gpio_hw_dir_mask,
				    sc->sc_gpio_hw_out_mask | (1 << pin));
#ifdef UCHEPP_DEBUG
				aprint_normal_dev(sc->sc_dev,
				    "output pin %d set to HIGH\n", pin);
#endif
				break;
			default:
				aprint_normal_dev(sc->sc_dev,
				    "ignoring pin_write of unknown value 0x%.8x on pin %d\n",
				    value, pin);
			}
		}
	} else {
		aprint_normal_dev(sc->sc_dev,
		    "ignoring pin_write on input-only pin %d\n", pin);
	}

	mutex_exit(&sc->sc_lock);
}

static void
pin_ctl(void *arg, int pin, int flags)
{
	struct uchepp_softc *sc = arg;
	mutex_enter(&sc->sc_lock);

#ifdef UCHEPP_DEBUG
	aprint_normal_dev(sc->sc_dev, "%s for pin %d, flags 0x%.8x\n",
	    __FUNCTION__, pin, flags);
#endif

	/* Only D[5:0] are allowed for real manipulation */
	if (pin < UCHEPP_NUM_D_BIDIR_LINES) {
		uint8_t hw_dir_mask = sc->sc_gpio_hw_dir_mask;

		if (flags & GPIO_PIN_INPUT) {
			hw_dir_mask &= ~(1 << pin);	// set pin to IN
			set_pins_dirs_outs(sc, hw_dir_mask,
			    sc->sc_gpio_hw_out_mask);
#ifdef UCHEPP_DEBUG
			aprint_normal_dev(sc->sc_dev,
			    "set pin %d mode to INPUT\n", pin);
#endif
		} else if ((flags & GPIO_PIN_OUTPUT)
		    || (flags & GPIO_PIN_PUSHPULL)) {
			hw_dir_mask |= (1 << pin);	// set pin to OUT
			/*
			 * Now, with this chip we can't just set the direction of
			 * the pin to OUT, we must also set its (current) output
			 * value. Set it to GPIO_PIN_HIGH as initial output value.
			 */
			set_pins_dirs_outs(sc, hw_dir_mask,
			    sc->sc_gpio_hw_out_mask | (1 << pin));
#ifdef UCHEPP_DEBUG
			aprint_normal_dev(sc->sc_dev,
			    "set pin %d mode to OUTPUT\n", pin);
#endif
		} else {
			aprint_normal_dev(sc->sc_dev,
			    "ignoring unsupported flags %d on pin %d\n",
			    flags, pin);
		}
	} else {
#ifdef UCHEPP_DEBUG
		aprint_normal_dev(sc->sc_dev,
		    "ignoring pin_ctl on input-only pin %d\n", pin);
#endif
	}

	mutex_exit(&sc->sc_lock);
}

void
uchepp_gpio_attach(struct uchepp_softc *sc)
{
	struct gpiobus_attach_args gba;

	setup_gpio_layer(sc);

	gba.gba_gc = &sc->sc_gpio_gc;
	gba.gba_npins = UCHEPP_NUM_GPIO_LINES;
	gba.gba_pins = sc->sc_gpio_pins;

	sc->sc_gpio_dev = config_found(sc->sc_dev, &gba, gpiobus_print,
	    CFARGS(.iattr = "gpiobus"));
}
