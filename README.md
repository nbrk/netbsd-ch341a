# NetBSD support for WinChipHead/QinHeng CH341A in EPP mode (GPIO, I2C)
This is a NetBSD kernel mode driver for USB serial adapter CH341A, operating in so 
called "EPP" mode (USB PID 0x5512). In this mode, the chip provides GPIO and 
SPI/I2C bus mastering functionality via a series of simple (but highly undocumented)
commands over the bulk endpoint.

The driver currently attaches `gpio(4)` and exposes all of the 19 I/O lines, of 
which only the first 6 lines (namely, pins `D[5:0]`) are truly bi-directional and
support output, rest pins being uncontrollable input-only. Support for attachment 
of `iic(4)` is ongoing.

Right now, the code is a work in progress and probably contains bugs.

# Usage

Add the driver to `sys/dev/usb/files.usb`:
```
# QinHeng/WinChipHead CH341A EPP mode driver
device uchepp: gpiobus
attach uchepp at usbdevif
file   dev/usb/uchepp.c        uchepp
file   dev/usb/uchepp_usb.c    uchepp
file   dev/usb/uchepp_gpio.c   uchepp
file   dev/usb/uchepp_iic.c    uchepp
```

Add device to the kernel config:
```
uchepp* at uhub? port ?
```
or compile and load the module in `sys/modules/uchepp`.

# TODO
- Support of the I2C functionality
- Support of the hardware interrupt (the `#INTR` line)
- Asynchronous USB transfers, so `gpiopwm(4)` won't block in `callout(9)`
- Explore chip's comms protocol, refine interaction with the hardware
