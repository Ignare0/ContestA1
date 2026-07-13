# Third-Party Software

## Slang

- Project: https://github.com/MikePopoloski/slang
- Version: 11.0 development snapshot
- Commit: `8acc660a20b70de48ecec1c7471863e6f4b3ae6f`
- License: MIT; see `third_party/slang/LICENSE` and `third_party/slang/LICENSES/`.
- Reused for: preprocessing, parsing, name and type resolution, constant evaluation,
  diagnostics, and elaboration.
- Not reused for: ModelIR, four-state runtime values, net resolution, event scheduling,
  system-task runtime behavior, C++ model generation, compile caching, or parallel simulation.

## fmt

- Project: https://github.com/fmtlib/fmt
- Version: 12.2.0
- License: MIT; see `third_party/fmt/LICENSE`.
- Used transitively by Slang.

## Boost.Regex standalone fork

- Project: https://github.com/MikePopoloski/regex
- Version: boost-1.91.0
- License: Boost Software License 1.0; see `third_party/boost-regex/LICENSE_1_0.txt`.
  The pinned standalone archive references but does not contain this file, so it is
  copied verbatim from https://www.boost.org/LICENSE_1_0.txt.
- Used transitively by Slang.

## tomlplusplus

- Project: https://github.com/marzer/tomlplusplus
- Version: 3.4.0
- License: MIT; see `third_party/tomlplusplus/LICENSE`.
- Used transitively by Slang.

Slang's optional mimalloc, Catch2, tools, examples, documentation, installation,
and Python-binding targets are disabled and are not part of the submitted build.
