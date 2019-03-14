iocsh.bash
====

## E3 Unique Variables

```
E3_CMD_TOP   : the absolute path where a startup script (cmd file) is
E3_IOCSH_TOP : the absolute path where the iocsh.bash is executed
```

For example, one executes the iocsh.bash ```${HOME}``` to call ```e3_local/cmds``` via

```sh
$ iocsh.bash e3_local/cmds/iocStats.cmd
```
In this case,
```E3_CMD_TOP``` is defined as ```"${HOME}/e3_local/cmds"```
```E3_IOCSH_TOP``` is defined as ```"${HOME}"```

