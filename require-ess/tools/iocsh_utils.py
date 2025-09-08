"""Utilities for iocsh."""

import atexit
import os
import logging
import socket
import sys
from tempfile import NamedTemporaryFile
from pathlib import Path
from sys import platform
from typing import List

DEFAULT_ERRLOG_BUFFER_SIZE = 2048
SUPP_FILE = Path(__file__).resolve().parent / "iocsh_epics.supp"


@atexit.register
def graceful_shutdown() -> None:
    print("\nExiting e3 IOC shell")
    os.system("/bin/bash -c '[[ -t 1 ]] && stty sane'")


class TemporaryStartupScript:
    """Class to manage IOC shell commands.

    Holds on to commands in a buffer which it writes to file.
    """

    def __init__(self, iocname: str = None) -> None:
        self.file = NamedTemporaryFile(delete=False)
        self.command_buffer = []
        self._saved = False

        self.set_variable("REQUIRE_IOC", sanitize_iocname(iocname or generate_prefix()))
        self.set_variable("IOCSH_TOP", Path.cwd())
        self.set_variable(
            "IOCSH_PS1", f"{iocname} > " if iocname else generate_prompt()
        )

        # The message size maximum must be slightly smaller than the buffer size
        # (to account for the null terminator)
        self.add_command(
            f"errlogInit2 {DEFAULT_ERRLOG_BUFFER_SIZE} {DEFAULT_ERRLOG_BUFFER_SIZE-1}"
        )

        if platform.startswith("linux"):
            shared_lib_suffix = "so"
        elif platform == "darwin":
            shared_lib_suffix = "dylib"
        else:
            raise NotImplementedError(f"Unsupported platform: {platform}")

        # load require
        self.add_command(
            f"dlload {str(Path(os.environ['EPICS_MODULES']) / 'lib' / f'librequire.{shared_lib_suffix}')}"
        )
        self.add_command(
            f"dbLoadDatabase {str(Path(os.environ['E3_REQUIRE_DBD']) / 'require.dbd')}"
        )
        self.add_command("require_registerRecordDeviceDriver")

    @property
    def name(self) -> str:
        return self.file.name

    def __enter__(self) -> None:
        return self

    def __exit__(self, *args) -> None:
        self.file.close()

    def _get_content(self) -> str:
        """Return list of commands as multi-line string."""
        return "\n".join(self.command_buffer) + "\n"  # ensure newline at EOF

    def save(self) -> None:
        """Store command 'buffer' to file."""
        if self._saved:
            raise RuntimeError("File has already been saved")
        self.file.write(self._get_content().encode())
        self.file.flush()
        self._saved = True

    def set_variable(self, var: str, val: str) -> None:
        self.add_command(
            f'epicsEnvSet {var} "{val}"'
        )  # add quotation marks to avoid symbols being interpreted

    def load_snippet(self, name: str) -> None:
        self.add_command(f"iocshLoad {name}")

    def load_module(self, name: str) -> None:
        self.add_command(f"require {name}")

    def load_database(self, name: str) -> None:
        self.add_command(f"dbLoadRecords {name}")

    def add_command(self, command: str) -> None:
        self.command_buffer.append(command)


def verify_environment_and_return_require_version() -> str:
    """Verify select known EPICS and e3 variables and return current require version."""

    def check_mandatory_env_vars() -> None:
        mandatory_vars = (
            "EPICS_HOST_ARCH",
            "EPICS_BASE",
            "E3_REQUIRE_VERSION",
            "E3_REQUIRE_BIN",
            "E3_REQUIRE_LIB",
            "E3_REQUIRE_DB",
            "E3_REQUIRE_DBD",
        )
        for var in mandatory_vars:
            _ = os.environ[var]

    try:
        check_mandatory_env_vars()
    except KeyError as e:
        logging.debug(f"Environment variable {e} is not set")
        sys.exit("Please source an environment before you try to use the IOC shell")

    # compare path of this script to sourced environment's executables
    if not str(Path(__file__).resolve().parent) == os.environ["E3_REQUIRE_BIN"]:
        logging.debug(
            f"Sourced environment is '{os.environ['E3_REQUIRE_BIN']}' and this script is from '{Path(__file__).resolve().parent}'"
        )
        sys.exit(
            "You have sourced a different environment than what this IOC shell is from"
        )

    return os.environ["E3_REQUIRE_VERSION"]


def extract_require_version() -> str:
    """Return loaded environment's version of require."""
    try:
        return os.environ["E3_REQUIRE_VERSION"]
    except KeyError:
        sys.exit("Please source an environment before you try to use the IOC shell")


def generate_prompt() -> str:
    """Return IOC shell prompt."""
    fqdn = socket.gethostname()
    hostname, *_ = fqdn.partition(".")
    prompt = f"{hostname}-{os.getpid()}"
    return f"{prompt} > "


def generate_prefix() -> str:
    """Return fallback PV prefix."""
    try:
        user = os.getlogin()
    except OSError:  # for wonky cases
        user = "UNKNOWN"
    return f"TEST:{user}-{os.getpid()}"


def generate_banner() -> str:
    """Return ascii art banner."""
    ascii_art = r"""
       ,----.     ,--. ,-----.  ,-----.           ,--.            ,--.,--.
 ,---. '.-.  |    |  |'  .-.  ''  .--./     ,---. |  ,---.  ,---. |  ||  |
| .-. :  .' <     |  ||  | |  ||  |        (  .-' |  .-.  || .-. :|  ||  |
\   --./'-'  |    |  |'  '-'  ''  '--'\    .-'  `)|  | |  |\   --.|  ||  |
 `----'`----'     `--' `-----'  `-----'    `----' `--' `--' `----'`--'`--'
"""
    return ascii_art


def sanitize_iocname(iocname: str) -> str:
    """Remove characters not allowed in PV names from iocname."""
    extra_allowed = "_-+:[]<>;"
    sanitized = "".join(c if c.isalnum() or c in extra_allowed else "" for c in iocname)
    if sanitized != iocname:
        logging.warning(
            f"Removed illegal characters from IOC name: '{iocname}' -> '{sanitized}'"
        )
    return sanitized


def fetch_debugger_args(debugger: str, debugger_args: str) -> List[str]:
    """Fetch the debugger arguments for the given debugger."""
    if debugger == "gdb":
        return debugger_args.split(" ") + ["--args"]
    if debugger == "lldb":
        return debugger_args.split(" ") + ["--"]
    if debugger == "valgrind":
        debugger_args = debugger_args if debugger_args else "--leak-check=full"
        return [f"--suppressions={SUPP_FILE!s}"] + debugger_args.split(" ")
    raise NotImplementedError(f"Invalid debugger: {debugger}")
