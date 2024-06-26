"""Setup module based on setuptools.

It uses CMake to build an extension, but there are also normal Python files in
the package, making it a bit more complicated than usual.

Currently the Python package tooling is changing. This project now does not
closely follow the trend because it seems that the other tools and practices
are not very mature and we want to preserve a bit of backward compatibility.
However, we may need to and would like to do so in the future.

Ref:
- https://github.com/pypa/sampleproject
- https://github.com/pybind/cmake_example

"""

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from pathlib import Path
import os
import sys
import shlex
from subprocess import check_call


# Mapping from Python (distutils) to CMake for platform names.
#
# The value is passed to CMake '-A' option, and it seems that only MSVC uses
# it. So this is Windows only for now.
PLAT_PY2CMAKE = {
    "win32": "Win32",
    "win-amd64": "x64",
    "win-arm32": "ARM",
    "win-arm64": "ARM64",
}


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        super().__init__(name, sources=[])
        self.sourcedir = Path(sourcedir).resolve()


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        # Configuration
        extdir = Path(self.get_ext_fullpath(ext.name)).parent.resolve()
        debug = (
            int(os.environ.get("DEBUG", "0"))
            if self.debug is None
            else self.debug
        )
        cfg = "Debug" if debug else "Release"

        # CMake arguments
        cmake_args = [
            "-DPYTHON_EXECUTABLE={}".format(sys.executable),
            "-DCMAKE_BUILD_TYPE={}".format(cfg),
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={}".format(extdir),
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}".format(
                cfg.upper(), extdir
            ),
            *shlex.split(os.environ.get("CMAKE_ARGS", "")),
        ]
        if self.plat_name in PLAT_PY2CMAKE:
            cmake_args.extend(["-A", PLAT_PY2CMAKE[self.plat_name]])

        # macOS cross compilation (for the 'universal2' fat binaries)
        if sys.platform.startswith("darwin"):
            archflags = shlex.split(os.environ.get("ARCHFLAGS", ""))
            archs = []
            while archflags:
                flag = archflags.pop(0)
                if flag.startswith("-arch="):
                    archs.append(flag.split("=")[1])
                elif flag == "-arch":
                    archs.append(archflags.pop(0))
            if archs:
                cmake_args.append(
                    "-DCMAKE_OSX_ARCHITECTURES={}".format(";".join(archs))
                )

        # Build arguments
        build_args = ["--config", cfg]
        if "CMAKE_BUILD_PARALLEL_LEVEL" not in os.environ:
            if hasattr(self, "parallel") and self.parallel:
                build_args.append("-j{}".format(self.parallel))

        # Build extension
        build_temp = Path(self.build_temp) / ext.name
        if not build_temp.exists():
            build_temp.mkdir(parents=True, exist_ok=False)
        check_call(["cmake", str(ext.sourcedir)] + cmake_args, cwd=build_temp)
        check_call(["cmake", "--build", "."] + build_args, cwd=build_temp)


setup(
    ext_modules=[CMakeExtension("_macposts_ext")],
    cmdclass={"build_ext": CMakeBuild},
)
