#
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
#  PSI original author : Dirk Zimoch
#  ESS specific author : Jeong Han Lee
#               email  : han.lee@esss.se
#
# Date    : Wednesday, November 29 13:46:39 CET 2017
# version : 0.0.1


# This is the one time path in order to compile and install
# require within EPICS Environment. After the installation
# include $(E3_REQUIRE_TOOLS)/driver.makefile should be used
#
where_am_I := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# It is easy to maintain RULES_E3 if we use the "repository" makefile
# instead of the installed makefile.
include $(where_am_I)/App/tools/driver.makefile


BUILDCLASSES += Linux

SOURCES += require.c
DBDS    += require.dbd
SOURCES += runScript.c
DBDS    += runScript.dbd

# PSI split expression at 0b6d1dd. So, require.Makefile should
# handel the different require source files with and without
# expr.c in the same way. Thus, I added the additional logic
# to handle to cover entire cases.
# If there is no expr.c, it is now safe to ignore it
# Friday, May 11 21:58:24 CEST 2018, jhlee

expr_src=expr.c
SOURCES += $(filter $(expr_src), $(wildcard *.c))


##
SOURCES += dbLoadTemplate.y
DBDS    += dbLoadTemplate.dbd

# ESS doesn't have any T2_ppc604 and vxWorks target
# Friday, May 11 22:05:07 CEST 2018, jhlee
#
#SOURCES_T2 += strdup.c
#SOURCES_vxWorks += asprintf.c
#
#HEADERS += strdup.h
#HEADERS += asprintf.h

HEADERS += require.h

#HEADERS += require_env.h

# We need to find the Linux link.h before the EPICS link.h
USR_INCLUDES_Linux=-idirafter $(EPICS_BASE)/include 

# ESS require doesn't use T_A, because Linux should handle linux as "1"
# instead of its name. ESS require can handle them within the EPICS
# IOC shell internally.
# 
#USR_CFLAGS += -DT_A='"${T_A}"'

# ESS doesn't support WIN32
# This should really go into some global WIN32 config file
# USR_CFLAGS_WIN32 += /D_WIN32_WINNT=0x501


TEMPLATES += moduleversion.template
#TEMPLATES += moduleversion.db

dbLoadTemplate.c: dbLoadTemplate_lex.c ../dbLoadTemplate.h

## moduleversion should convert to db instead of template
## So, ESS uses it internally independent upon any IOC
## varialbes

EPICS_BASE_HOST_BIN = $(EPICS_BASE)/bin/$(EPICS_HOST_ARCH)
MSI =  $(EPICS_BASE_HOST_BIN)/msi


USR_DBFLAGS += -I . -I ..
USR_DBFLAGS += -I$(EPICS_BASE)/db


TEMS = $(wildcard *.template)


db: $(TEMS)

$(TEMS): 
	@printf "Inflating database ... %44s >>> %40s \n" "$@" "$(basename $(@)).db"
	@rm -f  $(basename $(@)).db.d  $(basename $(@)).db
	@$(MSI) -D $(USR_DBFLAGS) -o $(basename $(@)).db $@  > $(basename $(@)).db.d
	@$(MSI)    $(USR_DBFLAGS) -o $(basename $(@)).db $@




.PHONY: db $(TEMS) 
