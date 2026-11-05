# Personal Windows build on Telegram Desktop 7.3.5

This branch is an experimental port of AyuGram Desktop, not an official AyuGram release.

Source inputs:
- AyuGram Desktop: 0f2a31bb420de98f331990432265b131b8d836b5
- Telegram Desktop 7.3.5: 3a2a9a34e833d3243a13787215b8a7cdcf5dc3c3

The application merge conflicts were resolved manually. The updated lib_ui and codegen submodules use the pinned upstream versions and checked-in AyuGram patches in build/personal. The workflow verifies the submodule revisions before applying those patches. The MSYS2 diffutils package name was corrected.

The upstream profile music layout is retained; the obsolete AyuGram saved-music layout helper is removed. A conflicting display-only message flag was moved to an unused bit and the display flags widened to uint32.

Windows builds use Debug configuration, with automatic updates and crash reporting disabled. The API parameters are the public parameters documented by the AyuGram project in docs/api_credentials.md.

Validation so far: workflow YAML parsing, absence of application conflict markers, clean application of the library patches to the pinned upstream files, and an exact match between the local source tree and its uploaded GitHub tree. Compilation and runtime validation are still pending. Do not treat this source snapshot as a tested release or security audit.
