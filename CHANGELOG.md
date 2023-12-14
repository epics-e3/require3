# CHANGELOG
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### New features

* Allow for module-specific build rules (see: sequencer) to be installed and used
  within e3
* Automatically install LICENSE files with modules

### Bugfixes

* Fixed an issue where .template and .substitutions files with the same name would build incorrectly
* Removed a number of memory leaks found by valgrind

### Other changes

* Rewrite `iocsh` converting it from being a shell script to a python (3.6) script
  * Remove support for file extensions: `.so`, `.dbd`, `.db`, `.substitutions`, `.template`, `.iocsh`
  * Remove support for `nice`
  * Remove support for sequencer programs
  * Remove optional argument passing to `gdb`
  * Change the IOC shell to use both stdout and stderr (previously only stdout)
  * Change default prompt
  * Change fallback IOC-name (used when `IOCNAME` is not set)
  * Multiple argument changes; e.g.
    * Shortened argument for printing version and exit changed from `-v` to `-V`
    * Make running IOC as realtime or with debuggers mutually exclusive
    * Change how arguments are passed to `gdb` and `valgrind` (see help: `--help`)
* Replaced `tclx` script to expand .dbd files with a python script
* Removed ability to pass `args` to require (which have not been used within e3)
* Removed `require module,ifexists` option
* Stop require from looking in legacy locations for various files
* Rewrite internal linked list to have better memory safety
* Remove `ARCH_FILTER` support; from now on, only `EXCLUDE_ARCHS` is used.

## [5.0.0]

### New Features
* Check inconsistent versions between dependencies at build time. If an inconsistency is found the build will fail.
* Block module loading after iocInit has been invoked.
* Arguments have been added to `iocsh.bash` to enable user to pass any debugger options to GDB and Valgrind.
* Autocompletion for `iocsh.bash` has been added
* Removed `iocsh_gdb.bash` and `iocsh_nice.bash`, both of whose functionality can be called via `iocsh.bash -dg` and `iocsh.bash -n`, respectively.
* Require will automatically build `.template` and `.substitutions` files into the common build directory instead of into the source Db path
* Added consistency check between e3 environment variables and path to `iocsh.bash`. `iocsh.bash` will abort if these are not consistent.
* Add e3 version infomation to the shell prompt
* Add option to allow override of automatic addition of `iocInit` to generated startup script

### Bugfixes
* Fixed issue where updated dependencies of substitution files did not retrigger a .db expansion.
* db expansion happens at build time, not at install time.
* `iocsh.bash --help` (and variants) no longer loads tries to load `env.sh`.
* `make build` will fail if any of the target architectures fail, not just the last one.
* Fixed issue where git passwords would be displayed in plaintext in metadata file
* Fixed issue where `LD_LIBRARY_PATH` could keep old `siteLibs` path around
* Removed duplicated entries from generated `.dep` files
* Fixed issue where `.hpp` files were not installed correctly with `KEEP_HEADER_SUBDIRS`
* Missing `REQUIRED` dependencies now cause the build to fail instead of providing a warning
* Fixed issue where consecutive builds might not track updated dependencies
* Fixed an issue related to buffering of data being written to a shared filesystem which produced garbled `.dep` files

### Other changes
* Remove `IOCNAME:exit` PV. Stop loading `softIocExit.db` and now it's require that exposes the `BaseVersion` PV.
* Patch system now applies all patches in the `patch/` directory. Versioning for patches should be handled by git,
  not by require.
* The loop over `EPICSVERSION` in `driver.makefile` has been removed; various other cleanup has been performed.
* Improved output during IOC startup
* Remove Win32 and CYGWIN32 support
* Remove `/tools` directory, including `test_installed_modules.sh` utility
* Rename `runScript` to `afterInit`
* Updated PV-names to be ESS compliant, and remove VERSIONS PV
* Added revision number parsing for test versions
* Joined together the variables `REQUIRE_CONFIG` and `E3_REQUIRE_CONFIG`
* Removed legacy file `DECOUPLE_FLAGS`
* Rename `iocsh.bash` to `iocsh`
* Removed `<module>_TEMPLATES` in favour of `<module>_DB`
* Removed unnecessary code from `make init`.
* Removed usage of `env.sh` - now there is a check only for seeing if the environment variable `$IOCNAME` is set
* Fix typos in `iocsh_functions.bash` comments
* Rearrange usage to match order of options in code
* Add information about realtime option to usage
* Removed references to EPICS Base v3


[Unreleased]: https://gitlab.esss.lu.se/e3/e3-require/-/compare/5.0.0...master
[5.0.0]: https://gitlab.esss.lu.se/epics-modules/require/-/compare/3.1.4_conda_fix_epics_7.0.6.1...e3-1170?from_project_id=960&straight=false
