_iocsh_bash() {
  local cur opts
  COMPREPLY=()
  cur="${COMP_WORDS[COMP_CWORD]}"
  opts="-h -v -c -s -r -e -dg -dv -l -n"

  if [[ ${cur} == -* ]]; then
    mapfile -t COMPREPLY < <(compgen -W "${opts}" -- "${cur}")
    return 0
  fi
}

complete -o filenames -o nospace -o bashdefault -F _iocsh_bash iocsh
