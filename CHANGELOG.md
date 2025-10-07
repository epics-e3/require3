# CHANGELOG
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

* Add timestamp for `LoadedModules`
* Deprecate `IOCNAME` environment variable in favour of `--iocname` argument for `iocsh`
* Switch to using softIocPVX in lieu of softIocPVA (new PVA stack)
* Support to use lldb for debugging. Note that this changes how debugging works:
```sh
$ iocsh --debugger gdb --debugger-args "args for gdb" st.cmd
$ iocsh --debugger lldb --debugger-args "args for lldb" st.cmd
$ iocsh --debugger valgrind --debugger-args "args for valgrind" st.cmd
```

### Fixed

* Exit `iocsh` if file to run does not exist
* Add trigger mapping on `LoadedModules` to silence QSRV2 warning

### Removed

* Remove `--realtime` option from `iocsh`
* Remove functions not used in production: `ld`, `libversionShow` and `pathAdd`.
* License handling in driver.makefile. This should be provided by a package manager.
* Remove version handling.

### Changed

* Change module path.
* Module libraries are installed on standard directory `/lib`.
* Rework internal logic for loading modules. See commit 24d41905d46ed4395e0822f2467b54d89437890f.

## [5.1.1.post2]

### Added

* Add support for darwin-x86

## [5.1.1.post1]

### Fixed

* Add `MODULE_CONFIG` variable to include module-specific config file

### Changed

* Remove dbd rules for GPIB code
* Remove dbd rules for sequencer

## [5.1.1]
The require code was once separated on two repositories for customization. One
would be target for an NFS based distribution system while the other for conda
packages. As ESS decided to pursue an NFS solution much work was done on that
repository that was latter ported to this one. Five versions were released from
the NFS repository that are not here: 3.4.0, 3.4.1, 4.0.0, 5.0.0 and 5.1.0. Some work
was done to synchronize both repositories for this release. The following
entries are a compilation of the entries from all of this versions.

### Fixed

* Fix memory leaks on IOC exit
* Fix dirty terminal after IOC exit for some distributions
* Resolve `E3_CMD_TOP` when a startup script is being run with `iocsh` (previously expanded to just `.`)
* Fix truncated errorlog messages
* Fixed an issue where .template and .substitutions files with the same name would build incorrectly
* Removed a number of memory leaks found by valgrind
* Fixed issue where failure of one of the patches to apply wouldn't stop the build
* Fixed memory leak in `afterInit`
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
* Fixed an issue where `iocshRegisterCommon()` was called when registering functions for modules as they are loaded. This had
  the effect of overwriting any functions that have the same name as a common one with their original one (e.g. `dbLoadTemplate`
  from `require`)
* Removed `loadIocsh` function, which was just a cover for `runScript`.
* Removed references to `INSTBASE`
* Fixed issue where `make debug` would recompile a module.
* Fixed issue where `make install` would fail if you had not run `make build` first.
* Fixed issue with module name checking which caused modules to fail to build on certain linux distributions
* Exact module version is fetched correctly at build-time to ensure that new releases do not affect upstream dependencies
* Issue involving priority between some test versions fixed
* `make patch` when there are no patch files will no longer return an error code.

### New features

* Allow for module-specific build rules (see: sequencer) to be installed and used
  within e3
* Automatically install LICENSE files with modules
* `afterInit` can now run commands of arbitrary length. Note: The syntax has changed from
  ```
  afterInit foo bar baz
  ```
  to
  ```
  afterInit 'foo bar baz'
  ```
* Add NTTable PV for module and version information as `LoadedModules`
* Block module loading after iocInit has been invoked.
* Arguments have been added to `iocsh.bash` to enable user to pass any debugger options to GDB and Valgrind.
* Autocompletion for `iocsh.bash` has been added
* Removed `iocsh_gdb.bash` and `iocsh_nice.bash`, both of whose functionality can be called via `iocsh.bash -dg` and `iocsh.bash -n`, respectively.
* Require will automatically build `.template` and `.substitutions` files into the common build directory instead of into the source Db path
* Added consistency check between e3 environment variables and path to `iocsh.bash`. `iocsh.bash` will abort if these are not consistent.
* Add option to allow override of automatic addition of `iocInit` to generated startup script
* Build numbers are now prefixed with a `+`.


### Changed

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
* Remove `IOCNAME:exit` PV. Stop loading `softIocExit.db` and now it's require that exposes the `BaseVersion` PV.
* Patch system now applies all patches in the `patch/` directory. Versioning for patches should be handled by git,
  not by require.
* The loop over `EPICSVERSION` in `driver.makefile` has been removed; various other cleanup has been performed.
* Improved output during IOC startup
* Rename `runScript` to `afterInit`
* Updated PV-names to be ESS compliant, and remove VERSIONS PV
* Added revision number parsing for test versions
* Rename `iocsh.bash` to `iocsh`
* Removed `<module>_TEMPLATES` in favour of `<module>_DB`
* Removed usage of `env.sh` - now there is a check only for seeing if the environment variable `$IOCNAME` is set
* Add information about realtime option to usage

## [3.3.0]
3.3.0 is a relatively major release with many bugfixes and a number of features added based on consultation
with developers. One major change is that versions are now parsed as MAJOR.MINOR.PATCH-BUILD (with build
number being optional, assumed to be zero if not present). This has proven to be necessary in the NFS
setting for a number of separate reasons to handle dependencies properly. Note the following behavior:

* `require module` - loads the latest numeric version of a given module.
* `require module,x.x.x` - loads the specific numeric version of a module, with the highest build number
* `require module,x.x.x-x` - loads a specific build number of the specified version of a module

### Removed
* Removed all EPICS 3.\* and VxWorks code, as these are not to be supported at ESS.

### Added
* Consistent with the philosophy of not requiring module version pinning, if one specifies a dependent
  module with e.g. `REQUIRED += asyn` then the latest version of asyn will be used. No version need
  to be specified.
* Installed modules will be installed together with a metadata file containing information about the
  module and wrapper, including the git status of the wrapper/module, the latest tag/commit hash, and
  the git url for the wrapper.
* Build numbers are now supported properly
* `iocsh.bash` will load `env.sh` at startup (which can be specified), which allows `IOCNAME` to be
  specified at startup
* Added the ability to install header files while preserving directory structure instead of flattening
  all header files into a single module/version/includes directory.
* Added a `prebuild` target that runs before build so module developers can run specific code before the build process.
* A module developer can now install dbd files separate from the module dbd file by using `DBD_INSTALLS += file.dbd`.

### Fixed
* Ensures that lowercase module names are enforced consistently
* Vendor libraries are only installed at install time, not at build time
* Vendor libraries are uninstalled when `make uninstall` is run
* `iocsh.bash` now supports multiple directories being specified with the -l (local) flag as a source of loading modules
* Fixed an error where mounting the NFS E3 in a location other than `/epics` would result in failed builds
* Re-added functionality to have `iocsh.bash` automagically source `setE3Env.bash`.

## [3.2.0]
Require 3.2.0 is the first version of require used during our initial return to the NFS implementation
of E3. It maintains some of the work done there (modules are lowercased for consistency), but mostly
reverts back to a similar version to require 3.1.2. One major difference is that module pinning is not
"required" (pun only semi-intended). That is, a startup script can simply include a statement of the
form `require module` instead of `require module,version` as was the case in 3.1.2. An IOC developer can
of course pin a specific module version. If they do not, then the highest numeric version will be chosen.

### Added
* Added -dg, -dv options to run gdb and valgrind using `iocsh.bash`
* If `IOCNAME` is defined, then it is used in the PV names set by require instead of `REQMOD:$(hostname)-$(pid)`.
* Reverted changes from conda require to return to NFS
* Moved tools files from e3-require to require submodule

### Fixed
* Fixed issue where a second user running `iocsh.bash` on a machine would be unable to create the temporary
  startup script


[Unreleased]: https://gitlab.esss.lu.se/e3/e3-require/-/compare/5.1.1...master
[5.1.1]: https://gitlab.esss.lu.se/e3/e3-require/-/compare/3.3.0...5.1.1
[3.3.0]: https://gitlab.esss.lu.se/e3/e3-require/-/compare/3.2.0...3.3.0
[3.2.0]: https://gitlab.esss.lu.se/e3/e3-require/-/tree/3.2.0
