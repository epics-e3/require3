#Makefile at top of application tree
TOP = ../..
include $(TOP)/configure/CONFIG

LIBRARY_IOC = require

require_SRCS += require_registerRecordDeviceDriver.cpp

require_SRCS += require.c
require_SRCS += afterInit.c
require_SRCS += common.c
require_SRCS += module.c

require_SRCS += dbLoadTemplate.c

DBD += require.dbd
DBD += afterInit.dbd

DBD += dbLoadTemplate.dbd

INC += require.h

USR_INCLUDES_Linux += -idirafter $(EPICS_BASE)/include
USR_CFLAGS += -std=gnu99
USR_CFLAGS += -DT_A='"$(T_A)"'

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

dbLoadTemplate.c: dbLoadTemplate_lex.c ../dbLoadTemplate.h
