#  Copyright (c) 2004 - 2017     Paul Scherrer Institute
#  Copyright (c) 2017 - Present  European Spallation Source ERIC
#
#  The program is free software: you can redistribute
#  it and/or modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation, either version 2 of the
#  License, or any newer version.
#
#  This program is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#  more details.
#
#  You should have received a copy of the GNU General Public License along with
#  this program. If not, see https://www.gnu.org/licenses/gpl-2.0.txt
#
#  Author:      Dirk Zimoch (PSI)
#  Maintainer:  Simon Rose (ESS) <simon.rose@ess.eu>


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

USR_CFLAGS += -std=gnu99

TEMPLATES += $(APPDB)/moduleversion.template
