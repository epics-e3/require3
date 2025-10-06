# require - EPICS module for dynamically loading functionality at runtime

In general, EPICS IOCs are built as statically or dynamically linked executables.
`require` provides a third option: dynamically load the EPICS module during IOC
startup.

In order to do this, `require` provides a wrapper utility, `iocsh` that calls and
configures `softIocPVX` from EPICS base and allows you to have a startup script that
looks like
```
require iocstats

iocshLoad("$(iocstats_DIR)/iocStats.iocsh", "IOCNAME=test")

iocInit
```
which will load the EPICS module `iocstats` and run a custom snippet from that in
order to load and configure the various records that it provides.

`require` provides at its heart three main components:
* An extension of the EPICS build system in order to build support modules to be
loaded at runtime
* A shell function `require` that is used to dynamically load support modules
during IOC startup
* A wrapper, `iocsh`, that configures and runs the default executable (`softIocPVX`)

For more information, please see the [official documentation](http://e3.pages.esss.lu.se/).
