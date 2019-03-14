#!/bin/bash
#
#  Copyright (c) 2004 - 2017    Paul Scherrer Institute 
#  Copyright (c) 2017 - 2019    European Spallation Source ERIC
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
#
#  PSI original iocsh author : Dirk Zimoch
#  ESS specific iocsh author : Jeong Han Lee
#                     email  : han.lee@esss.se
#

declare -gr SC_SCRIPT="$(realpath "$0")"
declare -gr SC_SCRIPTNAME=${0##*/}
declare -gr SC_TOP="${SC_SCRIPT%/*}"
declare -g  SC_VERSION="v0.3.6-nice"
declare -g  STARTUP=""
declare -g  BASECODE=""

set -a
. ${SC_TOP}/ess-env.conf
set +a

. ${SC_TOP}/iocsh_functions

# The most unique environment variable for e3 is EPICS_DRIVER_PATH
#
if [[ $(checkIfVar ${EPICS_DRIVER_PATH}) -eq "$NON_EXIST" ]]; then
    set -a
    . ${SC_TOP}/setE3Env.bash "no_msg"
    set +a
fi

BASECODE="$(basecode_generator)"

check_mandatory_env_settings

#
# IOCSH_HASH_VERSION is defined when doing 'make install'
SC_VERSION+=-${IOCSH_HASH_VERSION}.PID-${BASHPID}

#
# We define IOCSH Git HASH + HOSTNAME + BASHPID
IOCSH_PS1=$(iocsh_ps1     "${IOCSH_HASH_VERSION}" "${BASHPID}")
REQUIRE_IOC=$(require_ioc "${IOCSH_HASH_VERSION}" "${BASHPID}")
#
# Default Initial Startup file for REQUIRE and minimal environment

IOC_STARTUP=/tmp/${SC_SCRIPTNAME}-${SC_VERSION}-startup

# To get the absolute path where iocsh.bash is executed
IOCSH_TOP=${PWD}

# EPICS_DRIVER_PATH defined in iocsh and startup.script_common
# Remember, driver is equal to module, so EPICS_DRIVER_PATH is the module directory
# In our jargon. It is the same as ${EPICS_MODULES}

trap "softIoc_end ${IOC_STARTUP}" EXIT HUP INT TERM


{
    printIocEnv;
    printf "# Set REQUIRE_IOC for its internal PVs\n";
    printf "epicsEnvSet REQUIRE_IOC \"${REQUIRE_IOC}\"\n";
    printf "#\n";
    printf "# Set E3_IOCSH_TOP for the absolute path where %s is executed.\n" "${SC_SCRIPTNAME}"
    printf "epicsEnvSet E3_IOCSH_TOP \"${IOCSH_TOP}\"\n";
    printf "#\n";
    
    loadRequire;

    loadFiles "$@";

    printf "# Set the IOC Prompt String One \n";
    printf "epicsEnvSet IOCSH_PS1 \"$IOCSH_PS1\"\n";
    printf "#\n";
    

    if [ "$init" != NO ]; then
	printf "# \n";
	printf "iocInit\n"
    fi
    
}  > ${IOC_STARTUP}

ulimit -c unlimited

# -x "PREFIX"
# PREFIX:exit & PREFIX:BaseVersion PVs are added to softIoc
# We can end this IOC via caput PREFIX:exit 1


if [[ ${BASECODE} -ge  07000101 ]]; then
    _PVA_="PVA"
else
    _PVA_=""
fi


nice --10 softIoc${_PVA_} -D ${EPICS_BASE}/dbd/softIoc${_PVA_}.dbd "${IOC_STARTUP}" 2>&1

