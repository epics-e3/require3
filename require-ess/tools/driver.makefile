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
# LIBVERSION is set to "dev" if not overwritten.
# The library is installed to ${EPICS_MODULES}/${MODULE}/${LIBVERSION}/lib/${T_A}/.
# A module can be loaded with  require "<module>" [,"<version>"] [,"<variable>=<substitution>, ..."]
#
# User variables (add them to your Makefile, none is required):
# MODULE
#    Name of the built module.
#    If not defined, it is derived from the directory name.
# SOURCES
#    All source files to compile.
# DBDS
#    All dbd files of the project.
#    If not defined, default is all *.dbd files in the source directory.
# HEADERS
#    Header files to install (e.g. to be included by other drivers)
#    If not defined, all headers are for local use only.
# EXCLUDE_ARCH
#    Sub set of architectures to exclude for, e.g. ppc604; note that these will be wildcarded
#    as %ppc604 and ppc604%.

# Get the location of this file.
MAKEHOME:=$(dir $(lastword ${MAKEFILE_LIST}))
# Get the name of the Makefile that included this file.
USERMAKEFILE:=$(lastword $(filter-out $(lastword ${MAKEFILE_LIST}), ${MAKEFILE_LIST}))

# These are the targets that we will pass through to the next stages of require's
# recursive build process. For each of these targets we will perform all three
# of the build runs listed above; for others (e.g. `make clean`) we only perform
# a single pass.
RECURSE_TARGETS = install build debug

##---## In conda, We only use one version of EPICS base when compiling modules.
##---## EPICS_BASE / EPICS_BASE_VERSION / EPICS_MODULES are set as environment variables by conda
MSI = ${EPICS_BASE_HOST_BIN}/msi
CONFIG=${EPICS_BASE}/configure

# Set LIBVERSION to dev if not set
LIBVERSION := $(or $(LIBVERSION),dev)
EPICSVERSION:=$(EPICS_BASE_VERSION)

BUILDCLASSES = Linux Darwin
OS_CLASS_LIST = $(BUILDCLASSES)

# $PREFIX can be used to refer to dependencies installed by conda
# (like -I$(PREFIX)/include/libxml2)
# Set PREFIX to
# - PREFIX if set (when using conda-build)
# - CONDA_PREFIX otherwise (when compiling locally in a conda env)
PREFIX := $(or $(PREFIX),$(CONDA_PREFIX))

MODULE=
PROJECT=
PRJ := $(strip $(or ${MODULE},${PROJECT}))

MODULE_LOCATION = $(PREFIX)/epics-modules/$(PRJ)

# Override config here:
-include ${MAKEHOME}/config

# Some shell commands:
RMDIR = rm -rf
LN = ln -s
RM = rm -f
CP = cp
MKDIR = mkdir -p -m 775

# Some generated file names:
REGISTRYFILE = ${PRJ}_registerRecordDeviceDriver.cpp
DEPFILE = ${PRJ}.dep
VERSIONFILE = ${PRJ}_version

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

# This is used to ensure that relative/absolute paths in e.g. USR_DBFLAGS are
# correct, relative to the build directory.
define fix_relative_paths
$(foreach t,$(patsubst -I%,-I %,$1),$(if $(filter -I,$t),$t,$(if $(filter /%,$t),$t,../$t)))
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

# This is (at the moment) only used for a single module. If LEGACY_RSET is defined then
# we use the _old_ untyped `struct rset` definitions for record device support. Otherwise,
# we use the updated `struct typed_rset` ones. This helps remove some compiler warnings.
ifndef LEGACY_RSET
USR_CPPFLAGS+=-DUSE_TYPED_RSET
endif

SHRLIB_VERSION=
# Avoid linking everything with libreadline.so
COMMANDLINE_LIBRARY =

OBJ=.o

COMMON_DIR = O.${EPICSVERSION}_Common

ifndef T_A

where_am_I:=$(abspath $(CURDIR))/
## RUN 1
# Target achitecture not yet defined, but EPICSVERSION is already known.
# Still in source directory.

# Look for sources etc., and select target architectures to build.
# Export everything for second run:
SRCS = ${SOURCES}
export SRCS

DBD_SRCS = $(if ${DBDS},$(filter-out -none-,${DBDS}),$(wildcard menu*.dbd *Record.dbd) $(strip $(filter-out %Include.dbd dbCommon.dbd %Record.dbd,$(wildcard *.dbd)) ${BPTS}))
DBD_SRCS += ${DBDS_${EPICSVERSION}}
export DBD_SRCS

# Read dbd files from source files. Note that this assumes that any xxxRecord.(c|cpp|...) has
# a corresponding xxxRecord.dbd, which is used to generate xxxRecord.h; this is standard usage
# in EPICS. However, if such a .dbd file does not exist, then the build will fail due to the
# "missing" header file.
RECORDS = $(filter %Record,$(basename $(notdir $(SRCS))))
export RECORDS

MENUS = $(patsubst %.dbd,%.h,$(wildcard menu*.dbd))
export MENUS

BPTS = $(patsubst %.data,%.dbd,$(wildcard bpt*.data))
export BPTS

DBDINSTALLS = $(DBD_INSTALLS)
DBDINSTALLS += $(MENUS)
DBDINSTALLS += $(BPTS)
export DBDINSTALLS

HDRS = ${HEADERS}
HDRS += $(RECORDS:%=${COMMON_DIR}/%.h)
HDRS += ${HEADERS_${EPICSVERSION}}
export HDRS

HDR_SUBDIRS = $(KEEP_HEADER_SUBDIRS)
export HDR_SUBDIRS

TEMPLS = $(if ${TEMPLATES},$(filter-out -none-,${TEMPLATES}),$(wildcard *.template *.db *.subs))
TEMPLS += ${TEMPLATES_${EPICSVERSION}}
TEMPLS += $(wildcard $(COMMON_DIR)/*.db)
export TEMPLS

CFGS = ${CONFIGS}
CFGS += ${CONFIGS_${EPICSVERSION}}
export CFGS

SCR = $(if ${SCRIPTS},$(filter-out -none-,${SCRIPTS}),$(wildcard *.cmd *.iocsh))
SCR += ${SCRIPTS_${EPICSVERSION}}
export SCR

INSTALL_LICENSE = ${MODULE_LOCATION}/doc
# Find all license files to distribute with binaries
LICENSES = $(shell find . -not -path '*/.*' -type f -iname LICENSE)
LICENSES += $(shell find . -not -path '*/.*' -type f -iname Copyright)
export LICENSES

# Filter architectures to build using EXCLUDE_ARCHS.
ALL_ARCHS = ${EPICS_HOST_ARCH} ${CROSS_COMPILER_TARGET_ARCHS}
BUILD_ARCHS = $(filter-out $(addprefix %,${EXCLUDE_ARCHS}),\
              $(filter-out $(addsuffix %,${EXCLUDE_ARCHS}),${ALL_ARCHS}))

SRCS_Linux = ${SOURCES_Linux}
export SRCS_Linux

$(RECURSE_TARGETS)::
	@echo "MAKING EPICS VERSION ${EPICSVERSION}"

build:: $(COMMON_DIR)

debug::
	@echo "===================== Pass 1 ====================="
	@echo "BUILDCLASSES = ${BUILDCLASSES}"
	@echo "LIBVERSION = ${LIBVERSION}"
	@echo "PRJ = ${PRJ}"
	@echo "EPICS_BASE = ${EPICS_BASE}"
	@echo "BUILD_ARCHS = ${BUILD_ARCHS}"
	@echo "EXCLUDE_ARCHS = ${EXCLUDE_ARCHS}"
	@echo "LIBVERSION = ${LIBVERSION}"
	@echo "EPICS_MODULES = ${EPICS_MODULES}"
	@echo "LICENSES = ${LICENSES}"

# Create e.g. build-$(T_A) rules for each architecture, so that we can just do
#   build: build-arch1 build-arch2
define target_rule
$1-%:
	$${MAKE} -f $${USERMAKEFILE} T_A=$$* $1
endef
$(foreach target,$(RECURSE_TARGETS),$(eval $(call target_rule,$(target))))

.SECONDEXPANSION:

# This has to be after .SECONDEXPANSION since BUILD_ARCHS will be modified based on EXCLUDE_ARCHS
# which is defined _after_ driver.makefile.
$(foreach target,$(RECURSE_TARGETS),$(eval $(target):: $$$$(foreach arch,$$$${BUILD_ARCHS},$(target)-$$$${arch})))

else # T_A

ifeq ($(filter O.%,$(notdir ${CURDIR})),)
where_am_I:=$(abspath $(CURDIR))/

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

$(RECURSE_TARGETS):: O.${EPICSVERSION}_${T_A}
	@${MAKE} -C O.${EPICSVERSION}_${T_A} -f ../${USERMAKEFILE} $@

endif

SRCS += $(foreach x, ${VAR_EXTENSIONS}, ${SOURCES_$x})
USR_LIBOBJS += ${LIBOBJS} $(foreach x,${VAR_EXTENSIONS},${LIBOBJS_$x})
export USR_LIBOBJS

BINS += $(foreach x, ${VAR_EXTENSIONS}, ${BINS_$x})
export BINS

export USR_DBFLAGS
export TMPS
export SUBS

else # in O.*
where_am_I:=$(abspath $(CURDIR)/..)/

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
$(foreach m,$(filter-out $(PRJ),$(notdir $(wildcard ${PREFIX}/epics/*))) ,$(eval $(call ADD_INCLUDES_TEMPLATE,$m,$(PREFIX)/epics)))

BASERULES=${EPICS_BASE}/configure/RULES

INSTALL_REV     = ${MODULE_LOCATION}
INSTALL_BIN     = ${PREFIX}/bin
INSTALL_LIB     = ${PREFIX}/lib
INSTALL_INCLUDE = ${PREFIX}/include
INSTALL_DEP     = ${INSTALL_REV}
INSTALL_DBD     = ${INSTALL_REV}/dbd
INSTALL_DB      = ${INSTALL_REV}/db
INSTALL_CONFIG  = ${INSTALL_REV}/cfg
INSTALL_DOC     = ${INSTALL_REV}/doc
INSTALL_SCR     = ${INSTALL_REV}

LIBRARY_OBJS = $(strip ${LIBOBJS} $(foreach l,${USR_LIBOBJS},$(addprefix ../,$(filter-out /%,$l))$(filter /%,$l)))

MODULELIB = $(if ${LIBRARY_OBJS},${LIB_PREFIX}${PRJ}${SHRLIB_SUFFIX},)

# Handle registry stuff automagically if we have a dbd file.
# See ${REGISTRYFILE} rule below.
LIBOBJS += $(if $(MODULEDBD), $(addsuffix $(OBJ),$(basename ${REGISTRYFILE})))


LIBOBJS += $(addsuffix $(OBJ),$(notdir $(basename $(filter-out %.$(OBJ) %$(LIB_SUFFIX),$(sort ${SRCS})))))
LIBOBJS += $(filter /%.$(OBJ) /%$(LIB_SUFFIX),${SRCS})
LIBOBJS += ${LIBRARIES:%=${INSTALL_LIB}/%Lib}
LIBS = -L ${EPICS_BASE_LIB} ${BASELIBS:%=-l%}
LINK.cpp += ${LIBS}
PRODUCT_OBJS = ${LIBRARY_OBJS}

# Linux
LOADABLE_LIBRARY=$(if ${LIBRARY_OBJS},${PRJ},)
# Create and include dependency files.
HDEPENDS =
HDEPENDS_METHOD = COMP
HDEPENDS_COMPFLAGS = -c
MKMF = DO_NOT_USE_MKMF
CPPFLAGS += -MD
CPPFLAGS += -DMODULE_NAME='"${MODULE}"' -DLIBVERSION='"${LIBVERSION}"'
CXXFLAGS += -I$(E3_REQUIRE_TOOLS)/
-include *.d

# Need to find source dbd files relative to one dir up but generated dbd files in this dir.
DBDFILES += ${DBD_SRCS:%=../%}
DBD_PATH = $(sort $(dir ${DBDFILES}))

DBDEXPANDPATH = $(addprefix -I , ${DBD_PATH} ${EPICS_BASE}/dbd)
USR_DBDFLAGS += $(DBDEXPANDPATH)

# Search all directories where sources or headers come from, plus existing os dependend subdirectories.
SRC_INCLUDES = $(addprefix -I, $(wildcard $(foreach d,$(call uniq, $(filter-out /%,$(dir ${SRCS:%=../%} ${HDRS:%=../%}))), $d $(addprefix $d/, os/${OS_CLASS} $(POSIX_$(POSIX)) os/default))))

MODULE_CONFIGS = ${CFGS:%=../%}
MODULE_CONFIGS += $(foreach m,$(filter-out $(PRJ),$(notdir $(wildcard ${EPICS_MODULES}/*))),$(wildcard ${EPICS_MODULES}/$m/$($(m)_VERSION)/cfg/CONFIG*))

MODULE_RULES = ${CFGS:%=../%}
MODULE_RULES += $(foreach m,$(filter-out $(PRJ),$(notdir $(wildcard ${EPICS_MODULES}/*))),$(wildcard ${EPICS_MODULES}/$m/$($(m)_VERSION)/cfg/RULES*))


# We ony want to include ${BASERULES} from EPICS base if we are /not/ in debug
# mode. Including this causes all of the source files to be compiled!
ifeq (,$(findstring debug,${MAKECMDGOALS}))
  ifneq ($(strip $(MODULE_CONFIGS)),)
    include $(MODULE_CONFIGS)
  endif
endif

ifneq ($(strip ${DBDFILES}),)
MODULEDBD=${PRJ}.dbd
endif

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
	@echo "CFGS = ${CFGS}"
	@echo "LIBOBJS = ${LIBOBJS}"
	@echo "DBDS = ${DBDS}"
	@echo "DBDS_${OS_CLASS} = ${DBDS_${OS_CLASS}}"
	@echo "DBD_SRCS = ${DBD_SRCS}"
	@echo "DBDFILES = ${DBDFILES}"
	@echo "TEMPLS = ${TEMPLS}"
	@echo "LIBVERSION = ${LIBVERSION}"
	@echo "MODULE_LOCATION = ${MODULE_LOCATION}"
	@echo "MODULE_CONFIGS = ${MODULE_CONFIGS}"
	@echo "MODULE_RULES = ${MODULE_RULES}"

build: MODULEINFOS
build: ${MODULEDBD}
build: ${DEPFILE}
build: db_internal
build: ${VERSIONFILE}

db_internal:

COMMON_INC = ${RECORDS:%=${COMMON_DIR}/%.h}

# Include default EPICS Makefiles (version dependent).
# Avoid library installation when doing 'make build'.
INSTALL_LOADABLE_SHRLIBS=

# We ony want to include ${BASERULES} from EPICS base if we are /not/ in debug
# mode. Including this causes all of the source files to be compiled!
ifeq (,$(findstring debug,${MAKECMDGOALS}))
  include ${BASERULES}
  ifneq ($(strip $(MODULE_RULES)),)
    include $(MODULE_RULES)
  endif
endif

DBDEPENDS_FILES = $(wildcard $(COMMON_DIR)/*.db.d)
ifneq (,$(DBDEPENDS_FILES))
-include $(DBDEPENDS_FILES)
endif

USR_DBFLAGS := $(call fix_relative_paths,$(USR_DBFLAGS))

define SUBS_EXPAND
db_internal: $(COMMON_DIR)/$(notdir $(basename $2).db)

# Note that this rule overrides the one from RULES.Db from EPICS_BASE
$(COMMON_DIR)/$(notdir $(basename $2).db): $(if $(filter /%,$2),$2,../$2)
	@printf "Inflating database ... %44s >>> %40s \n" "$$<" "$$@"
	$(MSI) -D $$(USR_DBFLAGS) -o $(COMMON_DIR)/$$(notdir $$(basename $2).db) $1 $$< > $(COMMON_DIR)/$$(notdir $$(basename $2).db).d
	$(MSI)    $$(USR_DBFLAGS) -o $(COMMON_DIR)/$$(notdir $$(basename $2).db) $1 $$<
endef

$(foreach file,$(TMPS),$(eval $(call SUBS_EXPAND,,$(file))))
$(foreach file,$(SUBS),$(eval $(call SUBS_EXPAND,-S,$(file))))

# Fix incompatible release rules.
RELEASE_DBDFLAGS = -I ${EPICS_BASE}/dbd
RELEASE_INCLUDES = -I${EPICS_BASE}/include
# For EPICS 3.15+:
RELEASE_INCLUDES += -I${EPICS_BASE}/include/compiler/${CMPLR_CLASS}
RELEASE_INCLUDES += -I${EPICS_BASE}/include/os/${OS_CLASS}

# Find all sources and set vpath accordingly.
$(foreach file, ${SRCS} ${TEMPLS} ${DBDINSTALLS} ${SCR} ${CFGS}, $(eval vpath $(notdir ${file}) ../$(dir ${file})))

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


PRODUCTS = ${MODULELIB} ${MODULEDBD} ${DEPFILE} ${VERSIONFILE}
MODULEINFOS:
	@echo ${PRJ} > MODULENAME
	@echo ${PRODUCTS} > PRODUCTS
	@echo ${LIBVERSION} > LIBVERSION

# Build one module dbd file by expanding all source dbd files.
${MODULEDBD}: ${DBDFILES}
	@echo "Expanding $@"
	${PERL} ${EPICS_BASE_HOST_BIN}/dbdExpand.pl -A ${DBDEXPANDPATH} -o $@ $^

# Install everything.
INSTALL_LIBS = ${MODULELIB:%=${INSTALL_LIB}/%}
INSTALL_DEPS = ${DEPFILE:%=${INSTALL_DEP}/%}
INSTALL_VERSION = ${VERSIONFILE:%=${INSTALL_DEP}/%}
INSTALL_DBDS = ${MODULEDBD:%=${INSTALL_DBD}/%}
# append project names
INSTALL_DBDS += $(addprefix $(INSTALL_DBD)/,$(notdir ${DBDINSTALLS}))
ifneq ($(strip $(HDR_SUBDIRS)),)
  INSTALL_HDRS = $(addprefix ${INSTALL_INCLUDE}/,$(notdir $(filter-out $(addsuffix /%,$(HDR_SUBDIRS)),${HDRS})))
else
  INSTALL_HDRS = $(addprefix ${INSTALL_INCLUDE}/,$(notdir ${HDRS}))
endif
INSTALL_DBS  = $(addprefix ${INSTALL_DB}/,$(notdir ${TEMPLS}))
INSTALL_SCRS = $(addprefix ${INSTALL_SCR}/,$(notdir ${SCR}))
INSTALL_BINS = $(addprefix ${INSTALL_BIN}/,$(notdir ${BINS}))
INSTALL_CONFIGS = $(addprefix ${INSTALL_CONFIG}/,$(notdir ${CFGS}))
INSTALL_LICENSES = $(addprefix ${INSTALL_DOC}/,${LICENSES})

debug::
	@echo "MODULELIB = $(MODULELIB)"
	@echo "INSTALL_LIB = $(INSTALL_LIB)"
	@echo "INSTALL_LIBS = $(INSTALL_LIBS)"
	@echo "INSTALL_DEPS = $(INSTALL_DEPS)"
	@echo "INSTALL_DBD = $(INSTALL_DBD)"
	@echo "INSTALL_DBDS = $(INSTALL_DBDS)"
	@echo "INSTALL_INCLUDE = $(INSTALL_INCLUDE)"
	@echo "INSTALL_HDRS = $(INSTALL_HDRS)"
	@echo "INSTALL_DB = $(INSTALL_DB)"
	@echo "INSTALL_DBS = $(INSTALL_DBS)"
	@echo "INSTALL_SCR = $(INSTALL_SCR)"
	@echo "INSTALL_SCRS = $(INSTALL_SCRS)"
	@echo "INSTALL_CONFIG = $(INSTALL_CONFIG)"
	@echo "INSTALL_CONFIGS = $(INSTALL_CONFIGS)"
	@echo "INSTALL_BIN = $(INSTALL_BIN)"
	@echo "INSTALL_BINS = $(INSTALL_BINS)"
	@echo "INSTALL_LICENSES = $(INSTALL_LICENSES)"
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

INSTALLS += ${INSTALL_CONFIGS} ${INSTALL_SCRS} ${INSTALL_HDRS} ${INSTALL_DBDS} ${INSTALL_DBS} \
            ${INSTALL_LIBS} ${INSTALL_VLIBS} ${INSTALL_BINS} ${INSTALL_DEPS} ${INSTALL_VERSION} \
            ${INSTALL_LICENSES}

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

${INSTALL_VERSION}: $(notdir ${INSTALL_VERSION})
	@echo "Installing module dependency file $@"
	$(INSTALL) -d -m$(INSTALL_PERMISSIONS) $< $(@D)

${INSTALL_DBS}: $(notdir ${INSTALL_DBS})
	@echo "Installing module template files $^ to $(@D)"
	$(INSTALL) -d -m$(INSTALL_PERMISSIONS) $^ $(@D)

${INSTALL_SCRS}: $(notdir ${SCR})
	@echo "Installing scripts $^ to $(@D)"
	$(INSTALL) -d -m$(BIN_PERMISSIONS) $^ $(@D)

${INSTALL_CONFIGS}: $(notdir ${INSTALL_CONFIGS})
	@echo "Installing configuration files $^ to $(@D)"
	$(INSTALL) -d -m$(INSTALL_PERMISSIONS) $^ $(@D)

${INSTALL_BINS}: $(addprefix ../,$(filter-out /%,${BINS})) $(filter /%,${BINS})
	@echo "Installing binaries $^ to $(@D)"
	$(INSTALL) -d -m$(BIN_PERMISSIONS) $^ $(@D)

define license_install
$1: $2
	@echo "Installing license file $$^"
	$$(INSTALL) -d -m444 $$^ $$(@D)
endef
$(foreach l,$(LICENSES),$(eval $(call license_install,$(INSTALL_DOC)/$l,../$l)))

# Create GPIB code from *.gt file.
%.c %.dbd %.list: %.gt
	@echo "Converting $*.gt"
	${LN} $< $(*F).gt
	gdc $(*F).gt

# Create file to fill registry from dbd file. Because of c++ static
# initialization order fiasco all static initialization needs to be
# in a single function. Therefore the last line of
# module_registerRecordDeviceDriver.cpp, that would run Registration()
# for initialization, is removed. Then init.cpp is appended and
# __module_library_init() is initializes the library and calls
# Registration().
${REGISTRYFILE}: ${MODULEDBD}
	$(PERL) $(EPICS_BASE_HOST_BIN)/registerRecordDeviceDriver.pl $< $(basename $@) | grep -v 'iocshRegisterCommon();' > $@
	sed -i'.bak' '$$d' $@
	echo "#include <init.cpp>" >> $@

# Create dependency file for recursive requires.
.PHONY: ${DEPFILE} ${VERSIONFILE}
${DEPFILE}: ${LIBOBJS} $(USERMAKEFILE)
	@echo "Collecting dependencies"
	$(RM) $@.tmp
	@echo "# Generated file. Do not edit." > $@
# Check dependencies on other module headers.
	cat *.d 2>/dev/null | sed 's/ /\n/g' | sed -n 's%$(EPICS_MODULES)/*\([^/]*\)/\([0-9]*\.[0-9]*\.[0-9]*\)/.*%\1 \2%p;s%$(EPICS_MODULES)/*\([^/]*\)/\([^/]*\)/.*%\1 \2%p'| grep -v "include" | sort -u > $@.tmp
# Manully added dependencies: ${REQ}
	@$(foreach m,${REQ},echo "$m $($m_VERSION)" >> $@.tmp;)
	cat $@.tmp | sort -u >> $@

${VERSIONFILE}: ${USERMAKEFILE}
	@echo "$(LIBVERSION)" > $@

endif # In O.* directory
endif # T_A defined
