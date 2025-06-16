#!/usr/bin/env bash

_iocsh_completion() {
  local cur prev opts mods
  COMPREPLY=()
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD - 1]}"

  #  -r should try to determine modules
  if [[ ${prev} == -r ]]; then
    if [ -n "${EPICS_MODULES}" ]; then
      mods=$(ls "${EPICS_MODULES}/" 2>/dev/null)
      # shellcheck disable=SC2207
      COMPREPLY=($(compgen -W "${mods}" -- "${cur}"))
    fi
    return 0
  fi

  opts="-h -V -c -r -dg -dv"
  if [[ ${cur} == -* ]]; then
    # shellcheck disable=SC2207
    COMPREPLY=($(compgen -W "${opts}" -- "${cur}"))
    return 0
  fi

  # shellcheck disable=SC2207
  COMPREPLY=($(compgen -f -- "${cur}"))
}

complete -o filenames -o nospace -o bashdefault -F _iocsh_completion iocsh
