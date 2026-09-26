# Warcraft III CASC Storage In The VFS

## Goal

Allow OpenRealm to read installed Warcraft III 3.0 game resources directly
from its local CASC storage, so the map-testing workflow can run a loose or
MPQ-packed map against real game data. The initial acceptance installation is
`E:\Games\Warcraft III`, product `w3`, build `3.0.0.24268`.

## Constraints

- Keep MPQ map loading and MPQ-specific operations on StormLib.
- Keep loose-file lookup and existing MPQ precedence unchanged.
- Read installed CASC data locally. Do not contact Blizzard CDNs, download
  assets, extract the installation, or modify files under the game directory.
- Select the CASC product explicitly as `w3`; never silently select the first
  product in a multi-product storage.
- If a directory is positively identified as CASC but cannot be opened as the
  requested product, report the storage path, product, and library error, and
  fail data-directory setup. Do not silently continue as if the game data were
  mounted.
- Preserve the current behavior for directories that are not CASC roots.
- Use CascLib's storage-default locale selection unless Warcraft III-specific
  data proves that an explicit locale is needed.
- Keep OpenRealm's VFS independent from CascLib types and handles.

## Architecture

### VFS file handles

`FS_OpenFile` currently returns a StormLib file handle, and `FS_ReadFile`
directly calls StormLib to obtain size and read bytes. Introduce an opaque VFS
file handle with backend operations for read, seek, size, and close. Its
backend may wrap either a StormLib file handle or a CascLib file handle.
`FS_ReadFile` and other VFS consumers use these operations rather than calling
StormLib directly.

StormLib archive handles remain unchanged. In particular, the current map
archive stays an MPQ priority overlay, and WC3 map readers that open
`war3map.w3i`, `war3map.wpm`, or other members from that archive continue using
the existing MPQ API. `FS_OpenNestedLooseFile` may still open an inner MPQ,
wrapped by the VFS file handle when returned through `FS_OpenFile`.

### Mounted sources and lookup order

Represent mounted file sources as typed VFS sources while preserving the
existing order and visibility rules:

1. The active map's priority MPQ archive.
2. MPQ sources in the same reverse-mount order currently used for archives,
   applying `FS_ArchiveFileVisible` to MPQ sources.
3. CASC sources in reverse mount order as base installed game data.
4. Existing nested loose-archive lookup and loose-file fallback.

A CASC storage participates as a mounted named-file source below explicit MPQ
overlays and above loose fallback. Its named-file opens use the path supplied
to `FS_OpenFile`; this first scope does not add CASC directory enumeration or
map-list enumeration. Loose maps and MPQ maps remain discoverable through
their existing paths.

### Storage discovery and lifetime

When adding a data directory, detect a local CASC root through its CASC
installation markers rather than recursively scanning the large `Data/data`
payload files. Open the root with CascLib in local/offline mode and explicitly
request product `w3`. Store the storage handle with the mounted source and
close it from the VFS shutdown/reset path. Open file handles must close before
their storage is released.

Non-CASC directories continue through the existing loose-file and MPQ scan.
If a directory contains both loose overlays and CASC data, keep the directory
available for loose-file reads and register the CASC storage as an additional
source without scanning CASC payload files as ordinary files.

### Dependency boundary

Use the upstream CascLib C API through a small OpenRealm adapter. Pin upstream
release 3.0 at commit `4971d36` and retain its MIT license. Build CascLib as an
isolated static dependency using the project's supported Windows and Unix
toolchains; do not expose CascLib headers from the public VFS API. Before
acquiring or vendoring the dependency, ask the user, as requested.

The first dependency-backed step is a compatibility probe against the supplied
installation: open product `w3`, query the product/build metadata, and read
several known named files. Continue with the VFS integration only after the
probe succeeds. If it fails, report the precise CascLib error and stop before
choosing another library or revision.

## Error reporting

- Distinguish “directory is not CASC” from “recognized CASC root failed to
  open.”
- On file lookup failure, continue searching lower-priority mounted sources,
  matching existing VFS behavior.
- On an actual read/seek/size failure after a CASC file opens, return failure
  through the VFS operation and include the requested path and operation in
  diagnostics; do not substitute empty data.
- Never report a CASC source as mounted until CascLib has opened the requested
  product successfully.

## Validation

1. A dependency-backed probe opens the exact local installation with product
   `w3` and reads known named files without changing the installation.
2. VFS tests cover generic reads, seeks, sizes, and closes for MPQ and CASC
   backends, including empty and failed reads.
3. Existing MPQ tests and map-priority behavior remain unchanged.
4. `FS_FileExists` sees named CASC files. Map enumeration remains based on the
   existing loose/MPQ paths.
5. The 23-Race Legion map loads from its test copy against the CASC data root;
   the script audit distinguishes load/config/main results and reports any
   remaining unsupported natives or missing resources.
6. Verify Windows MinGW and at least one Unix build for the isolated dependency
   and adapter.

## Out Of Scope

- Parsing CASC storage formats inside OpenRealm.
- Online storage, CDN access, asset downloading, or full-install extraction.
- CASC creation or mutation.
- Listing every CASC file or enumerating maps from CASC.
- Replacing StormLib or converting Warcraft III map archives away from MPQ.
- Explicit locale overrides unless Warcraft III's actual storage behavior
  proves that they are needed.

## References

- [CascLib release 3.0 at commit `4971d36`](https://github.com/ladislav-zezula/CascLib/releases)
- [CascLib API header](https://github.com/ladislav-zezula/CascLib/blob/4971d36/src/CascLib.h)
- [CascOpenStorage locale-mask documentation](https://www.zezula.net/en/casc/casclib/cascopenstorage.html)
- [CascLib issue #291: a newer Warcraft III build failing to open with CascLib 3.0](https://github.com/ladislav-zezula/CascLib/issues/291)
