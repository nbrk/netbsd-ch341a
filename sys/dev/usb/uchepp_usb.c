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

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>

#define	UCHEPP_USB_CONFIGURATION_INDEX	0
#define	UCHEPP_USB_INTERFACE_INDEX	0

#define UCHEPP_CTRL_IN_BUF_SIZE	8



static int	find_connect_endpoints(struct uchepp_softc *sc);

static int
find_connect_endpoints(struct uchepp_softc *sc)
{
	usbd_status err;
	struct usbd_interface *iface;
	int i;
	uint8_t numeps;

	err = usbd_device2interface_handle(sc->sc_udev, UCHEPP_USB_INTERFACE_INDEX,
	                                   &iface);
	if (err) {
		aprint_error_dev(sc->sc_dev, "failed to get interface handle: %s\n",
		                 usbd_errstr(err));
		return -1;
	}

	err = usbd_endpoint_count(iface, &numeps);
	if (err) {
		aprint_error_dev(sc->sc_dev, "failed to get endpoint count: %s\n",
		                 usbd_errstr(err));
		return -1;
	}

	// iterate over all endpoints, grab first bulk in/out, and open pipes
	for (i = 0; i < numeps; ++i) {
		usb_endpoint_descriptor_t const * const epdesc =
		                usbd_interface2endpoint_descriptor(iface, i);
		if (!epdesc)
			continue;

		if ((UE_GET_XFERTYPE(epdesc->bmAttributes) == UE_BULK) &&
		    (UE_GET_DIR(epdesc->bEndpointAddress) == UE_DIR_IN) &&
		                !sc->sc_bin_pipe) {
			sc->sc_bin_pipe_maxsize = UGETW(epdesc->wMaxPacketSize);
			err = usbd_open_pipe(iface, epdesc->bEndpointAddress,
			                     USBD_EXCLUSIVE_USE | USBD_MPSAFE,
			                     &sc->sc_bin_pipe);
			if (err) {
				aprint_error_dev(sc->sc_dev,
				        "failed to open bulk-in pipe: %s\n",
				        usbd_errstr(err));
				return -1;
			}
		} else if ((UE_GET_XFERTYPE(epdesc->bmAttributes) == UE_BULK) &&
		           (UE_GET_DIR(epdesc->bEndpointAddress) == UE_DIR_OUT) &&
		           !sc->sc_bout_pipe) {
			sc->sc_bout_pipe_maxsize = UGETW(epdesc->wMaxPacketSize);
			err = usbd_open_pipe(iface, epdesc->bEndpointAddress,
			                     USBD_EXCLUSIVE_USE | USBD_MPSAFE,
			                     &sc->sc_bout_pipe);
			if (err) {
				aprint_error_dev(sc->sc_dev,
				        "failed to open bulk-out pipe: %s\n",
				        usbd_errstr(err));
				return -1;
			}
		} else if ((UE_GET_XFERTYPE(epdesc->bmAttributes) == UE_INTERRUPT) &&
		           (UE_GET_DIR(epdesc->bEndpointAddress) == UE_DIR_IN) &&
		           !sc->sc_intr_pipe) {
			sc->sc_intr_pipe_maxsize = UGETW(epdesc->wMaxPacketSize);

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
	aprint_normal_dev(sc->sc_dev, "bulk-in 0x%.2x(%d), bulk-out 0x%.2x(%d), interrupt 0x%.2x(%d)\n",
	                  sc->sc_bin_pipe->up_endpoint->ue_edesc->bEndpointAddress,
	                  sc->sc_bin_pipe_maxsize,
	                  sc->sc_bout_pipe->up_endpoint->ue_edesc->bEndpointAddress,
	                  sc->sc_bout_pipe_maxsize,
	                  sc->sc_intr_pipe->up_endpoint->ue_edesc->bEndpointAddress,
	                  sc->sc_intr_pipe_maxsize
	                  );
#endif

	return 0;
}



int
uchepp_usb_init(struct uchepp_softc *sc)
{
	usbd_status err;

	// Configure the device. Very important.
	err = usbd_set_config_index(sc->sc_udev, UCHEPP_USB_CONFIGURATION_INDEX, 1);
	if (err) {
		aprint_error_dev(sc->sc_dev, "failed to set configuration: %s\n",
		                 usbd_errstr(err));
		return -1;
	}

	// Find all three endpoints, query data sizes, open pipes
	if (find_connect_endpoints(sc)) {
		aprint_error_dev(sc->sc_dev, "failed to find or connect endpoints\n");
		return -1;
	}

	/*
	 * Create usbd_xfer objects, each allocating a nice DMA-compatible buffer
	 * capable of holding the endpoint's maximum packet size. We reuse it.
	 */
	if (usbd_create_xfer(sc->sc_bin_pipe, sc->sc_bin_pipe_maxsize, 0, 0,
	                     &sc->sc_bin_xfer)) {
		aprint_error_dev(sc->sc_dev, "failed to create data-in xfer\n");
		return -1;
	}
	if (usbd_create_xfer(sc->sc_bout_pipe, sc->sc_bout_pipe_maxsize, 0, 0,
	                     &sc->sc_bout_xfer)) {
		aprint_error_dev(sc->sc_dev, "failed to create data-out xfer\n");
		return -1;
	}
	if (usbd_create_xfer(sc->sc_intr_pipe, sc->sc_intr_pipe_maxsize, 0, 0,
	                     &sc->sc_intr_xfer)) {
		aprint_error_dev(sc->sc_dev, "failed to create intr-in xfer\n");
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
		(void)usbd_close_pipe(sc->sc_intr_pipe);
	}
	if (sc->sc_bin_xfer)
		usbd_destroy_xfer(sc->sc_bin_xfer);
	if (sc->sc_bout_xfer)
		usbd_destroy_xfer(sc->sc_bout_xfer);
	if (sc->sc_intr_xfer)
		usbd_destroy_xfer(sc->sc_intr_xfer);

	(void)usbd_close_pipe(sc->sc_bin_pipe);
	(void)usbd_close_pipe(sc->sc_bout_pipe);
}

int
uchepp_usb_bulk_send(struct uchepp_softc *sc, void *buf, size_t bsiz)
{


	usbd_status err;
	uint32_t n;
	void * const realbuf = usbd_get_buffer(sc->sc_bout_xfer);

	if (bsiz > sc->sc_bout_pipe_maxsize) {
		aprint_error_dev(sc->sc_dev,  "bulk-out buffer exceeds %d bytes",
		                 sc->sc_bout_pipe_maxsize);
		return -1;
	}

	memcpy(realbuf, buf, bsiz);

	n = bsiz;
	err = usbd_bulk_transfer(sc->sc_bout_xfer, sc->sc_bout_pipe,
	                         0, USBD_DEFAULT_TIMEOUT, realbuf, &n);
	if (err) {
		aprint_error_dev(sc->sc_dev,
		                 "bulk-out xfer failed: %s\n",
		                 usbd_errstr(err));
		return -1;
	}

	return 0;
}

int
uchepp_usb_bulk_recv(struct uchepp_softc *sc, void *buf, size_t bsiz)
{
	usbd_status err;
	uint32_t n;
	void * const realbuf = usbd_get_buffer(sc->sc_bin_xfer);

	if (bsiz > sc->sc_bin_pipe_maxsize) {
		aprint_error_dev(sc->sc_dev,  "bulk-in buffer exceeds %d bytes",
		                 sc->sc_bin_pipe_maxsize);
		return -1;
	}

	n = bsiz;
	err = usbd_bulk_transfer(sc->sc_bin_xfer, sc->sc_bout_pipe,
	                         0, USBD_DEFAULT_TIMEOUT, realbuf, &n);
	if (err) {
		aprint_error_dev(sc->sc_dev,
		                 "bulk-in xfer failed: %s\n",
		                 usbd_errstr(err));
		return -1;
	}

	memcpy(buf, realbuf, bsiz);

	return 0;
}

int
uchepp_usb_req_vendor_read(struct uchepp_softc *sc, uint8_t req, void *buf, size_t bsiz)
{
	usbd_status err;
	usb_device_request_t request;
	uint8_t realbuf[UCHEPP_CTRL_IN_BUF_SIZE];
	int actlen;

	if (bsiz > UCHEPP_CTRL_IN_BUF_SIZE) {
		aprint_error_dev(sc->sc_dev,  "vendor read request 0x%.2x exceeds %d bytes\n",
		                 req, UCHEPP_CTRL_IN_BUF_SIZE);
		return -1;
	}

	request.bmRequestType = UT_READ_VENDOR_DEVICE;
	request.bRequest = req;
	USETW(request.wValue, 0);
	USETW(request.wIndex, 0);
	USETW(request.wLength, UCHEPP_CTRL_IN_BUF_SIZE);

	// NOTE: passing of arbitrary KVA as the buffer in the control request
	err = usbd_do_request_flags(sc->sc_udev, &request, realbuf, USBD_SHORT_XFER_OK, &actlen,
	                            USBD_DEFAULT_TIMEOUT);
	if (err) {
		aprint_error_dev(sc->sc_dev, "vendor read request 0x%.2x failed: %s\n",
		                 req, usbd_errstr(err));
		return -1;
	}

	memcpy(buf, realbuf, bsiz);

	return 0;
}
