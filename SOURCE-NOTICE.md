Purpose: isolated CPU-only Canvas alpha regression candidate for our own multi-store browser.

Public source inputs:
- Chromium155.0.8059.39 snapshot4677e69ea147ee4627dd7e0ee8f9b87a09ab847f.
- Fury native canvas patch from e7dcc1874861258f1639dd301586e60f17bdd28a, core/patches/0030-canvas-noise.patch; blob43300f9f868337799c18395e59ad7b930042ec0f.
- Local vendor baseline e7d61266f16c62b8349e633fe524146d51501619 and independent candidate7c5869a7982b8768f2a0f5404895789de2df5807 are captured file snapshots, not complete Chromium commits.
- Chromium DEPS pins Skia29ed1e87a0a50f3d8347e988842d9d59e7573efa.
- Skia fetch-gn selects GN b2afae122eeb6ce09c52d63f67dc53fc517dbdc8; this candidate plans to compile that source directly from gn.googlesource.com.
- Source GN bootstrap was reviewed through a third-party mirror with matching commit/blob IDs because primary-host reads and CIPD resolution timed out locally. Runtime fetch uses primary host only and verifies exact commit; no automatic mirror or mutable-branch fallback.

Native source copyrights and the accompanying BSD license texts are preserved under native/. No Fury agent/server/desktop/build scripts are included or executed. NATIVE-SOURCES.json records individual copied source hashes. Newly written Python/C++ test support is only an experimental verification harness.

Boundaries:
The harness extracts the actual baseline/candidate ImageDataBuffer method, declaration and relevant HTML caller fragment. Real SkPixmap conversions are delegated to fixed Skia. Explicit fault injection uses a thin SkPixmap subclass. ImageDataBuffer envelope, WTF Vector, base::span, configuration and String/encoded-continuation are substitutes. No actual PNG encoder, Blink translation unit, GPU or browser runtime is covered. The static Skia library is planned as a release CPU build; ASan/UBSan instrument the extracted method/test executable, not the whole library.

No browser binary, merchant Profile/Cookie, license key, credentials or private history is part of this payload. No full Chromium build or product release is authorized by this local preparation.
