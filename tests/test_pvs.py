import os
import subprocess

import pytest
from run_iocsh import IOC

# require publishes five PVs, plus one per loaded module:
# - BaseVersion
# - Modules
# - Versions
# - ModuleVersions
# - Labels  # should be internal; will not be tested
# - ${module}Version


@pytest.mark.packaging
def test_baseversion_contains_base_version() -> None:
    raise NotImplementedError("Requires packaging metadata")


def test_modules_contains_only_require_name_when_no_modules() -> None:
    prefix = os.environ["IOCNAME"] = "PREFIX"
    pv_name = f"{prefix}:Modules"

    with IOC():
        result = subprocess.run(["pvget", pv_name], capture_output=True, text=True)
        module_list = result.stdout

        # This will confusingly be a list if multiple elements, but a string otherwise
        # i.e. ["foo", "bar"] vs "foo"
        if isinstance(module_list, list):
            assert "require" in module_list
            assert len(module_list) == 1
        elif isinstance(module_list, str):
            assert module_list == "require"
        else:
            raise TypeError(
                "Unexpected data type for %s - expected string or list of strings",
                module_list,
            )


@pytest.mark.packaging
def test_versions_contains_only_require_version_when_no_modules() -> None:
    raise NotImplementedError("Requires packaging metadata")


@pytest.mark.packaging
def test_moduleversions_contains_only_require_name_and_version_when_no_modules() -> (
    None
):
    raise NotImplementedError("Requires packaging metadata")


@pytest.mark.packaging
def test_requireversion_contain_require_version() -> None:
    raise NotImplementedError("Requires packaging metadata")
