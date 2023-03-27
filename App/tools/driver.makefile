# driver.makefile
#
# This generic makefile compiles EPICS modules (drivers, records, snl, ...)
# for all installed EPICS versions.
# Read this documentation and the inline comments carefully before
# changing anything in this file.
#
# Usage: Create a Makefile containig the line:
#        include /ioc/tool/driver.makefile
#        Optionally add variable definitions below that line.
#
# This makefile automatically finds the source file (unless overwritten with
# the SOURCES variable in your Makefile) and generates a module consisting
# of a library and .dbd file for each EPICS version and each target architecture.
# Therefore, it calls itself recursively.
#
# - First run: (see comment ## RUN 1)
#   Find the sources etc.
#   Include EPICS configuration files for ${EPICSVERSION}, determined by ${EPICS_BASE}
#   Iterate over all target architectures (${T_A}) defined.
#
# - Second run: (see comment ## RUN 2)
#   Check which target architectures to build.
#   Create O.${EPICSVERSION}_${T_A} subdirectories if necessary.
#   Change to O.${EPICSVERSION}_${T_A} subdirectories.
#
# - Third run: (see comment ## RUN 3)
#   Compile everything.
#
# Module names are derived from the directory name (unless overwritten
# with the MODULE variable in your Makefile).
# A LIBVERSION number is generated from the latest CVS or GIT tag of the sources.
# If any file is not up-to-date in CVS/GIT, not tagged, or tagged differently from the
# other files, the version is a test version and labelled with the user name.
# The library is installed to ${EPICS_MODULES}/${MODULE}/${LIBVERSION}/lib/${T_A}/.
# A module can be loaded with  require "<module>" [,"<version>"] [,"<variable>=<substitution>, ..."]
#
# User variables (add them to your Makefile, none is required):
# MODULE
#    Name of the built module.
#    If not defined, it is derived from the directory name.
# SOURCES
#    All source files to compile. 
#    If not defined, default is all *.c *.cc *.cpp *.st *.stt in
#    the source directory (where you run make).
#    If you define this, you must list ALL sources.
# DBDS
#    All dbd files of the project.
#    If not defined, default is all *.dbd files in the source directory.
# HEADERS
#    Header files to install (e.g. to be included by other drivers)
#    If not defined, all headers are for local use only.
# ARCH_FILTER
#    Sub set of architectures to build for, e.g. %-ppc604

# Get the location of this file.
MAKEHOME:=$(dir $(lastword ${MAKEFILE_LIST}))
# Get the name of the Makefile that included this file.
USERMAKEFILE:=$(lastword $(filter-out $(lastword ${MAKEFILE_LIST}), ${MAKEFILE_LIST}))


##---## In conda, We only use one version of EPICS base when compiling modules.
##---## EPICS_BASE / EPICS_BASE_VERSION / EPICS_MODULES are set as environment variables by conda
MSI = ${EPICS_BASE_HOST_BIN}/msi
CONFIG=${EPICS_BASE}/configure

# Set LIBVERSION to dev if not set
LIBVERSION := $(or $(LIBVERSION),dev)
EPICSVERSION:=$(EPICS_BASE_VERSION)

BUILDCLASSES = Linux
OS_CLASS_LIST = $(BUILDCLASSES)

MODULE=
PROJECT=
PRJ := $(strip $(or ${MODULE},${PROJECT}))

MODULE_LOCATION =${EPICS_MODULES}/$(or ${PRJ},$(error PRJ not defined))/$(or ${LIBVERSION},$(error LIBVERSION not defined))


# Override config here:
-include ${MAKEHOME}/config

# Use fancy glob to find latest versions.
SHELL = /bin/bash -O extglob

# Some shell commands:
RMDIR = rm -rf
LN = ln -s
RM = rm -f
CP = cp
MKDIR = mkdir -p -m 775

# This is to allow for build numbers in recognized versions. First regex is for grep, second for sed.
VERSIONGLOB = +([0-9]).+([0-9]).+([0-9])?(-+([0-9]))
VERSIONREGEX1 = [0-9]+\.[0-9]+\.[0-9]+(-[0-9]+)?
VERSIONREGEX2 = [0-9]+\.[0-9]+\.[0-9]+\(-[0-9]+\)\?

# Some generated file names:
VERSIONFILE = ${PRJ}_version_${LIBVERSION}.c
REGISTRYFILE = ${PRJ}_registerRecordDeviceDriver.cpp
DEPFILE = ${PRJ}.dep
METAFILE = ${PRJ}_meta.yaml

# Clear potential environment variables.
TEMPLATES=
SOURCES=
DBDS=
DBD_INSTALLS=
HEADERS=
BASH_ENV=
ENV=

# Default target is "build" for all versions.
# Don't install anything (different from default EPICS make rules).
default: build

prebuild:

clean:
	$(RMDIR) O.*

O.%:
	+$(MKDIR) $@

uninstall:
	$(RMDIR) ${MODULE_LOCATION}

IGNOREFILES = .gitignore
%: ${IGNOREFILES}
${IGNOREFILES}:
	@echo -e "O.*\n.gitignore" > $@

# Function that removes duplicates without re-ordering (unlike sort):
define uniq
  $(eval seen :=) \
  $(foreach _,$1,$(if $(filter $_,${seen}),,$(eval seen += $_))) \
  ${seen}
endef

# Some TOP and EPICS_BASE tweeking necessary to work around release check in 3.14.10+.
EB:=${EPICS_BASE}
TOP:=${EPICS_BASE}
-include ${CONFIG}/CONFIG
EPICS_BASE:=${EB}

${CONFIG}/CONFIG:
	$(error EPICS release ${EPICSVERSION} not installed on this host.)

# Variables that need to override data from ${CONFIG}/CONFIG
BASE_CPPFLAGS=

ifndef LEGACY_RSET
USR_CPPFLAGS+=-DUSE_TYPED_RSET
endif

SHRLIB_VERSION=
COMMANDLINE_LIBRARY =

OBJ=.o

COMMON_DIR = O.${EPICSVERSION}_Common
	
ifndef T_A
## RUN 1
# Target achitecture not yet defined, but EPICSVERSION is already known.
# Still in source directory.

# Look for sources etc., and select target architectures to build.
# Export everything for second run:

AUTOSRCS := $(filter-out ~%,$(wildcard *.c *.cc *.cpp *.st *.stt *.gt))
SRCS = $(if ${SOURCES},$(filter-out -none-,${SOURCES}),${AUTOSRCS})
export SRCS

DBD_SRCS = $(if ${DBDS},$(filter-out -none-,${DBDS}),$(wildcard menu*.dbd *Record.dbd) $(strip $(filter-out %Include.dbd dbCommon.dbd %Record.dbd,$(wildcard *.dbd)) ${BPTS}))
DBD_SRCS += ${DBDS_${EPICSVERSION}}
export DBD_SRCS

#record dbd files given in DBDS
RECORDS1 = $(patsubst %Record.dbd, %, $(filter-out dev%, $(filter %Record.dbd, $(notdir ${DBD_SRCS}))))
#record dbd files included by files given in DBDS
RECORDS2 = $(filter-out dev%, $(shell ${MAKEHOME}/expandDBD.tcl -r $(addprefix -I, $(sort $(dir ${DBD_SRCS}))) $(realpath ${DBDS})))
RECORDS = $(sort ${RECORDS1} ${RECORDS2})
export RECORDS

MENUS = $(patsubst %.dbd,%.h,$(wildcard menu*.dbd))
export MENUS

BPTS = $(patsubst %.data,%.dbd,$(wildcard bpt*.data))
export BPTS

DBDINSTALLS = $(DBD_INSTALLS)
DBDINSTALLS += $(MENUS)
DBDINSTALLS += $(BPTS)
export DBDINSTALLS

HDRS = ${HEADERS} $(addprefix ${COMMON_DIR}/,$(addsuffix Record.h,${RECORDS}))
HDRS += ${HEADERS_${EPICSVERSION}}
export HDRS

HDR_SUBDIRS = $(KEEP_HEADER_SUBDIRS)
export HDR_SUBDIRS

TEMPLS = $(if ${TEMPLATES},$(filter-out -none-,${TEMPLATES}),$(wildcard *.template *.db *.subs))
TEMPLS += ${TEMPLATES_${EPICSVERSION}}
TEMPLS += $(wildcard $(COMMON_DIR)/*.db)
export TEMPLS

SCR = $(if ${SCRIPTS},$(filter-out -none-,${SCRIPTS}),$(wildcard *.cmd *.iocsh))
SCR += ${SCRIPTS_${EPICSVERSION}}
export SCR

# Filter architectures to build using EXCLUDE_ARCHS and ARCH_FILTER.
ALL_ARCHS = ${EPICS_HOST_ARCH} ${CROSS_COMPILER_TARGET_ARCHS}
BUILD_ARCHS = $(filter-out $(addprefix %,${EXCLUDE_ARCHS}),$(filter-out $(addsuffix %,${EXCLUDE_ARCHS}),\
        $(if ${ARCH_FILTER},$(filter ${ARCH_FILTER},${ALL_ARCHS}),${ALL_ARCHS})))

SRCS_Linux = ${SOURCES_Linux}
export SRCS_Linux

# Perform default database expansion of .substitions/.templates into $(COMMON_DIR)
db_internal: $(COMMON_DIR)

-include $(COMMON_DIR)/*.db.d

VPATH += $(dir $(TMPS))
VPATH += $(dir $(SUBS))

$(COMMON_DIR)/%.db: %.template
	@printf "Inflating database ... %44s >>> %40s \n" "$^" "$@"
	$(QUIET)$(MSI) -D $(USR_DBFLAGS) -o $(COMMON_DIR)/$(notdir $(basename $@).db) $^ > $(COMMON_DIR)/$(notdir $(basename $@).db).d
	$(QUIET)$(MSI)    $(USR_DBFLAGS) -o $(COMMON_DIR)/$(notdir $(basename $@).db) $^

$(COMMON_DIR)/%.db: %.substitutions
	@printf "Inflating database ... %44s >>> %40s \n" "$^" "$@"
	$(QUIET)$(MSI) -D $(USR_DBFLAGS) -o $(COMMON_DIR)/$(notdir $(basename $@).db) -S $^ > $(COMMON_DIR)/$(notdir $(basename $@).db).d
	$(QUIET)$(MSI)    $(USR_DBFLAGS) -o $(COMMON_DIR)/$(notdir $(basename $@).db) -S $^


install build debug::
	@echo "MAKING EPICS VERSION ${EPICSVERSION}"

debug::
	@echo "===================== Pass 1 ====================="
	@echo "BUILDCLASSES = ${BUILDCLASSES}"
	@echo "LIBVERSION = ${LIBVERSION}"
	@echo "PRJ = ${PRJ}"
	@echo "EPICS_BASE = ${EPICS_BASE}"
	@echo "BUILD_ARCHS = ${BUILD_ARCHS}"
	@echo "ARCH_FILTER = ${ARCH_FILTER}"
	@echo "EXCLUDE_ARCHS = ${EXCLUDE_ARCHS}"
	@echo "LIBVERSION = ${LIBVERSION}"

# Create e.g. build-$(T_A) rules for each architecture, so that we can just do
#   build: build-arch1 build-arch2
define target_rule
$1-%: | $(COMMON_DIR)
	$${MAKE} -f $${USERMAKEFILE} T_A=$$* $1
endef
$(foreach target,install build debug,$(eval $(call target_rule,$(target))))

.SECONDEXPANSION:

# This has to fit under .SECONDEXPANSION in order to catch TMPS and SUBS, which are typically defined
# _after_ driver.makefile is included.
db_internal: $$(addprefix $(COMMON_DIR)/,$$(notdir $$(patsubst %.template,%.db,$$(TMPS))))

db_internal: $$(addprefix $(COMMON_DIR)/,$$(notdir $$(patsubst %.substitutions,%.db,$$(SUBS))))

# This has to be after .SECONDEXPANSION since BUILD_ARCHS will be modified based on EXCLUDE_ARCHS
# and ARCH_FILTER, which are defined _after_ driver.makefile.
$(foreach target,install build debug,$(eval $(target):: $$$$(foreach arch,$$$${BUILD_ARCHS},$(target)-$$$${arch})))

else # T_A

ifeq ($(filter O.%,$(notdir ${CURDIR})),)
## RUN 2
# Target architecture defined.
# Still in source directory, second run.

# Add sources for specific epics types or architectures.
ARCH_PARTS = ${T_A} $(subst -, ,${T_A}) ${OS_CLASS}
VAR_EXTENSIONS = ${EPICSVERSION} ${ARCH_PARTS} ${ARCH_PARTS:%=${EPICSVERSION}_%}
export VAR_EXTENSIONS

REQ = ${REQUIRED} $(foreach x, ${VAR_EXTENSIONS}, ${REQUIRED_$x})
export REQ
ifeq ($(filter ${OS_CLASS},${OS_CLASS_LIST}),)

install% build%: build
install build:
	@echo Skipping ${T_A} because $(if ${OS_CLASS},OS_CLASS=\"${OS_CLASS}\" is not in BUILDCLASSES=\"${BUILDCLASSES}\",it is not available for R$(EPICSVERSION).)
%:
	@true

else ifeq ($(shell which $(firstword ${CC})),)

install% build%: build
install build:
	@echo Warning: Skipping ${T_A} because cross compiler $(firstword ${CC}) is not installed.
%:
	@true

else

install:: build
	$(if $(wildcard ${MODULE_LOCATION}/lib/${T_A}),$(error ${MODULE_LOCATION}/lib/${T_A} already exists. If you really want to overwrite then uninstall first.))

install build debug:: O.${EPICSVERSION}_${T_A}
	@${MAKE} -C O.${EPICSVERSION}_${T_A} -f ../${USERMAKEFILE} $@

endif

SRCS += $(foreach x, ${VAR_EXTENSIONS}, ${SOURCES_$x})
USR_LIBOBJS += ${LIBOBJS} $(foreach x,${VAR_EXTENSIONS},${LIBOBJS_$x})
export USR_LIBOBJS

BINS += $(foreach x, ${VAR_EXTENSIONS}, ${BINS_$x})
export BINS

export CFG

# These variables are written into a .yaml file in the installed module directory to keep track of 
# metadata for which module was compiled.

${PRJ}_GIT_DESC := $(shell git describe --tags 2> /dev/null || git rev-parse HEAD)
export ${PRJ}_GIT_DESC
# The formatting here is just to make sure this is properly parseable .yaml data
${PRJ}_GIT_STATUS := [ $(shell git status --porcelain | grep -v "\.Makefile" | sed 's/^/\\\"/' | sed 's/$$/\\\", /')]
export ${PRJ}_GIT_STATUS

else # in O.*
## RUN 3
# In build directory.

# Add macros like USR_CFLAGS_Linux.
EXTENDED_VARS=INCLUDES CFLAGS CXXFLAGS CPPFLAGS CODE_CXXFLAGS LDFLAGS
$(foreach v,${EXTENDED_VARS},$(foreach x,${VAR_EXTENSIONS},$(eval $v+=$${$v_$x}) $(eval USR_$v+=$${USR_$v_$x})))
CFLAGS += ${EXTRA_CFLAGS}

COMMON_DIR = ../O.${EPICSVERSION}_Common

# Remove include directory for this module from search path.
INSTALL_INCLUDES =


$(foreach m, $(wildcard ${EPICS_MODULES}/*/*),$(eval $(patsubst $(EPICS_MODULES)/%/,%,$(dir $m))_VERSION := $(notdir $m)))

define ADD_INCLUDES_TEMPLATE
INSTALL_INCLUDES += $$(patsubst %,-I${2}/${1}/%/include,$${${1}_VERSION})
endef
$(foreach m,$(filter-out $(PRJ),$(notdir $(wildcard ${EPICS_MODULES}/*)))   ,$(eval $(call ADD_INCLUDES_TEMPLATE,$m,$(EPICS_MODULES))))

BASERULES=${EPICS_BASE}/configure/RULES

INSTALL_REV     = ${MODULE_LOCATION}
INSTALL_BIN     = ${INSTALL_REV}/bin/$(T_A)
INSTALL_LIB     = ${INSTALL_REV}/lib/$(T_A)
INSTALL_INCLUDE = ${INSTALL_REV}/include
INSTALL_DBD     = ${INSTALL_REV}/dbd
INSTALL_DB      = ${INSTALL_REV}/db
INSTALL_CFG     = ${INSTALL_REV}/cfg
INSTALL_DOC     = ${MODULE_LOCATION}/doc
INSTALL_SCR     = ${INSTALL_REV}

LIBRARY_OBJS = $(strip ${LIBOBJS} $(foreach l,${USR_LIBOBJS},$(addprefix ../,$(filter-out /%,$l))$(filter /%,$l)))

MODULELIB = $(if ${LIBRARY_OBJS},${LIB_PREFIX}${PRJ}${SHRLIB_SUFFIX},)

LIBOBJS += $(addsuffix $(OBJ),$(notdir $(basename $(filter-out %.$(OBJ) %$(LIB_SUFFIX),$(sort ${SRCS})))))
LIBOBJS += $(filter /%.$(OBJ) /%$(LIB_SUFFIX),${SRCS})
LIBOBJS += ${LIBRARIES:%=${INSTALL_LIB}/%Lib}
LIBS = -L ${EPICS_BASE_LIB} ${BASELIBS:%=-l%}
LINK.cpp += ${LIBS}
PRODUCT_OBJS = ${LIBRARY_OBJS}

# Linux
LOADABLE_LIBRARY=$(if ${LIBRARY_OBJS},${PRJ},)

# Handle registry stuff automagically if we have a dbd file.
# See ${REGISTRYFILE} rule below.
LIBOBJS += $(if $(MODULEDBD), $(addsuffix $(OBJ),$(basename ${REGISTRYFILE})))

# Create and include dependency files.
HDEPENDS = 
HDEPENDS_METHOD = COMP
HDEPENDS_COMPFLAGS = -c
MKMF = DO_NOT_USE_MKMF
CPPFLAGS += -MD
-include *.d

# Need to find source dbd files relative to one dir up but generated dbd files in this dir.
DBDFILES += ${DBD_SRCS:%=../%}
DBD_PATH = $(sort $(dir ${DBDFILES}))

DBDEXPANDPATH = $(addprefix -I , ${DBD_PATH} ${EPICS_BASE}/dbd)
USR_DBDFLAGS += $(DBDEXPANDPATH)

# Search all directories where sources or headers come from, plus existing os dependend subdirectories.
SRC_INCLUDES = $(addprefix -I, $(wildcard $(foreach d,$(call uniq, $(filter-out /%,$(dir ${SRCS:%=../%} ${HDRS:%=../%}))), $d $(addprefix $d/, os/${OS_CLASS} $(POSIX_$(POSIX)) os/default))))

# Create dbd file for snl code.
DBDFILES += $(patsubst %.st,%_snl.dbd,$(notdir $(filter %.st,${SRCS})))
DBDFILES += $(patsubst %.stt,%_snl.dbd,$(notdir $(filter %.stt,${SRCS})))

# Create dbd file for GPIB code.
DBDFILES += $(patsubst %.gt,%.dbd,$(notdir $(filter %.gt,${SRCS})))

# snc location
SNCALL=$(shell ls  -dv $(EPICS_MODULES)/sequencer/$(sequencer_VERSION)/bin/$(EPICS_HOST_ARCH) 2> /dev/null)
SNC=$(lastword $(SNCALL))/snc



ifneq ($(strip ${DBDFILES}),)
MODULEDBD=${PRJ}.dbd
endif

# If we build a library, provide a version variable.
ifneq ($(MODULELIB),)
LIBOBJS += $(addsuffix $(OBJ),$(basename ${VERSIONFILE}))
endif # MODULELIB

debug::
	@echo "===================== Pass 3: Build directory ====================="
	@echo "BUILDCLASSES = ${BUILDCLASSES}"
	@echo "OS_CLASS = ${OS_CLASS}"
	@echo "MODULEDBD = ${MODULEDBD}"
	@echo "RECORDS = ${RECORDS}"
	@echo "MENUS = ${MENUS}"
	@echo "BPTS = ${BPTS}"
	@echo "DBDINSTALLS = ${DBDINSTALLS}"
	@echo "HDRS = ${HDRS}"
	@echo "SOURCES = ${SOURCES}" 
	@echo "SOURCES_${OS_CLASS} = ${SOURCES_${OS_CLASS}}" 
	@echo "SRCS = ${SRCS}" 
	@echo "REQ = ${REQ}"
	@echo "LIBOBJS = ${LIBOBJS}"
	@echo "DBDS = ${DBDS}"
	@echo "DBDS_${OS_CLASS} = ${DBDS_${OS_CLASS}}"
	@echo "DBD_SRCS = ${DBD_SRCS}"
	@echo "DBDFILES = ${DBDFILES}"
	@echo "TEMPLS = ${TEMPLS}"
	@echo "LIBVERSION = ${LIBVERSION}"
	@echo "MODULE_LOCATION = ${MODULE_LOCATION}"

build: MODULEINFOS
build: ${MODULEDBD}
build: $(addprefix ${COMMON_DIR}/,$(addsuffix Record.h,${RECORDS}))
build: ${DEPFILE}

# Include default EPICS Makefiles (version dependent).
# Avoid library installation when doing 'make build'.
INSTALL_LOADABLE_SHRLIBS=

# We ony want to include ${BASERULES} from EPICS base if we are /not/ in debug
# mode. Including this causes all of the source files to be compiled!
ifeq (,$(findstring debug,${MAKECMDGOALS}))
include ${BASERULES}
endif

# Fix incompatible release rules.
RELEASE_DBDFLAGS = -I ${EPICS_BASE}/dbd
RELEASE_INCLUDES = -I${EPICS_BASE}/include 
# For EPICS 3.15+:
RELEASE_INCLUDES += -I${EPICS_BASE}/include/compiler/${CMPLR_CLASS}
RELEASE_INCLUDES += -I${EPICS_BASE}/include/os/${OS_CLASS}

# Find all sources and set vpath accordingly.
$(foreach file, ${SRCS} ${TEMPLS} ${DBDINSTALLS} ${SCR}, $(eval vpath $(notdir ${file}) ../$(dir ${file})))

# Do not treat %.dbd the same way because it creates a circular dependency
# if a source dbd has the same name as the project dbd. Have to clear %.dbd and not use ../ path.
# But the %Record.h and menu%.h rules need to find their dbd files (example: asyn).
vpath %.dbd
vpath %Record.dbd ${DBD_PATH}
vpath menu%.dbd ${DBD_PATH}

# Find header files to install.
vpath %.h $(addprefix ../,$(sort $(dir $(filter-out /%,${HDRS}) ${SRCS}))) $(sort $(dir $(filter /%,${HDRS})))
vpath %.hpp $(addprefix ../,$(sort $(dir $(filter-out /%,${HDRS}) ${SRCS}))) $(sort $(dir $(filter /%,${HDRS})))
vpath %.hh $(addprefix ../,$(sort $(dir $(filter-out /%,${HDRS}) ${SRCS}))) $(sort $(dir $(filter /%,${HDRS})))
vpath %.hxx $(addprefix ../,$(sort $(dir $(filter-out /%,${HDRS}) ${SRCS}))) $(sort $(dir $(filter /%,${HDRS})))


PRODUCTS = ${MODULELIB} ${MODULEDBD} ${DEPFILE} ${METAFILE}
MODULEINFOS:
	@echo ${PRJ} > MODULENAME
	@echo ${PRODUCTS} > PRODUCTS
	@echo ${LIBVERSION} > LIBVERSION

# Build one module dbd file by expanding all source dbd files.
# We can't use dbExpand (from the default EPICS make rules)
# because it has too strict checks to be used for a loadable module.
${MODULEDBD}: ${DBDFILES}
	@echo "Expanding $@"
	${MAKEHOME}expandDBD.tcl -$(basename ${EPICSVERSION}) ${DBDEXPANDPATH} $^ > $@

# Install everything.
INSTALL_LIBS = ${MODULELIB:%=${INSTALL_LIB}/%}
INSTALL_DEPS = ${DEPFILE:%=${INSTALL_LIB}/%}
INSTALL_META = ${METAFILE:%=${INSTALL_REV}/%}
INSTALL_DBDS = ${MODULEDBD:%=${INSTALL_DBD}/%}
INSTALL_DBDS += $(addprefix $(INSTALL_DBD)/,$(notdir ${DBDINSTALLS}))
ifneq ($(strip $(HDR_SUBDIRS)),)
  INSTALL_HDRS = $(addprefix ${INSTALL_INCLUDE}/,$(notdir $(filter-out $(addsuffix /%,$(HDR_SUBDIRS)),${HDRS})))
else
  INSTALL_HDRS = $(addprefix ${INSTALL_INCLUDE}/,$(notdir ${HDRS}))
endif
INSTALL_DBS  = $(addprefix ${INSTALL_DB}/,$(notdir ${TEMPLS}))
INSTALL_SCRS = $(addprefix ${INSTALL_SCR}/,$(notdir ${SCR}))
INSTALL_BINS = $(addprefix ${INSTALL_BIN}/,$(notdir ${BINS}))
INSTALL_CFGS = $(CFG:%=${INSTALL_CFG}/%)

debug::
	@echo "INSTALL_LIB = $(INSTALL_LIB)"
	@echo "INSTALL_LIBS = $(INSTALL_LIBS)"
	@echo "INSTALL_DEPS = $(INSTALL_DEPS)"
	@echo "INSTALL_META = $(INSTALL_META)"
	@echo "INSTALL_DBD = $(INSTALL_DBD)"
	@echo "INSTALL_DBDS = $(INSTALL_DBDS)"
	@echo "INSTALL_INCLUDE = $(INSTALL_INCLUDE)"
	@echo "INSTALL_HDRS = $(INSTALL_HDRS)"
	@echo "INSTALL_DB = $(INSTALL_DB)"
	@echo "INSTALL_DBS = $(INSTALL_DBS)"
	@echo "INSTALL_SCR = $(INSTALL_SCR)"
	@echo "INSTALL_SCRS = $(INSTALL_SCRS)"
	@echo "INSTALL_CFG = $(INSTALL_CFG)"
	@echo "INSTALL_CFGS = $(INSTALL_CFGS)"
	@echo "INSTALL_BIN = $(INSTALL_BIN)"
	@echo "INSTALL_BINS = $(INSTALL_BINS)"
	@echo "HDR_SUBDIRS = $(HDR_SUBDIRS)"

define install_subdirs
$1_HDRS = $$(filter $1/%,$$(HDRS))
INSTALL_HDRS += $$(addprefix $$(INSTALL_INCLUDE)/,$$($1_HDRS:$1/%=%))
vpath %.h ../$1
vpath %.hpp ../$1
vpath %.hh ../$1
vpath %.hxx ../$1
debug::
	@echo "$1_HDRS = $$($1_HDRS)"
endef
$(foreach d,$(HDR_SUBDIRS),$(eval $(call install_subdirs,$d)))

define install_subdirs
$1_HDRS = $$(filter $1/%,$$(HDRS))
INSTALL_HDRS += $$(addprefix $$(INSTALL_INCLUDE)/,$$($1_HDRS:$1/%=%))
vpath %h ../$1
debug::
	@echo "$1_HDRS = $$($1_HDRS)"
endef
$(foreach d,$(HDR_SUBDIRS),$(eval $(call install_subdirs,$d)))

install: ${INSTALLS}

${INSTALL_DBDS}: $(notdir ${INSTALL_DBDS})
	@echo "Installing module dbd file(s) $^ to $(@D)"
	$(INSTALL) -d -m$(INSTALL_PERMISSIONS) $^ $(@D)

${INSTALL_LIBS}: $(notdir ${INSTALL_LIBS})
	@echo "Installing module library $@"
	$(INSTALL) -d -m$(SHRLIB_PERMISSIONS) $< $(@D)
${INSTALL_DEPS}: $(notdir ${INSTALL_DEPS})
	@echo "Installing module dependency file $@"
	$(INSTALL) -d -m$(INSTALL_PERMISSIONS) $< $(@D)

${INSTALL_DBS}: $(notdir ${INSTALL_DBS})
	@echo "Installing module template files $^ to $(@D)"
	$(INSTALL) -d -m$(INSTALL_PERMISSIONS) $^ $(@D)

${INSTALL_SCRS}: $(notdir ${SCR})
	@echo "Installing scripts $^ to $(@D)"
	$(INSTALL) -d -m$(BIN_PERMISSIONS) $^ $(@D)

${INSTALL_CFGS}: ${CFGS}
	@echo "Installing configuration files $^ to $(@D)"
	$(INSTALL) -d -m$(INSTALL_PERMISSIONS) $^ $(@D)

${INSTALL_BINS}: $(addprefix ../,$(filter-out /%,${BINS})) $(filter /%,${BINS})
	@echo "Installing binaries $^ to $(@D)"
	$(INSTALL) -d -m$(BIN_PERMISSIONS) $^ $(@D)

# Create SNL code from st/stt file.
# Important to have %.o: %.st and %.o: %.stt rule before %.o: %.c rule!

CPPSNCFLAGS1  = $(filter -D%, ${OP_SYS_CFLAGS})
CPPSNCFLAGS1 += $(filter-out ${OP_SYS_INCLUDE_CPPFLAGS} ,${CPPFLAGS}) ${CPPSNCFLAGS}
CPPSNCFLAGS1 += -I $(dir $(SNC))../../include
SNCFLAGS += -r

%.i: %.st
	@echo ">> Preprocessing $(<F)"
	$(CPP) ${CPPSNCFLAGS1} $< > $(*F).i

%.c: %.i
	@echo ""
	@echo ">> SNC building process .... "
	@echo ">> SNC                  : $(SNC)"
	@echo ">> SNC_VERSION          : $(sequencer_VERSION)"
	@echo ">> SNC is defined as $(SNC)"
	$(SNC) $(TARGET_SNCFLAGS) $(SNCFLAGS) $(*F).i -o $(*F).c

%_snl.dbd: %.c
	@echo ">> Building $(*F)_snl.dbd"
	awk -F [\(\)]  '/epicsExportRegistrar/ { print "registrar (" $$2 ")"}' $(*F).c > $(*F)_snl.dbd

%.c: %.stt
	@echo ""
	@echo ">> SNC building process .... "
	@echo ">> SNC                  : $(SNC)"
	@echo ">> SNC_VERSION          : $(sequencer_VERSION)"
	@echo ">> SNC is defined as $(SNC)"
	$(SNC) $(TARGET_SNCFLAGS) $(SNCFLAGS) $< -o $(*F).c


# Create GPIB code from *.gt file.
%.c %.dbd %.list: %.gt
	@echo "Converting $*.gt"
	${LN} $< $(*F).gt
	gdc $(*F).gt

${VERSIONFILE}:
	echo "char _${PRJ}LibRelease[] = \"${LIBVERSION}\";" >> $@

# Create file to fill registry from dbd file.
${REGISTRYFILE}: ${MODULEDBD}
	$(PERL) $(EPICS_BASE_HOST_BIN)/registerRecordDeviceDriver.pl $< $(basename $@) | grep -v 'iocshRegisterCommon();' > $@

# Create dependency file for recursive requires.
.PHONY: ${DEPFILE}
${DEPFILE}: ${LIBOBJS} $(USERMAKEFILE)
	@echo "Collecting dependencies"
	$(RM) $@.tmp
	@echo "# Generated file. Do not edit." > $@
# Check dependencies on other module headers.
	cat *.d 2>/dev/null | sed 's/ /\n/g' | sed -n 's%$(E3_SITEMODS_PATH)/*\([^/]*\)/\($(VERSIONREGEX2)\)/.*%\1 \2%p;s%$(E3_SITEMODS_PATH)/*\([^/]*\)/\([^/]*\)/.*%\1 \2%p;s%$(E3_SITEAPPS_PATH)/*\([^/]*\)/\($(VERSIONREGEX2)\)/.*%\1 \2%p;s%$(E3_SITEAPPS_PATH)/*\([^/]*\)/\([^/]*\)/.*%\1 \2%p;s%$(EPICS_MODULES)/*\([^/]*\)/\($(VERSIONREGEX2)\)/.*%\1 \2%p;s%$(EPICS_MODULES)/*\([^/]*\)/\([^/]*\)/.*%\1 \2%p'| grep -v "include" | sort -u >> $@
ifneq ($(strip ${REQ}),)
# Manully added dependencies: ${REQ}
	@$(foreach m,${REQ},echo "$m $(or ${$m_VERSION},$(and $(wildcard ${EPICS_MODULES}/$m),$(error REQUIRED module $m has no numbered version. Set $m_VERSION)),$(warning REQUIRED module $m not found for ${T_A}.))" >> $@;)
endif

endif # In O.* directory
endif # T_A defined
