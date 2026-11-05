#Makefile at top of application tree
TOP = ../..
include $(TOP)/configure/CONFIG

ifeq ($(strip $(REQUIRE_VERSION)),)
  REQUIRE_VERSION=$(shell git describe --tags)
endif

LOADABLE_LIBRARY = require3

require3_SRCS += require3_registerRecordDeviceDriver.cpp
require3_registerRecordDeviceDriver_CPPFLAGS += -DLIBVERSION='"$(REQUIRE_VERSION)"' -DMODULE_NAME='"require3"' -DMODULE_SYMBOL_NAME='"require3"'

require3_SRCS += require.c
require3_SRCS += common.c
require3_SRCS += module.c

require3_DBD += require_.dbd

ifeq ($(strip $(WITH_AFTERINIT)),YES)
  require3_SRCS += afterInit.c
  require3_DBD += afterInit.dbd
endif

DBD += require3.dbd

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
require3_registerRecordDeviceDriver.cpp: $(COMMON_DIR)/require3.dbd
	$(PERL) $(EPICS_BASE_HOST_BIN)/registerRecordDeviceDriver.pl $< $(basename $@) | grep -v 'iocshRegisterCommon();' > $@
	sed -i'.bak' -E '/^.*= Registration\(\)\;$$/d' $@
	echo "#include <init.cpp>" >> $@
