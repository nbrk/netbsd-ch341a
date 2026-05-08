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

#include <sys/module.h>

#include <dev/usb/usbdevs.h>
#include <dev/usb/usbdi.h>

/* Chip vendor-specific requests */
#define UCHEPP_REQ_GET_VERSION	 0x5f	// From uchcom(4), XXX seems to work?

static int	uchepp_match(device_t, cfdata_t, void *);
static void	uchepp_attach(device_t, device_t, void *);
static int	uchepp_detach(device_t, int);

CFATTACH_DECL_NEW(uchepp, sizeof(struct uchepp_softc),
    uchepp_match, uchepp_attach, uchepp_detach, NULL);

static const struct usb_devno uchepp_devs[] = {
        { USB_VENDOR_QINHENG, USB_PRODUCT_QINHENG_CH341_EPP},
};

/**********************************************************************************
 *
 * Autoconf interface.
 *
 *********************************************************************************/

static int
uchepp_match(device_t parent, cfdata_t match, void *aux) {
	struct usb_attach_arg *uiaa = aux;

	if (usb_lookup(uchepp_devs, uiaa->uaa_vendor, uiaa->uaa_product)) {
		return UMATCH_VENDOR_PRODUCT;
	} else {
		return UMATCH_NONE;
	}
}

static void
uchepp_attach(device_t parent, device_t self, void *aux) {
	struct usb_attach_arg *uaa = aux;
	struct uchepp_softc *sc = device_private(self);
	char *devinfop;

	/* Print the device info.  */
	aprint_naive("\n");
	aprint_normal("\n");
	devinfop = usbd_devinfo_alloc(uaa->uaa_device, 0);
	aprint_normal_dev(self, "%s\n", devinfop);
	usbd_devinfo_free(devinfop);

	/* Initialize softc.  */
	memset(sc, 0, sizeof(*sc));
	sc->sc_dev = self;
	sc->sc_udev = uaa->uaa_device;
	sc->sc_dying = false;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);

	/*
	 * Prepare the USB machinery to work.
	 */
	if (uchepp_usb_init(sc)) {
		sc->sc_dying = true;
		(void)config_detach(self, 0);
		return;
	}

	if (uchepp_usb_req_vendor_read(sc, UCHEPP_REQ_GET_VERSION, &sc->sc_version, 1)) {
		sc->sc_version = 0xff; // Unknown version
	}

#ifdef UCHEPP_DEBUG
	aprint_normal_dev(sc->sc_dev, "firmware version 0x%x\n", sc->sc_version);
#endif
	// usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->sc_udev, sc->sc_dev);

	uchepp_gpio_attach(sc);

}

static int
uchepp_detach(device_t self, int flags) {
	struct uchepp_softc *sc = device_private(self);

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_dying) {
		(void)config_detach_children(self, flags);
	}

	// usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_udev, sc->sc_dev);

	/*
	 * Tear all USB machinery down.
	 */
	uchepp_usb_fini(sc);

	mutex_exit(&sc->sc_lock);

	mutex_destroy(&sc->sc_lock);

	return 0;
}


/**********************************************************************************
 *
 * Kernel Module interface.
 *
 *********************************************************************************/

MODULE(MODULE_CLASS_DRIVER, uchepp, NULL);

#ifdef _MODULE
#include "ioconf.c"
#endif

static int
uchepp_modcmd(modcmd_t cmd, void *aux)
{
	int error = 0;

	switch (cmd) {
	case MODULE_CMD_INIT:
#ifdef _MODULE
		error = config_init_component(cfdriver_ioconf_uchepp,
		    cfattach_ioconf_uchepp, cfdata_ioconf_uchepp);
#endif
		return error;
	case MODULE_CMD_FINI:
#ifdef _MODULE
		error = config_fini_component(cfdriver_ioconf_uchepp,
		    cfattach_ioconf_uchepp, cfdata_ioconf_uchepp);
#endif
		return error;
	default:
		return ENOTTY;
	}
}
