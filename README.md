# KatanaOpenAssetIO

## What

A [Katana AssetAPI](https://learn.foundry.com/katana/dev-guide/OpsAndOpScript/AssetAPI/index.html)
adapter for [OpenAssetIO](https://github.com/OpenAssetIO/OpenAssetIO),
allowing Katana to make use of OpenAssetIO manager plugins for
interaction with an Asset Management System.

## Why

OpenAssetIO is an open standard and set of C++/Python libraries that
allows a host application (such as a DCC tool) to interface with an
asset management system. This is done via means of a plugin system,
where each asset management system has its own bespoke OpenAssetIO
manager plugin.

Once an OpenAssetIO plugin is available for a given asset management
system, any host application that integrates OpenAssetIO can interact
with it.

Katana has its own native equivalent, the Katana AssetAPI. However, the
AssetAPI is limited to usage in Katana - a plugin written for it cannot
be used in other applications. In addition, the Katana AssetAPI does not
support Python plugins, only C/C++.

This project provides a Katana AssetAPI plugin that delegates all calls
to a discovered OpenAssetIO plugin, allowing the same plugin that is
used in other host applications to also work in Katana.

Since OpenAssetIO supports Python plugins, this also means that Katana
can support Python manager plugins, overcoming a limitation of the
AssetAPI.

## Usage

### Runtime dependencies

KatanaOpenAssetIO has been tested with:

- [OpenAssetIO](https://github.com/OpenAssetIO/OpenAssetIO)
  v1.0.0-rc.1.0.
- [OpenAssetIO-MediaCreation](https://github.com/OpenAssetIO/OpenAssetIO-MediaCreation)
  v1.0.0-alpha.11 (for Python plugins).
- [Katana](https://www.foundry.com/products/katana) 7.0v4.

### Runtime configuration

A brief summary follows:

* Add the install path of KatanaOpenAssetIO to `KATANA_RESOURCES`.
* Ensure the OpenAssetIO libraries are discoverable, by adding to 
  `PATH` (Windows) /`LD_LIBRARY_PATH` (POSIX), or otherwise.
* Set `OPENASSETIO_PLUGIN_PATH` to the path to an OpenAssetIO manager
  plugin (Python or C++).
* Set `OPENASSETIO_DEFAULT_CONFIG` to the full path of an OpenAssetIO
  configuration file.
* Set Katana's default asset plugin to `KatanaOpenAssetIO`, by setting
  `KATANA_DEFAULT_ASSET_PLUGIN`, or otherwise.
* For Python manager plugins, add the location of the OpenAssetIO and
  OpenAssetIO-MediaCreation Python distribution packages to 
  `PYTHONPATH`.

On starting Katana, the configured OpenAssetIO plugin should be
automatically loaded and ready to use.

See [OpenAssetIO runtime configuration docs](http://docs.openassetio.org/OpenAssetIO/runtime_configuration.html)
for more info on the runtime requirements of OpenAssetIO, including the
format of the OpenAssetIO configuration file.

If any problems are encountered, please see below to enable debug
logging, and if this doesn't help, create a [GitHub issue](https://github.com/TheFoundryVisionmongers/KatanaOpenAssetIO/issues).

### Debug logging

KatanaOpenAssetIO's logging is tied to Katana's built-in logging
functionality. By default, you will not see many logs from
KatanaOpenAssetIO. In addition, the AssetAPI plugin is loaded lazily, so
you may not see any logs on Katana startup.

To enable debug-level logging and confirm KatanaOpenAssetIO is properly
loaded:

* Edit `/path/to/Katana/bin/log.conf`, changing 
  `log4cplus.rootLogger=INFO,` to `log4cplus.rootLogger=DEBUG,`
* Launch Katana from a terminal, and observe the terminal for log
  output.
* Perform an action in Katana that requires the AssetAPI. For example,
  create a `LookFileMaterialsIn` node and edit the `lookfile` parameter.
* You should then see several logs tagged `[DEBUG plugins.OpenAssetIO]`.
  For example:
  ```
  [DEBUG plugins.OpenAssetIO]: Retrieved default manager config file path from 'OPENASSETIO_DEFAULT_CONFIG'
  ```

See [Katana's documentation](https://learn.foundry.com/katana/Content/tg/message_logging/message_logging.html)
for more details on configuring logging.


## Building

### Build dependencies

KatanaOpenAssetIO is known to build with:

- [OpenAssetIO](https://github.com/OpenAssetIO/OpenAssetIO)
  v1.0.0-rc.1.0.
- [OpenAssetIO-MediaCreation](https://github.com/OpenAssetIO/OpenAssetIO-MediaCreation)
  v1.0.0-alpha.11.
- [OpenAssetIO-TraitGen](https://github.com/OpenAssetIO/OpenAssetIO-TraitGen)
  v1.0.0-alpha.11.
- [Katana](https://www.foundry.com/products/katana) 7.0v4.
- CPython 3.10.
- CMake 3.26.
- A [VFX Reference Platform](https://vfxplatform.com/) CY2023 compiler
  toolchain.

### Quick start

Assuming the current working directory is the repository root, and
assuming a POSIX-like system:

```sh
cmake -S . -B build \
  -DKatana_DIR=/path/to/Katana/plugin_apis/cmake \
  -DOpenAssetIO_DIR=/path/to/OpenAssetIO/lib/cmake/OpenAssetIO \
  -DOpenAssetIO-MediaCreation_DIR=/path/to/OpenAssetIO-MediaCreation/lib/cmake/OpenAssetIO-MediaCreation
  
cmake --build build --parallel

cmake --install build --prefix /path/to/desired/location
```

replacing `/path/to/...` with appropriate local paths.

For usage in Katana, the `KATANA_RESOURCES` environment variable should
include the install prefix path, i.e. `/path/to/desired/location` in
the above.

### CMake variables

| Name                                          | Description                                                         | Default |
|-----------------------------------------------|---------------------------------------------------------------------|---------|
| KATANAOPENASSETIO_ENABLE_EXTRA_WARNINGS       | Enable a large set of compiler warnings for project targets         | ON      |
| KATANAOPENASSETIO_ENABLE_SECURITY_HARDENING   | Enable security hardening features for project targets              | ON      |
| KATANAOPENASSETIO_ENABLE_UI_DELEGATE          | Enable text box 'browser' alternative and Render node output widget | ON      |
| KATANAOPENASSETIO_ENABLE_PATCH_RENDERNODEINFO | Enable Startup script patching Render node 'Pre-Render' option      | ON      |
| KATANAOPENASSETIO_ENABLE_TESTS                | Enable unit tests (additional dependencies required)                | OFF     |

## Running tests

Tests must be enabled by setting `KATANAOPENASSETIO_ENABLE_TESTS=ON` in
the CMake configure stage.

The tests have additional dependencies. They are known to build with

- [Catch2](https://github.com/catchorg/Catch2) v3.5.1
- [pybind11](https://pybind11.readthedocs.io/en/stable/) v2.9.2
- Python 3.11 (development package)

Additionally, the tests use the [Basic Asset Library](https://github.com/OpenAssetIO/OpenAssetIO-Manager-BAL)
fake/mock asset manager in the Python environment. This will be
downloaded and installed by the test runner, see [requirements.txt](tests/resources/requirements.txt).

The test environment is configured by CMake's [CTest](https://cmake.org/cmake/help/latest/manual/ctest.1.html).

The tests can be run using
```
ctest --test-dir build
```
where `build` is the build directory used when configuring/building
the project.

## Katana-specific traits and MIME types

The MediaCreation traits library and official (IANA/XDG) MIME listings
are insufficient to fully describe Katana-specific entities. So when
publishing assets, KatanaOpenAssetIO will furnish the entity trait data
with Katana-specific traits and MIME types.

For the custom Katana-specific OpenAssetIO traits, see [traits.yml](src/traits.yml).
These traits are entirely opt-in - they simply provide slightly more
fine-grained categorisation of entities.

MIME types have been invented for this project where there are no
documented MIME types available. The `LocatableContent` trait's
`mimeType` property will be imbued with the appropriate MIME type when
publishing. 

A listing of the non-standard MIME types in use follows:

| Description              | File extension | MIME type                                                   |
|--------------------------|----------------|-------------------------------------------------------------|
| Katana project           | katana         | application/vnd.foundry.katana.project                      |
| Live Group               | livegroup      | application/vnd.foundry.katana.livegroup+xml                |
| Look file archive        | klf            | application/vnd.foundry.katana.lookfile                     |
| LookFileManager settings | lfmexport      | application/vnd.foundry.katana.lookfilemanager-settings+xml |
| GafferThree rig          | rig            | application/vnd.foundry.katana.rig+xml                      |
| Macro                    | macro          | application/vnd.foundry.katana.macro                        |
| FCurve                   | fcurve         | application/vnd.foundry.katana.fcurve+xml                   |
| Scene Graph bookmarks    | xml            | application/vnd.foundry.katana.scenegraph-bookmarks+xml     |
| Deep EXR images          | deepexr        | image/x-exr                                                 |
| RLA images               | rla            | image/x-rla                                                 |
| DTEX images              | dtex           | image/x-dtex                                                |
| Deepshad images          | deepshad       | image/x-deepshad                                            |
| Histogram                | hist           | application/vnd.foundry.katana.histogram+xml                |

## Limitations

This project is still work in progress.

* Most of the core AssetAPI functions have been mapped to corresponding
  OpenAssetIO functions, but some more esoteric functions are not yet
  available.
* Similarly, there is functionality available in OpenAssetIO that is not
  yet exposed through Katana's AssetAPI.
* Integration of OpenAssetIO UI delegate plugins is still
  work-in-progress, so any UI delegation desired by an asset system
  integration must be bespoke for Katana, for now.

## Contributing

Please feel free to contribute pull requests or issues. Note that
contributions will require signing a CLA.

See the OpenAssetIO contribution docs for how to structure
[commit messages](https://github.com/OpenAssetIO/OpenAssetIO/blob/main/doc/contributing/COMMITS.md),
the [pull request process](https://github.com/OpenAssetIO/OpenAssetIO/blob/main/doc/contributing/PULL_REQUESTS.md),
and [coding style guide](https://github.com/OpenAssetIO/OpenAssetIO/blob/main/doc/contributing/CODING_STYLE.md).

The C++ code is formatted using [ClangFormat v19](https://releases.llvm.org/19.1.0/tools/clang/docs/ClangFormat.html).

The C++ code is linted using [Clang-Tidy v19](https://releases.llvm.org/19.1.0/tools/clang/tools/extra/docs/clang-tidy/index.html).

Hint: set `-DCMAKE_CXX_CLANG_TIDY=clang-tidy-19` on the command line for
the CMake configure step, to run the checks as part of the build.