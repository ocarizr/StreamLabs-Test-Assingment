# Public API documentation

Doxygen generates HTML and XML from the public headers, README and product guides.
Generated files belong in the build tree and are not committed or packaged in
runtime ZIPs. Install Doxygen 1.9.8 or newer, CMake 3.25+, Ninja and Python 3.10+, then run:

```sh
cmake -G Ninja -S docs -B build/documentation
cmake --build build/documentation --target docs
python scripts/check_documentation.py --html build/documentation/api/html
```

This standalone target does not download or compile C++ dependencies. Open
`build/documentation/api/html/index.html` in a browser. Set
`-DDOXYGEN_EXECUTABLE=/path/to/doxygen` when the tool is not on PATH.
An existing root build can instead enable `PIPEFORGE_BUILD_DOCUMENTATION=ON`
and build its `docs` target; output then resides under that build's `docs/api`.

The configuration extracts public documented APIs, keeps undocumented public
members visible to diagnostics, checks enum-value and incomplete/reference
documentation, and fails on warnings. `EXTRACT_ALL` is disabled so undocumented
members cannot silently pass. See [Doxygen's configuration reference](https://www.doxygen.nl/manual/config.html).
Private state, generated Protobuf types and upstream
implementation are outside this public reference. Parameter and lifetime contracts
remain subject to review; Doxygen does not prove their correctness.

The API documentation workflow validates local guide links and R1-R8 test
references, generates the API and uploads the `pipeforge-api` HTML artifact on
pull requests and master pushes. Download and extract that artifact to browse it.
This pipeline needs no repository write permissions, Pages configuration or wiki
credentials. Hosting can consume the generated HTML separately.

Start with [the client API](client.md), [architecture](architecture.md),
[protocol](protocol.md) and [test guide](testing.md).
