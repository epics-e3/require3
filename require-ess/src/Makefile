#Makefile at top of application tree
TOP = ../..
include $(TOP)/configure/CONFIG

REQUIRE_VERSION=$(shell git describe --tags)

LOADABLE_LIBRARY = require

require_SRCS += require_registerRecordDeviceDriver.cpp
require_registerRecordDeviceDriver_CPPFLAGS += -DLIBVERSION='"$(REQUIRE_VERSION)"' -DMODULE_NAME='"require"'

require_SRCS += require.c
require_SRCS += afterInit.c
require_SRCS += common.c
require_SRCS += module.c

require_DBD += require_.dbd
require_DBD += afterInit.dbd

DBD += require.dbd

INC += require.h
INC += module.h

SCRS += init.cpp

USR_INCLUDES_Linux += -idirafter $(EPICS_BASE)/include
USR_CFLAGS += -std=gnu99
USR_CFLAGS += -DT_A='"$(T_A)"'

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# Override this rule from RULES.Db
require_registerRecordDeviceDriver.cpp: $(COMMON_DIR)/require.dbd
	$(PERL) $(EPICS_BASE_HOST_BIN)/registerRecordDeviceDriver.pl $< $(basename $@) | grep -v 'iocshRegisterCommon();' > $@
	sed -i'.bak' -E '/^.*= Registration\(\)\;$$/d' $@
	echo "#include <init.cpp>" >> $@
