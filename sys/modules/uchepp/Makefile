.include "../Makefile.inc"

.PATH:	${S}/dev/usb

KMOD=	uchepp
IOCONF=	uchepp.ioconf

SRCS+=	uchepp.c uchepp_usb.c uchepp_gpio.c uchepp_iic.c

#WARNS= 0 # XXX default is 5


.include <bsd.kmodule.mk>
