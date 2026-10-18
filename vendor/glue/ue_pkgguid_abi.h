/*
 * ue_pkgguid_abi.h — the optional "pkgguid" ABI module (C face).  PKGGUID_ABI_VERSION 1.
 * ============================================================================
 * An optional module, versioned on its own (as ../proxynet/ is): not part of the main thin-shim ABI (../ue_abi.h),
 * a change here does not move UE_ABI_VERSION, and an engine that does not vendor this header never asks.
 *
 * What it is for: a network join asks for each package by name and GUID; the size the server announces is only a
 * shortcut. When a local file's size differs, the engine needs that file's GUID, and a web build must not read the
 * file on that path (a read is a network fetch of the whole file, from the pending level). The build records every
 * package's GUID in the manifest the page already holds; this import hands it over.
 *
 * ue_package_guid(path, out33): 1 = known — `out33` (caller-owned, 33 bytes) holds the GUID as FGuid::String()
 * writes it (32 upper-case hex digits, four little-endian DWORDs, %08X each) + NUL; 0 = unknown (not a package,
 * not in the manifest, a mod path, or the module is absent), and the engine then downloads the package.
 * Not proxied: answered on the calling (engine) thread, over the control channel main already serves.
 *
 * Same conventions as ../ue_abi.h: UE_IMPORT = the engine calls, JS provides; one line per boundary function
 * (the parity test, test/package-guid.test.cjs, keys off the marker).
 */
#ifndef UE_PKGGUID_ABI_H
#define UE_PKGGUID_ABI_H

#define UE_PKGGUID_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UE_IMPORT
#define UE_IMPORT extern
#endif

UE_IMPORT int ue_package_guid(const char* path, char* out33);

#ifdef __cplusplus
}
#endif

#endif /* UE_PKGGUID_ABI_H */
