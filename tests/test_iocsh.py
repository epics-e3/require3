import subprocess
from time import sleep

from run_iocsh import IOC

TIMEOUT_DURATION_IN_SEC = 1


def test_ioc_is_running_when_started() -> None:
    with IOC(timeout=TIMEOUT_DURATION_IN_SEC) as ioc:
        sleep(TIMEOUT_DURATION_IN_SEC + 1)
        assert ioc.is_running()


def test_ioc_is_not_running_when_stopped() -> None:
    with IOC(timeout=TIMEOUT_DURATION_IN_SEC) as ioc:
        sleep(TIMEOUT_DURATION_IN_SEC + 1)
    # The IOC will be exited or killed at `IOC.__exit__`
    assert not ioc.is_running()


def test_afterinit_is_after_iocinit() -> None:
    proc = subprocess.Popen(
        ["iocsh", "-c", "afterInit('test-string')"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        output, _ = proc.communicate(input=b"exit\n", timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        # Trying to run `outs, errs = proc.communicate()` can raise
        #
        #   ValueError: Invalid file object: <_io.BufferedReader name=7>
        #
        # when stdin is already closed.
    output_array = output.split(b"iocInit")
    assert len(output_array) > 1
    output_after_init = output_array[-1]

    assert b"test-string" in output_after_init
