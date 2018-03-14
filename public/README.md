GN integration for Zircon
=========================

This directory hosts generated GN files for Zircon. These files are created
automatically as part of the build by `//build/zircon/create_gn_rules.py` and
should never be manually edited.

In order to expose a Zircon module to GN, set the `MODULE_PACKAGE` attribute in
its `rules.mk` build file. The possible values are:
 - `src`: the module's sources are published;
 - `shared`: the module is exposed as a precompiled shared library;
 - `static`: the module is exposed as a precompiled static library;
Note that this currently only applies to `ulib` modules.
