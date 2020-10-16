#!/bin/bash

#   Script to show in the prompt the EPICS Base Version 
#
#   Author  : Alfio Rizzo 
#   email   : alfio.rizzo@ess.eu
#   date    : Fri Oct 16 12:28:22 CEST 2020
#


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
source $DIR/setE3Env.bash 

IFS='-' read -ra base <<< "$EPICS_BASE"
PS1=(EPICS-${base[1]})$PS1


