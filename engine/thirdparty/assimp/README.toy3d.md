# Assimp source subset for Toy3d

This directory contains a source subset of Assimp 6.0.5, copied from commit
`3fddcfcc05ee6073853a01d6f133e01f3d9743c7`. See `LICENSE` for its
BSD 3-Clause license and attribution.

Toy3d builds this copy as a static, import-only library. Its enabled importers
are FBX, OBJ, and glTF (including glTF 2.0 / GLB). The subset retains Assimp's
core, public headers, these importer sources, and their supporting code.
`contrib/` contains only helpers still used by the retained import path:

- `rapidjson`: glTF JSON parsing.
- `stb`: Assimp's embedded image decoding path.
- `utf8cpp`: text conversion in the shared importer base.
- `earcut-hpp`: polygon triangulation post-process.
- `zlib`: compressed FBX data and Assimp's shared compression code.

ZIP archive I/O and the optional Open3DGC compression path from legacy glTF
are disabled and their sources are omitted. Other importer source directories, exporter implementations,
command-line tools, samples, documentation, and the upstream test suite are
omitted. `engine/thirdparty/assimp.cmake` owns the build options.

The import tool is optional (`TOY3D_ENABLE_ASSIMP_MODEL_IMPORT=ON`). Runtime
and resource targets do not link Assimp. When upgrading, start from the upstream
commit above, reapply this source selection, and run all four model probe tests.
