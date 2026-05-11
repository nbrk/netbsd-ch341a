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
#include <sys/kmem.h>

#include <dev/usb/usbdevs.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>	// XXX
#include <dev/usb/usbdi_util.h>

#define	UCHEPP_USB_CONFIGURATION_INDEX	0
#define	UCHEPP_USB_INTERFACE_INDEX	0

#define UCHEPP_CTRL_IN_BUF_SIZE 8

#define UCHEPP_REQ_GET_VERSION 0x5f

struct uchepp_usb_send_async_ctx {
	struct uchepp_softc *sc;
	void *buf;
};

static int uchepp_usb_find_connect_endpoints(struct uchepp_softc *sc);
static int uchepp_usb_init(struct uchepp_softc *);
static void uchepp_usb_fini(struct uchepp_softc *);
static int uchepp_usb_req_vendor_read(struct uchepp_softc *, uint8_t, void *,
    size_t);

static int uchepp_match(device_t, cfdata_t, void *);
static void uchepp_attach(device_t, device_t, void *);
static int uchepp_detach(device_t, int);

CFATTACH_DECL_NEW(uchepp, sizeof(struct uchepp_softc), uchepp_match,
    uchepp_attach, uchepp_detach, NULL);

static const struct usb_devno uchepp_devs[] = {
	{USB_VENDOR_QINHENG, USB_PRODUCT_QINHENG_CH341_EPP},
};

static int
uchepp_usb_find_connect_endpoints(struct uchepp_softc *sc)
{
	usbd_status err;
	struct usbd_interface *iface;
	int i;
	uint8_t numeps;
	uint8_t epaddrs[3];

	err =
	    usbd_device2interface_handle(sc->sc_udev,
	    UCHEPP_USB_INTERFACE_INDEX, &iface);
	if (err) {
		aprint_error_dev(sc->sc_dev,
		    "failed to get interface handle: %s\n", usbd_errstr(err));
		return -1;
	}

	err = usbd_endpoint_count(iface, &numeps);
	if (err) {
		aprint_error_dev(sc->sc_dev,
		    "failed to get endpoint count: %s\n", usbd_errstr(err));
		return -1;
	}

	bzero(epaddrs, sizeof epaddrs);

	// iterate over all endpoints, grab first bulk in/out, and open pipes
	for (i = 0; i < numeps; ++i) {
		usb_endpoint_descriptor_t const *const epdesc =
		    usbd_interface2endpoint_descriptor(iface, i);
		if (!epdesc)
			continue;

		if ((UE_GET_XFERTYPE(epdesc->bmAttributes) == UE_BULK) &&
		    (UE_GET_DIR(epdesc->bEndpointAddress) == UE_DIR_IN) &&
		    !sc->sc_bin_pipe) {
			sc->sc_bulk_pipe_maxsize = UGETW(epdesc->wMaxPacketSize);
			err =
			    usbd_open_pipe(iface, epdesc->bEndpointAddress,
			    USBD_EXCLUSIVE_USE | USBD_MPSAFE, &sc->sc_bin_pipe);
			if (err) {
				aprint_error_dev(sc->sc_dev,
				    "failed to open bulk-in pipe: %s\n",
				    usbd_errstr(err));
				return -1;
			}
			epaddrs[0] = epdesc->bEndpointAddress;
		} else if ((UE_GET_XFERTYPE(epdesc->bmAttributes) == UE_BULK)
		    && (UE_GET_DIR(epdesc->bEndpointAddress) == UE_DIR_OUT)
		    && !sc->sc_bout_pipe) { sc->sc_bulk_pipe_maxsize =
			    UGETW(epdesc->wMaxPacketSize);
			err =
			    usbd_open_pipe(iface, epdesc->bEndpointAddress,
			    USBD_EXCLUSIVE_USE | USBD_MPSAFE, &sc->sc_bout_pipe);
			if (err) {
				aprint_error_dev(sc->sc_dev,
				    "failed to open bulk-out pipe: %s\n",
				    usbd_errstr(err));
				return -1;
			}
			epaddrs[1] = epdesc->bEndpointAddress;
		} else if ((UE_GET_XFERTYPE(epdesc->bmAttributes) ==
			UE_INTERRUPT)
		    && (UE_GET_DIR(epdesc->bEndpointAddress) == UE_DIR_IN)
		    && !sc->sc_intr_pipe) {
			sc->sc_intr_pipe_maxsize =
			    UGETW(epdesc->wMaxPacketSize);

			// TODO: interrupts ...
			// err = usbd_open_pipe_intr(iface, epdesc->bEndpointAddress,
			//         USBD_EXCLUSIVE_USE | USBD_MPSAFE,
			//         &sc->sc_intr_pipe, sc,
			//         usbd_get_buffer(sc->sc_intr_xfer),
			//         ... );
			err = usbd_open_pipe(iface, epdesc->bEndpointAddress,
			    USBD_EXCLUSIVE_USE | USBD_MPSAFE,
			    &sc->sc_intr_pipe);
			if (err) {
				aprint_error_dev(sc->sc_dev,
				    "failed to open intr-in pipe: %s\n",
				    usbd_errstr(err));
				return -1;
			}
			epaddrs[2] = epdesc->bEndpointAddress;
		}
	}

	if (!sc->sc_bin_pipe || !sc->sc_bout_pipe || !sc->sc_intr_pipe) {
		if (!sc->sc_bin_pipe)
			aprint_error_dev(sc->sc_dev, "no bulk-in ep found\n");
		if (!sc->sc_bout_pipe)
			aprint_error_dev(sc->sc_dev, "no bulk-out ep found\n");
		if (!sc->sc_intr_pipe)
			aprint_error_dev(sc->sc_dev, "no intr-in ep found\n");

		return -1;
	}

#ifdef UCHEPP_DEBUG
	aprint_normal_dev(sc->sc_dev,
	    "bulk-in 0x%.2x(%d), bulk-out 0x%.2x(%d), interrupt 0x%.2x(%d)\n",
	    epaddrs[0], sc->sc_bulk_pipe_maxsize, epaddrs[1],
	    sc->sc_bulk_pipe_maxsize, epaddrs[2], sc->sc_intr_pipe_maxsize);
#endif

	return 0;
}

int
uchepp_usb_init(struct uchepp_softc *sc)
{
	usbd_status err;

	// Configure the device. Very important.
	err =
	    usbd_set_config_index(sc->sc_udev, UCHEPP_USB_CONFIGURATION_INDEX,
	    1);
	if (err) {
		aprint_error_dev(sc->sc_dev,
		    "failed to set configuration: %s\n", usbd_errstr(err));
		return -1;
	}

	// Find all endpoints, query data sizes, open pipes
	if (uchepp_usb_find_connect_endpoints(sc)) {
		aprint_error_dev(sc->sc_dev,
		    "failed to find or connect endpoints\n");
		return -1;
	}

	// create transfers and pre-allocate buffers for sync operations
	if (usbd_create_xfer(sc->sc_bin_pipe, sc->sc_bulk_pipe_maxsize, 0, 0,
		&sc->sc_bin_xfer)) {
		aprint_error_dev(sc->sc_dev, "failed to create data-in xfer\n");
		return -1;
	}
	if (usbd_create_xfer(sc->sc_bout_pipe, sc->sc_bulk_pipe_maxsize, 0, 0,
	        &sc->sc_bout_sync_xfer)) {
		aprint_error_dev(sc->sc_dev, "failed to create data-out xfer\n");
		return -1;
	}
	if (usbd_create_xfer(sc->sc_intr_pipe, sc->sc_intr_pipe_maxsize, 0, 0,
		&sc->sc_intr_xfer)) {
		aprint_error_dev(sc->sc_dev, "failed to create intr-in xfer\n");
		return -1;
	}
	// create dedicated xfer for async writes; do not alloc any static buf
	if (usbd_create_xfer(sc->sc_bout_pipe, sc->sc_bulk_pipe_maxsize, 0, 0,
	        &sc->sc_bout_async_xfer)) {
		aprint_error_dev(sc->sc_dev, "failed to create async data-out xfer\n");
		return -1;
	}

	return 0;
}

void
uchepp_usb_fini(struct uchepp_softc *sc)
{
	/*
	 * Abort transfers on all pipes, close pipes, destroy xfers and buffers.
	 */
	if (sc->sc_bin_pipe) {
		usbd_abort_pipe(sc->sc_bin_pipe);
	}
	if (sc->sc_bout_pipe) {
		usbd_abort_pipe(sc->sc_bout_pipe);
	}
	if (sc->sc_intr_pipe) {
		usbd_abort_pipe(sc->sc_intr_pipe);
		(void) usbd_close_pipe(sc->sc_intr_pipe);
	}
	if (sc->sc_bin_xfer)
		usbd_destroy_xfer(sc->sc_bin_xfer);
	if (sc->sc_bout_sync_xfer)
		usbd_destroy_xfer(sc->sc_bout_sync_xfer);
	if (sc->sc_bout_async_xfer)
		usbd_destroy_xfer(sc->sc_bout_async_xfer);
	if (sc->sc_intr_xfer)
		usbd_destroy_xfer(sc->sc_intr_xfer);

	(void) usbd_close_pipe(sc->sc_bin_pipe);
	(void) usbd_close_pipe(sc->sc_bout_pipe);
	(void) usbd_close_pipe(sc->sc_intr_pipe);
}

int
uchepp_usb_bulk_send_sync(struct uchepp_softc *sc, void *buf, size_t bsiz)
{
	usbd_status err;
	uint32_t n;
	void *realbuf;

	if (bsiz > sc->sc_bulk_pipe_maxsize) {
		aprint_error_dev(sc->sc_dev, "bulk-out buffer exceeds %d bytes",
		    sc->sc_bulk_pipe_maxsize);
		return -1;
	}

	// mutex_enter(&sc->sc_lock);

	realbuf = usbd_get_buffer(sc->sc_bout_sync_xfer);

	memcpy(realbuf, buf, bsiz);

	n = bsiz;
	err = usbd_bulk_transfer(sc->sc_bout_sync_xfer, sc->sc_bout_pipe,
	    0, USBD_DEFAULT_TIMEOUT, realbuf, &n);
	// mutex_exit(&sc->sc_lock); // we are done after usbd_bulk_transfer()
	if (err) {
		aprint_error_dev(sc->sc_dev, "bulk-out xfer failed: %s\n",
		                 usbd_errstr(err));
		return -1;
	}

	return 0;
}

static void
uchepp_usb_bulk_send_done(struct usbd_xfer *xfer, void *priv, usbd_status err) {
	struct uchepp_usb_send_async_ctx *ctx = priv;
	struct uchepp_softc *sc = ctx->sc;

	if (err) {
		aprint_error_dev(sc->sc_dev, "async send failed: %s\n",
		                 usbd_errstr(err));
		goto free_mem;
	}

free_mem:
	kmem_intr_free(ctx->buf, sc->sc_bulk_pipe_maxsize);
	kmem_intr_free(ctx, sizeof *ctx);
}

int
uchepp_usb_bulk_send_async(struct uchepp_softc *sc, void *buf, size_t bsiz) {
	struct uchepp_usb_send_async_ctx *ctx;
	usbd_status err;

	if (bsiz > sc->sc_bulk_pipe_maxsize) {
		aprint_error_dev(sc->sc_dev, "bulk-out buffer exceeds %d bytes",
		    sc->sc_bulk_pipe_maxsize);
		return -1;
	}

	ctx = kmem_intr_zalloc(sizeof *ctx, KM_NOSLEEP);
	ctx->sc = sc;
	ctx->buf = kmem_intr_zalloc(sc->sc_bulk_pipe_maxsize, KM_NOSLEEP);

	memcpy(ctx->buf, buf, bsiz);

	// mutex_enter(&sc->sc_lock);
	usbd_setup_xfer(sc->sc_bout_async_xfer, ctx, ctx->buf, bsiz, 0,
	                USBD_DEFAULT_TIMEOUT, uchepp_usb_bulk_send_done);

	err = usbd_transfer(sc->sc_bout_async_xfer);
	// mutex_exit(&sc->sc_lock); // we are done after usbd_transfer() submission

	if (err != USBD_NORMAL_COMPLETION && err != USBD_IN_PROGRESS) {
		aprint_error_dev(sc->sc_dev, "failed to dispatch async send: %s\n",
		                 usbd_errstr(err));
		kmem_intr_free(ctx->buf, sc->sc_bulk_pipe_maxsize);
		kmem_intr_free(ctx, sizeof *ctx);
		return -1;
	}

	return 0;
}

int
uchepp_usb_bulk_recv(struct uchepp_softc *sc, void *buf, size_t bsiz)
{
	usbd_status err;
	uint32_t n;
	void *realbuf;
	int ret;

	if (bsiz > sc->sc_bulk_pipe_maxsize) {
		aprint_error_dev(sc->sc_dev, "bulk-in buffer exceeds %d bytes",
		    sc->sc_bulk_pipe_maxsize);
		return -1;
	}

	// mutex_enter(&sc->sc_lock);

	realbuf = usbd_get_buffer(sc->sc_bin_xfer);

	n = bsiz;
	err = usbd_bulk_transfer(sc->sc_bin_xfer, sc->sc_bin_pipe, 0,
	                         USBD_DEFAULT_TIMEOUT, realbuf, &n);
	if (err) {
		aprint_error_dev(sc->sc_dev, "bulk-in xfer failed: %s\n",
		                 usbd_errstr(err));
		ret = -1;
	} else {
		memcpy(buf, realbuf, bsiz);
		ret = 0;
	}

	// mutex_exit(&sc->sc_lock); // we've to protect memcpy() with realbuf above

	return ret;
}

int
uchepp_usb_req_vendor_read(struct uchepp_softc *sc, uint8_t req, void *buf,
    size_t bsiz)
{
	usbd_status err;
	usb_device_request_t request;
	uint8_t realbuf[UCHEPP_CTRL_IN_BUF_SIZE];
	int actlen;

	if (bsiz > UCHEPP_CTRL_IN_BUF_SIZE) {
		aprint_error_dev(sc->sc_dev,
		    "vendor read request 0x%.2x exceeds %d bytes\n", req,
		    UCHEPP_CTRL_IN_BUF_SIZE);
		return -1;
	}

	request.bmRequestType = UT_READ_VENDOR_DEVICE;
	request.bRequest = req;
	USETW(request.wValue, 0);
	USETW(request.wIndex, 0);
	USETW(request.wLength, UCHEPP_CTRL_IN_BUF_SIZE);

	// NOTE: passing of arbitrary KVA as the buffer in the control request
	err =
	    usbd_do_request_flags(sc->sc_udev, &request, realbuf,
	    USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT);
	if (err) {
		aprint_error_dev(sc->sc_dev,
		    "vendor read request 0x%.2x failed: %s\n", req,
		    usbd_errstr(err));
		return -1;
	}

	memcpy(buf, realbuf, bsiz);

	return 0;
}

/**********************************************************************************
 *
 * Autoconf interface.
 *
 *********************************************************************************/

static int
uchepp_match(device_t parent, cfdata_t match, void *aux)
{
	struct usb_attach_arg *uiaa = aux;

	if (usb_lookup(uchepp_devs, uiaa->uaa_vendor, uiaa->uaa_product)) {
		return UMATCH_VENDOR_PRODUCT;
	} else {
		return UMATCH_NONE;
	}
}

static void
uchepp_attach(device_t parent, device_t self, void *aux)
{
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
	sc->sc_attached = false;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);

	/*
	 * Prepare the USB machinery to work.
	 */
	if (uchepp_usb_init(sc)) {
		sc->sc_dying = true;
		(void) config_detach(self, 0);
		return;
	}

	if (uchepp_usb_req_vendor_read(sc, UCHEPP_REQ_GET_VERSION,
		&sc->sc_version, 1)) {
		sc->sc_version = 0xff;	// Unknown version
	}

#ifdef UCHEPP_DEBUG
	aprint_normal_dev(sc->sc_dev, "firmware version 0x%x\n",
	    sc->sc_version);
#endif
	// usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->sc_udev, sc->sc_dev);

	uchepp_gpio_attach(sc);

	sc->sc_attached = true;
}

static int
uchepp_detach(device_t self, int flags)
{
	struct uchepp_softc *sc = device_private(self);

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_dying) {
		(void) config_detach_children(self, flags);
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
