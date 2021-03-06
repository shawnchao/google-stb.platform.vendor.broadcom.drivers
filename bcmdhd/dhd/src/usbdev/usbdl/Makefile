#
# USB Remote Download Makefile - Host linux user mode driver
#
#
# Copyright (C) 2017, Broadcom Corporation
# All Rights Reserved.
# 
# This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
# the contents of this file may not be disclosed to third parties, copied
# or duplicated in any form, in whole or in part, without the prior
# written permission of Broadcom Corporation.
#
# $Id: Makefile 675078 2016-12-14 05:16:14Z $


SRCBASE = ../..
include $(SRCBASE)/Makerules

vpath %.c $(SRCBASE)/shared

ifeq ($(STBLINUX),1)
ifeq ($(TARGETENV), linuxarm64)
CFLAGS= -Wall -Wstrict-prototypes  -Wno-pointer-to-int-cast -g -O2 -I$(SRCBASE)/include -I$(SRCBASE)/../components/shared -I$(SRCBASE)/shared/zlib -DBCMTRXV2 -fPIC
else
CFLAGS= -Wall -Werror -Wstrict-prototypes  -g -O2 -I$(SRCBASE)/include -I$(SRCBASE)/../components/shared -I$(SRCBASE)/shared/zlib -DBCMTRXV2 -fPIC
endif
else
CFLAGS= -Wall -Werror -Wstrict-prototypes -g -O2 -I$(SRCBASE)/include -I$(SRCBASE)/../components/shared -I$(SRCBASE)/shared/zlib -DBCMTRXV2
endif


LIBS = -lusb
TARGET = $(ObjPfx)bcmdl
SRCS = bcmdl.c adler32.c inffast.c inflate.c infcodes.c infblock.c inftrees.c infutil.c zutil.c crc32.c bcmutils.c

OBJECTS = $(SRCS:%.c=$(ObjPfx)%.o)

TESTS = tsthwa tstjtag timehwa timejtag

VPATH := $(SRCBASE)/shared/zlib

ifneq ($(findstring freebsd,$(TARGETENV)),)
        # To do cross compilation on linux,
        #     - Define the env CROSS_COMPILE pointing to the cross compile tool dir
        #     - Define the env FREEBSD_DIR pointing to the FreeBSD source directory
	# To do local compilation on a FreeBSD machine, do not set CROSS_COMPILE or FREEBSD_DIR
	CC := $(CROSS_COMPILE)gcc
	LD := $(CROSS_COMPILE)ld
	NM := $(CROSS_COMPILE)nm
	OBJCOPY := $(CROSS_COMPILE)objcopy
	CFLAGS += -I$(FREEBSD_DIR)/include
	OBJECTS += $(ObjPfx)usb_bsd.o
	COMPILE = $(CC)
	CLEAN = clean_linux
	ifneq ($(findstring freebsd_be,$(TARGETENV)),)
		CFLAGS += -DIL_BIGENDIAN
	endif
	ifneq ($(findstring cust_hw_30_1,$(TARGETHW)),)
		CFLAGS += -DCUSTOMER_HW_30_1
	endif
	ifneq ($(findstring cust_hw_30_2,$(TARGETHW)),)
		CFLAGS += -DCUSTOMER_HW_30_2
	endif
endif

ifeq ($(TARGETENV), linux)
	OBJECTS += $(ObjPfx)usb_linux.o
	COMPILE = $(CC)
	CLEAN = clean_linux
	CFLAGS += -I../libusb
	LDFLAGS += $(CROSS_LD_PATH)
endif

ifeq ($(TARGETENV), linuxmips_be)
	OBJECTS += $(ObjPfx)usb_linux.o
	COMPILE = mips-linux-gcc
ifeq ($(NETAPP_WIFI_DRIVER), dhd)
	LIBS := -L$(LIBUSB_PATH)/lib $(LIBUSB_PATH)/lib/libusb.a
	CFLAGS += -I$(LIBUSB_PATH)/include -DIL_BIGENDIAN
else
	LIBS := -L$(LIBUSB_PATH)/.libs $(LIBUSB_PATH)/.libs/libusb.a
	CFLAGS += -I$(LIBUSB_PATH) -DIL_BIGENDIAN
endif
endif


ifeq ($(TARGETENV), linuxmips)
	TARGET_PREFIX := mipsel-linux-
	OBJECTS += $(ObjPfx)usb_linux.o
	COMPILE = mipsel-linux-gcc
	CLEAN = clean_linux
ifeq ($(NETAPP_WIFI_DRIVER), dhd)
	LDFLAGS += -L$(LIBUSB_PATH)/lib
	LIBS := -L$(LIBUSB_PATH)/lib $(LIBUSB_PATH)/lib/libusb.a
	CFLAGS += -I$(LIBUSB_PATH)/include
else
	CFLAGS += -I ../libusb
	LDFLAGS += -L$(LIBUSB_PATH)/.libs
	LIBS= ../libusb/.libs/libusb.a
	LIBS := -L$(LIBUSB_PATH)/.libs $(LIBUSB_PATH)/.libs/libusb.a
	CFLAGS += -I$(LIBUSB_PATH)
endif
#It is wrong to put install target here, affect the rest of ifeq tests
#install:
#	cp bcmdl $(SRCBASE)/router/mipsel-uclibc/target/bin
endif

ifeq ($(TARGETENV), linux26mips)
	TARGET_PREFIX := mipsel-uclibc-linux26-
	OBJECTS += $(ObjPfx)usb_linux.o
	CFLAGS += -I ../libusb
	COMPILE = mipsel-uclibc-linux26-gcc
	CLEAN = clean_linux
	LDFLAGS += -L ../libusb/.libs
	LIBS= ../libusb/.libs/libusb.a
#It is wrong to put install target here, affect the rest of ifeq tests
#install:
#	cp bcmdl $(SRCBASE)/router/mipsel-uclibc/target/bin
endif

ifeq ($(TARGETENV), macos)
	TARGET = bcmdl_macos
	CLEAN = clean_macos
	COMPILE = xcodebuild
	PROJECT = bcmdl.xcodeproj
endif

ifeq ($(TARGETENV), linuxarm)
	TARGET_PREFIX := arm-linux-
	OBJECTS += $(OBJDIR)usb_linux.o
	COMPILE = arm-linux-gcc
	CLEAN = clean_linux
	LDFLAGS += -L$(LIBUSB_PATH)/lib
	LIBS := -L$(LIBUSB_PATH)/lib $(LIBUSB_PATH)/.libs/libusb.a
	CFLAGS += -I$(LIBUSB_PATH)/
endif

ifeq ($(TARGETENV), linuxarm64)
	TARGET_PREFIX := aarch64-linux-gnu-
	OBJECTS += $(OBJDIR)usb_linux.o
	COMPILE = aarch64-linux-gnu-gcc
	CLEAN = clean_linux
	LDFLAGS += -L$(LIBUSB_PATH)/lib
	LIBS := -L$(LIBUSB_PATH)/lib $(LIBUSB_PATH)/.libs/libusb.a
	CFLAGS += -I$(LIBUSB_PATH)/
endif

ifeq ($(STBLINUX),1)
ifeq ($(TARGETENV), linuxarm_le)
	TARGET_PREFIX := arm-linux-
	OBJECTS += $(OBJDIR)usb_linux.o
	COMPILE = arm-linux-gcc
	CLEAN = clean_linux
	LDFLAGS += -L$(LIBUSB_PATH)/lib
	LIBS := -L$(LIBUSB_PATH)/lib $(LIBUSB_PATH)/lib/libusb.a
	CFLAGS += -I$(LIBUSB_PATH)/include
endif
endif

# Have to make sure the TARGETARCH is set when TARGETENV is "linuxmips"
# Whether TARGETARCH or TARGETENV is the main macro doesn't really matter
# What matter is we need uniformity
ifeq ($(TARGETARCH), mips)
	ObjPfx := $(TARGETARCH)/
	OBJECTS := $(SRCS:%.c=$(ObjPfx)%.o)
	OBJECTS += $(ObjPfx)usb_linux.o
	TARGET_PREFIX := mipsel-linux-
	TARGET := $(ObjPfx)bcmdl
	COMPILE := $(CC)
	LDFLAGS += $(CROSS_LD_PATH)
ifeq ($(NETAPP_WIFI_DRIVER), dhd)
	LDFLAGS += -L$(LIBUSB_PATH)/lib
	LIBS := -L$(LIBUSB_PATH)/lib $(LIBUSB_PATH)/lib/libusb.a
	CFLAGS += -I$(LIBUSB_PATH)/include
else
	LDFLAGS += -L$(LIBUSB_PATH)/.libs
	LIBS= ../libusb/.libs/libusb.a
	LIBS := -L$(LIBUSB_PATH)/.libs $(LIBUSB_PATH)/.libs/libusb.a
	CFLAGS += -I ../libusb
	CFLAGS += -I$(LIBUSB_PATH)
endif
endif

# Have to make sure the TARGETARCH is set when TARGETENV is "linuxarm"
# Whether TARGETARCH or TARGETENV is the main macro doesn't really matter
# What matter is we need uniformity
ifeq ($(TARGETARCH), arm)
	ObjPfx := $(TARGETARCH)/
	OBJECTS := $(SRCS:%.c=$(ObjPfx)%.o)
	OBJECTS += $(ObjPfx)usb_linux.o
	TARGET_PREFIX := arm-linux
	TARGET := $(ObjPfx)bcmdl
	COMPILE := $(CC)
	LDFLAGS += $(CROSS_LD_PATH)
	LDFLAGS += -L$(LIBUSB_PATH)/.libs
	LIBS= ../libusb/.libs/libusb.a
	LIBS := -L$(LIBUSB_PATH)/.libs $(LIBUSB_PATH)/.libs/libusb.a
	CFLAGS += -I ../libusb
	CFLAGS += -I$(LIBUSB_PATH)
endif

ifeq ($(TARGETARCH), aarch64)
	ObjPfx := $(TARGETARCH)/
	OBJECTS := $(SRCS:%.c=$(ObjPfx)%.o)
	OBJECTS += $(ObjPfx)usb_linux.o
	TARGET_PREFIX := aarch64-linux-gnu-
	TARGET := $(ObjPfx)bcmdl
	COMPILE := $(CC)
	LDFLAGS += $(CROSS_LD_PATH)
	ifeq ($(STBLINUX),1)
        LDFLAGS += -L$(LIBUSB_PATH)/libs
        LIBS= ../libusb/libs/libusb.a
        LIBS := -L$(LIBUSB_PATH)/libs $(LIBUSB_PATH)/libs/libusb.a
        else
	LDFLAGS += -L$(LIBUSB_PATH)/.libs
	LIBS= ../libusb/.libs/libusb.a
	LIBS := -L$(LIBUSB_PATH)/.libs $(LIBUSB_PATH)/.libs/libusb.a
	endif
	CFLAGS += -I ../libusb
	CFLAGS += -I$(LIBUSB_PATH)
	CFLAGS += -I$(LIBUSB_PATH)/include
endif

ifeq ($(BCMQT),1)
	CFLAGS += -DBCMQT
	TARGET = bcmdl_qt
endif

all: $(TARGET)

tests: $(TESTS)

$(ObjPfx)bcmdl: $(OBJECTS)
	$(COMPILE)  $(OBJECTS)  $(CFLAGS) $(LDFLAGS) $(LIBS)  -o $@

bcmdl_qt: $(OBJECTS)
	$(COMPILE)  $(OBJECTS)  $(CFLAGS) $(LIBS)  -o $@

$(TESTS): %: %.o
	$(COMPILE) $< $(CFLAGS) $(LIBS) -o $@

# Executable is located in build/Debug/bcmdl
#
bcmdl_macos:
	$(COMPILE) -project $(PROJECT) -target $(TARGET) -configuration Debug build

clean_macos:
	$(COMPILE) -project $(PROJECT) -target $(TARGET) -configuration Debug clean

clean_linux:
	rm -rf bcmdl_qt $(TARGET) $(OBJECTS) $(TESTS) $(TESTS:=.o)

clean: $(CLEAN)

$(ObjPfx)%.o: %.c
	@[ -d "$(@D)" ] || mkdir -pv $(@D)
	$(COMPILE) -c $(CFLAGS) -o $@ $<
