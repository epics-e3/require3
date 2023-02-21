# -*- mode: sh -*-
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
#

EXIST=1
NON_EXIST=0
REALTIME=
__LOADER__=

function pushd() { builtin pushd "$@" >/dev/null; }
function popd() { builtin popd "$@" >/dev/null; }

function checkIfVar
{
    #@ Usage Example :
    # if [[ $(checkIfVar ${!var}) -eq "$NON_EXIST" ]]; then
    #    die 1 " $var is not defined!. Please run conda activate <env> "
    # fi
    local var=$1
    local result=""
    if [ -z "$var" ]; then
        result=$NON_EXIST
        # doesn't exist
    else
        result=$EXIST
        # exist
    fi
    echo "${result}"
}



function read_file_get_string {
  local FILENAME=$1
  local PREFIX=$2

  sed -n "s/^$PREFIX\(.*\)/\1/p" "$FILENAME"
}

# Base Code is defined with 8 digits numbers
# Two digits are enough to cover all others (I think)
# 7.0.6.1   : 07000601
#
# First  Two : 00 (EPICS VERSION)
# Second Two : 00 (EPICS_REVISION)
# Third  Two : 00 (EPICS_MODIFICATION)
# Fourth Two : 00 (EPICS_PATCH_LEVEL)

function basecode_generator() { #@ Generator BASECODE
  #@ USAGE: BASECODE=$(basecode_generator)

  local epics_ver_maj epics_ver_mid epics_ver_min epics_ver_patch
  epics_ver_maj="$(read_file_get_string "${EPICS_BASE}/configure/CONFIG_BASE_VERSION" "EPICS_VERSION = ")"
  epics_ver_mid="$(read_file_get_string "${EPICS_BASE}/configure/CONFIG_BASE_VERSION" "EPICS_REVISION = ")"
  epics_ver_min="$(read_file_get_string "${EPICS_BASE}/configure/CONFIG_BASE_VERSION" "EPICS_MODIFICATION = ")"
  epics_ver_patch="$(read_file_get_string "${EPICS_BASE}/configure/CONFIG_BASE_VERSION" "EPICS_PATCH_LEVEL = ")"

  local base_code=""

  if [[ ${#epics_ver_maj} -lt 2 ]]; then
    epics_ver_maj="00${epics_ver_maj}"
    epics_ver_maj="${epics_ver_maj: -2}"
  fi

  if [[ ${#epics_ver_mid} -lt 2 ]]; then
    epics_ver_mid="00${epics_ver_mid}"
    epics_ver_mid="${epics_ver_mid: -2}"
  fi

  if [[ ${#epics_ver_min} -lt 2 ]]; then
    epics_ver_min="00${epics_ver_min}"
    epics_ver_min="${epics_ver_min: -2}"
  fi

  if [[ ${#epics_ver_patch} -lt 2 ]]; then
    epics_ver_patch="00${epics_ver_patch}"
    epics_ver_patch="${epics_ver_patch: -2}"
  fi

  base_code=${epics_ver_maj}${epics_ver_mid}${epics_ver_min}${epics_ver_patch}

  echo "$base_code"
}

function version() {
  printf "%s : %s%s\n" "European Spallation Source ERIC" "$SC_SCRIPTNAME" ${SC_VERSION:+" ($SC_VERSION)"} >&2

  exit
}

function print_iocsh_header() {

  printf "███████╗██████╗     ██╗ ██████╗  ██████╗    ███████╗██╗  ██╗███████╗██╗     ██╗     \n"
  printf "██╔════╝╚════██╗    ██║██╔═══██╗██╔════╝    ██╔════╝██║  ██║██╔════╝██║     ██║     \n"
  printf "█████╗   █████╔╝    ██║██║   ██║██║         ███████╗███████║█████╗  ██║     ██║     \n"
  printf "██╔══╝   ╚═══██╗    ██║██║   ██║██║         ╚════██║██╔══██║██╔══╝  ██║     ██║     \n"
  printf "███████╗██████╔╝    ██║╚██████╔╝╚██████╗    ███████║██║  ██║███████╗███████╗███████╗\n"
  printf "╚══════╝╚═════╝     ╚═╝ ╚═════╝  ╚═════╝    ╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝\n"
  printf "\n"

}

function print_header_line() {
  printf "############################################################################\n"
  printf "##  %s\n" "${1}"
  printf "############################################################################\n"
}

function print_vars() {
  print_header_line "$1"
  shift
  for var in "$@"; do
    printf "# %s = \"%s\"\n" "${var}" "${!var}"
  done
  printf "#\n"
}

function printIocEnv() {

  printf "# Start at \"%s\"\n" "$(date +%Y-W%V-%b%d-%H%M-%S-%Z)"
  printf "# %s : %s%s\n" "European Spallation Source ERIC" "$SC_SCRIPTNAME" ${SC_VERSION:+" ($SC_VERSION)"}
  printf "#\n"

  print_vars "Shell and environment variables" PWD USER LOGNAME PATH
  print_vars "EPICS variables" EPICS_BASE EPICS_HOST_ARCH EPICS_DRIVER_PATH EPICS_CA_AUTO_ADDR_LIST EPICS_CA_ADDR_LIST EPICS_PVA_AUTO_ADDR_LIST EPICS_PVA_ADDR_LIST
  print_vars "e3-specific variables" E3_REQUIRE_VERSION E3_REQUIRE_LOCATION E3_REQUIRE_BIN E3_REQUIRE_DB E3_REQUIRE_DBD E3_REQUIRE_INC E3_REQUIRE_LIB

}

# Ctrl+c : OK
# exit   : OK
# kill softioc process : OK
# kill main precess : Enter twice in terminal,
#                     close softIoc, but STATUP file is remained.
#

function softIoc_end() {
  local startup_file=$1
  rm -f "${startup_file}"
  # only clean terminal when stdout is opened on a terminal
  # avoid "stty: standard input: Inappropriate ioctl for device" otherwise
  [[ -t 1 ]] && stty sane
  exit
}

function die() { #@ Print error message and exit with error code
  #@ USAGE: die [errno [message]]
  error=${1:-1}
  ## exits with 1 if error number not given
  shift
  [ -n "$*" ] &&
    printf "%s%s: %s\n" "$SC_SCRIPTNAME" ${SC_VERSION:+" ($SC_VERSION)"} "$*" >&2
  exit "$error"
}

function iocsh_ps1() {
  local iocsh_ps1=""
  local pid="$1"

  # If IOCNAME is not set use pid instead
  if [ -z "${IOCNAME}" ]; then
    iocsh_ps1+=${pid}
  else
    iocsh_ps1+="${IOCNAME}"
  fi

  iocsh_ps1+=" > "

  echo "${iocsh_ps1}"
}

# Please look at the limitation in require.c in  registerModule()
# /*
#    Require DB has the following four PVs:
#    - $(REQUIRE_IOC):$(MODULE)_VER
#    - $(REQUIRE_IOC):MOD_VER
#    - $(REQUIRE_IOC):VERSIONS
#    - $(REQUIRE_IOC):MODULES
#    We reserved 30 chars for :$(MODULE)_VER, so MODULE has the maximum 24 chars.
#    And we've reserved for 30 chars for $(REQUIRE_IOC).
#    So, the whole PV and record name in moduleversion.template has 59 + 1.
#  */


function require_ioc() {
  # e3-ioc-hash-hostname-pid fails when host has icslab-ser03 and IOCUSER-VIRTUALBOX
  # so better to keep simple in case when hostname is long.
  # And it has the limitation of PV length
  #  #define PVNAME_STRINGSZ 61 in EPICS_BASE/include/dbDefs.h

  local require_ioc=""

  # Test if IOCNAME is defined
  if [ -z "${IOCNAME}" ]; then
    local pid="$1"
    # Keep only short hostname (without domain)
    local hostname=${HOSTNAME%%.*}
    # Record name should not have . character, because it is used inside record

    require_ioc="REQMOD"          # char 6  ( 6)
    require_ioc+=":"              # char 1  ( 7)
    require_ioc+=${hostname:0:15} # char 15 (22)
    require_ioc+="-"              # char 1  (23)
    require_ioc+=${pid}           # char 7  (30),  max pid in  64 bit  4194304 (7),
  else
    require_ioc=${IOCNAME:0:30}
  fi

  echo "${require_ioc}"
}

function loadRequire() {
  local libPrefix=lib
  local libPostfix=.so
  local libName=${libPrefix}${E3_REQUIRE_NAME}${libPostfix}

  local require_lib=${E3_REQUIRE_LIB}/${EPICS_HOST_ARCH}/${libName}
  local require_dbd=${E3_REQUIRE_DBD}/${E3_REQUIRE_NAME}.dbd

  printf "# // Load %s module, version %s\n" "${E3_REQUIRE_NAME}" "${E3_REQUIRE_VERSION}"
  printf "#\n"
  printf "dlload %s\n" "${require_lib}"
  printf "dbLoadDatabase %s\n" "${require_dbd}"
  printf "%s_registerRecordDeviceDriver\n\n" "${E3_REQUIRE_NAME%-*}"
  printf "# \n"

}

function check_mandatory_env_settings(){
  declare -a var_list=()
  var_list+=(EPICS_HOST_ARCH)
  var_list+=(EPICS_BASE)
  var_list+=(E3_REQUIRE_NAME)
  var_list+=(E3_REQUIRE_BIN)
  var_list+=(E3_REQUIRE_LIB)
  var_list+=(E3_REQUIRE_DB)
  var_list+=(E3_REQUIRE_DBD)
  var_list+=(E3_REQUIRE_VERSION)
  for var in ${var_list[@]};  do
	  if [[ $(checkIfVar ${!var}) -eq "$NON_EXIST" ]]; then
	    die 1 " $var is not defined!. Please run conda activate <env> "
	  fi
  done

  if [[ -z "$IOCNAME" ]]; then
    echo "Warning: environment variable IOCNAME is not set." >&2
  else
    echo "IOCNAME is set to $IOCNAME"
  fi
};

function setPaths() {
  while [ "$#" -gt 0 ]; do
    arg="$1"

    case $arg in
      -l)
        shift
        add_path="$1/$(basename "${EPICS_BASE}")/require-${E3_REQUIRE_VERSION}"
        printf "epicsEnvSet EPICS_DRIVER_PATH %s:${EPICS_DRIVER_PATH}\n" "$add_path"
        EPICS_DRIVER_PATH="$add_path:$EPICS_DRIVER_PATH"
        ;;
    esac
    shift
  done
}

function loadFiles() {
  while [ "$#" -gt 0 ]; do

    arg=$1

    case $arg in
      -rt | -RT | -realtime | --realtime)
        REALTIME="RT"
        __LOADER__="chrt --fifo 1 "
        ;;
      @*)
        loadFiles "$(cat "${arg#@}")"
        ;;
      *=*)
        echo -n "$arg" | awk -F '=' '{printf "epicsEnvSet %s '\''%s'\''\n" $1 $2}'
        ;;
      -c)
        shift
        case $1 in
          seq*)
            if [ "$init" != NO ]; then
              echo "iocInit"
              init=NO
            fi
            ;;
          iocInit)
            init=NO
            ;;
        esac
        echo "$1"
        ;;
      -s)
        shift
        if [ "$init" != NO ]; then
          echo "iocInit"
          init=NO
        fi
        echo "seq $1"
        ;;
      -i | -noinit | --noinit)
        init=NO
        ;;
      -r)
        shift
        echo "require $1"
        ;;
      -l) # This is taken care of in setPaths()
        shift
        ;;
      -dg)
        if [[ -n "${2%--dgarg=*}" ]]; then
          __LOADER__="gdb --eval-command run --args "
        else
          shift
          if [[ -z "${1#*=}" ]]; then
            __LOADER__="gdb "
          else
            __LOADER__="gdb ${1#*=} "
          fi
        fi
        ;;
      -dv)
        if [[ -n "${2%--dvarg=*}" ]]; then
          __LOADER__="valgrind --leak-check=full "
        else
          shift
          if [[ -z "${1#*=}" ]]; then
            __LOADER__="valgrind "
          else
            __LOADER__="valgrind ${1#*=} "
          fi
        fi
        ;;
      -n)
        __LOADER__="nice --10 "
        shift
        ;;
      -*)
        printf "Unknown option %s\n\n" "$1" >&2
        help
        ;;
      *.so)
        echo "dlload \"$arg\""
        ;;
      *)
        subst=""
        while [ "$#" -gt 1 ]; do
          case $2 in
            *=*)
              subst="$subst,$2"
              shift
              ;;
            *)
              break
              ;;
          esac
        done
        subst=${subst#,}
        case $arg in
          *.db | *.template)
            echo "dbLoadRecords '$arg','$subst'"
            ;;
          *.subs | *.subst)
            echo "dbLoadTemplate '$arg','$subst'"
            ;;
          *.dbd)
            # some dbd files must be loaded before main to take effect
            echo "dbLoadDatabase '$arg','$DBD','$subst'"
            ;;
          *)
            set_e3_cmd_top "$arg"
            echo "iocshLoad '$arg','$subst'"

            # Search for any instance of iocInit at the start of the line.
            # If found, do not add the iocInit to the startup script. Any
            # other occurrence of iocInit (e.g. in comments) is not matched
            # and the script will add an active iocInit.
            if grep -q "^\s*iocInit\b" "$arg"; then
              init=NO
            fi
            ;;
        esac
        ;;

    esac
    shift
  done

}

function set_e3_cmd_top(){
  local file=$1
  local file_path=""
  local file_top=""
  local file_name=""
    
  if [ -f "$file" ]; then
	  file_path="$(readlink -e "$file")"
	  file_top="${file_path%/*}"
	  file_name=${file##*/}
	  printf "# Set E3_CMD_TOP for the absolute path where %s exists\n" "$file_name"
    printf "epicsEnvSet E3_CMD_TOP \"$file_top\"\n"
	  printf "#\n"
  fi
}

function help() {
  {
    printf "\n"
    printf "USAGE: iocsh [startup files]\n"
    printf "\n"
    printf "Start the ESS iocsh and load startup scripts.\n\n"
    printf "Options:\n\n"
    printf "  -?, -h, --help   Show this page and exit.\n"
    printf "  -v, --version    Show version and exit.\n"
    printf "  -rt              Execute in realtime mode.\n"
    printf "                   (Also -RT, -realtime, --realtime)\n"
    printf "  -c 'cmd args'    Ioc shell command.\n"
    printf "  -s 'prog m=v'    Sequencer program (and arguments), run with 'seq'.\n"
    printf "                   This forces an 'iocInit' before running the program.\n"
    printf "  -i               Do not add iocInit. This option does not override\n"
    printf "                   a valid iocInit in the startup script.\n"
    printf "                   (Also -noinit, --noinit)\n"
    printf "  -r module[,ver]  Module (optionally with version) loaded via 'require'.\n"
    printf "  -l 'cell path'   Run Ioc with a cell path.\n"
    printf "  -dg [--dgarg='gdb-options']          Run with debugger gdb with user selected options or default option.\n"
    printf "  -dv [--dvarg='valgrind-options']     Run with valgrind with user selected options or default option.\n"
    printf "  -n               Run with 'nice --10' (requires sudo).\n"
    printf "  @file            More arguments are read from file.\n\n"
    printf "Supported filetypes:\n\n"
    printf " *.db, *.dbt, *.template  loaded via 'dbLoadRecords'\n"
    printf " *.subs, *.subst          loaded via 'dbLoadTemplate'\n"
    printf " *.dbd                    loaded via 'dbLoadDatabase'\n"
    printf " *.so                     loaded via 'dlload'\n"
    printf "\n"
    printf "All other files are executed as startup scripts by the EPICS shell.\n"
    printf "After a file you can specify substitutions like m1=v1 m2=v1 for that file.\n\n"
    printf "Examples:\n"
    printf "  iocsh st.cmd\n"
    printf "  iocsh my_database.template P=XY M=3\n"
    printf "  iocsh -r my_module,version -c 'initModule()'\n"
    printf "  iocsh -c 'var requireDebug 1' st.cmd\n"
    printf "  iocsh -i st.cmd\n"
    printf "  iocsh -dv --dvarg='--vgdb=full'\n"
    printf "  iocsh -dv st.cmd\n\n"
  } >&2
  exit
}

for arg in "$@"; do
  case $arg in
    -h | "-?" | -help | --help)
      help
      ;;
    -v | -ver | --ver | -version | --version)
      version
      ;;
    *) ;;
  esac
done
