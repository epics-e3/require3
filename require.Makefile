where_am_I := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
include $(where_am_I)/require-ess/tools/driver.makefile

APP := require-ess
APPSRC := $(APP)/src
APPDB := $(APP)/Db
APPTOOLS := $(APP)/tools

SOURCES += $(APPSRC)/require.c
SOURCES += $(APPSRC)/afterInit.c
SOURCES += $(APPSRC)/common.c
SOURCES += $(APPSRC)/module.c
DBDS    += $(APPSRC)/require.dbd
DBDS    += $(APPSRC)/afterInit.dbd

HEADERS += $(APPSRC)/require.h
HEADERS += $(APPSRC)/module.h

BINS += $(APPTOOLS)/iocsh
BINS += $(APPTOOLS)/iocsh_utils.py
BINS += $(APPTOOLS)/iocsh_complete.bash

SCRIPTS += $(APPTOOLS)/driver.makefile
SCRIPTS += $(APPTOOLS)/iocsh_epics.supp

# This file is used by submodules to handle library initialisation when they are loaded
SCRIPTS += $(APPSRC)/init.cpp

CONFIGS += configure/CONFIG_REQUIRE

# We need to find the Linux link.h before the EPICS link.h
USR_INCLUDES_Linux=-idirafter $(EPICS_BASE)/include

TEMPLATES += $(APPDB)/moduleversion.template
