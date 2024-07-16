"""Utilities for iocsh."""

import atexit
import os
import logging
import socket
import sys
from tempfile import NamedTemporaryFile
from pathlib import Path

DEFAULT_ERRLOG_BUFFER_SIZE = 2048


@atexit.register
def graceful_shutdown() -> None:
    print("\nExiting e3 IOC shell")
    os.system("/bin/bash -c '[[ -t 1 ]] && stty sane'")


class TemporaryStartupScript:
    """Class to manage IOC shell commands.

    Holds on to commands in a buffer which it writes to file.
    """

    def __init__(self) -> None:
        self.file = NamedTemporaryFile(delete=False)
        self.command_buffer = []
        self._saved = False

        self.set_variable("REQUIRE_IOC", generate_prefix())
        self.set_variable("IOCSH_TOP", Path.cwd())
        self.set_variable("IOCSH_PS1", generate_prompt())

        self.add_command(
            f"errLogInit2 {DEFAULT_ERRLOG_BUFFER_SIZE} {DEFAULT_ERRLOG_BUFFER_SIZE}"
        )

        # load require
        self.add_command(
            f"dlload {str(Path(os.environ['E3_REQUIRE_LIB']) / os.environ['EPICS_HOST_ARCH'] / 'librequire.so')}"
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
            "E3_REQUIRE_NAME",
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


def generate_prompt(separator: str = " > ") -> str:
    """Return IOC shell prompt."""
    try:
        prompt = os.environ["IOCNAME"]
    except KeyError:
        fqdn = socket.gethostname()
        hostname, *_ = fqdn.partition(".")
        prompt = f"{hostname}-{os.getpid()}"
    return f"{prompt}{separator}"


def generate_prefix(prefix: str = "TEST:") -> str:
    """Return fallback IOC-name."""
    try:
        iocname = os.environ["IOCNAME"]
    except KeyError:
        try:
            user = os.getlogin()
        except OSError:  # for wonky cases
            user = "UNKNOWN"
        iocname = f"{prefix}{user}-{os.getpid()}"
    return iocname


def generate_banner() -> str:
    """Return ascii art banner."""
    ascii_art = """
       ,----.     ,--. ,-----.  ,-----.           ,--.            ,--.,--.
 ,---. '.-.  |    |  |'  .-.  ''  .--./     ,---. |  ,---.  ,---. |  ||  |
| .-. :  .' <     |  ||  | |  ||  |        (  .-' |  .-.  || .-. :|  ||  |
\   --./'-'  |    |  |'  '-'  ''  '--'\    .-'  `)|  | |  |\   --.|  ||  |
 `----'`----'     `--' `-----'  `-----'    `----' `--' `--' `----'`--'`--'
"""
    return ascii_art
