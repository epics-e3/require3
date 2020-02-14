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

declare -r SC_SCRIPT="$(readlink -e "$0")"
declare -r SC_SCRIPTNAME=${0##*/}
declare -r SC_TOP="${SC_SCRIPT%/*}"
declare SC_VERSION="${E3_REQUIRE_VERSION}-gdb"
declare STARTUP=""
declare BASECODE=""


. ${SC_TOP}/iocsh_functions


BASECODE="$(basecode_generator)"

check_mandatory_env_settings

# ${BASHPID} returns iocsh.bash PID
iocsh_bash_id=${BASHPID}
#
SC_VERSION+=-PID-${iocsh_bash_id}

#
# We define HOSTNAME + iocsh_bash_id
IOCSH_PS1=$(iocsh_ps1     "${iocsh_bash_id}")
REQUIRE_IOC=$(require_ioc "${iocsh_bash_id}")
#
# Default Initial Startup file for REQUIRE and minimal environment
IOC_STARTUP=$(mktemp -q --suffix=_iocsh_${SC_VERSION}) || die 1 "${SC_SCRIPTNAME} CANNOT create the startup file, please check the disk space";

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


gdb --eval-command run --args softIoc${_PVA_} -D ${EPICS_BASE}/dbd/softIoc${_PVA_}.dbd "${IOC_STARTUP}" 2>&1

