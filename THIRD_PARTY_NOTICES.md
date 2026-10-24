# Third-party components

davedown itself is MIT licensed (see `LICENSE`). It builds with and ships
these components, each under its own license:

| Component | License | Where |
|---|---|---|
| Dear ImGui (docking branch) | MIT | fetched at build time |
| Dear ImGui Test Engine | MIT (see its license for commercial terms) | fetched at build time, test binaries only |
| SDL3 | zlib | fetched at build time |
| md4c | MIT | `third_party/md4c` |
| stb_image | MIT / public domain | fetched at build time |
| Open Sans | SIL Open Font License 1.1 | `assets/fonts`, `OFL-OpenSans.txt` |
| Fira Code | SIL Open Font License 1.1 | `assets/fonts` |
| Material Icons (Google) | Apache License 2.0 | `assets/fonts`, `third_party/IconsMaterialDesign.h` |

The theme token set and icon choices follow fwcom, the FreeWili desktop
tool, by the same author.
