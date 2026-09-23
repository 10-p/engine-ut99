// pkgguid-shim.js — the thin --js-library for the OPTIONAL "pkgguid" ABI module (ufront 2.79p).
// ============================================================================
// VENDORED from @ufront/glue at PKGGUID_ABI_VERSION 1, by the engines whose join path asks a package's GUID
// (the v436 version-aware package lookup). A SEPARATE file: webrtc-shim.js stays byte-identical in every engine
// repo and the v1 ABI is untouched (ABI_VERSION stays 1).
//
// ONE import, FORWARDED to the runtime-injected `Module.UE.pkgguid` (installed on the engine thread by
// @ufront/glue, src/fetchfs/package-guid.js). No logic. NOT proxied: it runs on the calling (engine) thread,
// which is where `Module.UE.pkgguid` lives.
//
// ★ ABSENT MODULE = THE OLD BEHAVIOUR, never a failure: no `Module.UE.pkgguid` -> 0 ("unknown"), and the engine
// downloads as it did before. A module present at the wrong version is a real mismatch, and that one throws.
mergeInto(LibraryManager.library, {
    $uePkgGuid: function () {
        var ue = Module['UE'];
        var m = ue && ue.pkgguid;
        if (!m) return null;
        if (m.version !== 1) {
            throw new Error('[ue-abi] Module.UE.pkgguid version mismatch (host=' + m.version + ', shim expects 1)');
        }
        return m;
    },

    ue_package_guid__deps: ['$uePkgGuid'],
    ue_package_guid: function (pathPtr, outPtr) {
        var m = uePkgGuid();
        return m ? m.ue_package_guid(pathPtr, outPtr) : 0;
    },
});
