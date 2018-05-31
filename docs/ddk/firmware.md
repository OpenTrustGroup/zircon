# Device Firmware

Device firmware are binary blobs containing code that are executed by device
hardware. The binary blob is available in the driver's namespace for loading.

Device firmware are stored in CIPD (Chrome Infrastructure Package Deployment)
and mirrored in Google Storage.

## Before You Start

Ensure that CIPD is available. cipd must be either in PATH or
`zircon/../buildtools/cipd`.

The buildtools repository is available
[here](https://fuchsia.googlesource.com/buildtools/).

## Create a Firmware Package

To create a firmware package, create a directory containing the following
files:

* One or more firmware files
* A license file
* [README.fuchsia](https://fuchsia.googlesource.com/docs/+/master/development/source_code/README.fuchsia.md)

README.fuchsia must contain at least the following directives:

* `Name`
* `Version`
* `Upstream Git`
* `License`
* `License File`

If this is the first time you uploaded to CIPD from the host system,
authenticate with CIPD:

```
cipd auth-login
```

Upload and tag the package in CIPD using the following command:

```
cipd create -in <package-directory> -install-mode copy \
    -name <package-name> \
    -tag git_repository:<source-git-repositry> \
    -tag git_revision:<source-git-revision>
```

`package-name` has the format `fuchsia/firmware/<name>`.

`<name>` should be a string that identifies the firmware. It may contain
any non-whitespace character. It is helpful to identify the driver that will
use the firmware in the name.

After this step, the package is uploaded to CIPD. Check the
[CIPD browser here](https://chrome-infra-packages.appspot.com/#/?path=fuchsia/firmware)
for packages under fuchsia/firmware.

## Adding the Firmware Package to the Build

Add the following entry in `prebuilt/zircon.ensure`:

```
@Subdir firmware/<name>
<package-name> git_revision:<source-git-revision>
```

Where `<name>`, `<package-name>` and `<source-git-revision>` matches the
values passed to `cipd create` above. The package will be downloaded to
the path specified by `@Subdir` under `prebuilt`, i.e.
`prebuilt/firmware/<name>`.

Next, update `prebuilt/zircon.versions` with the following command:

```
scripts/download-prebuilt && scripts/update-prebuilt-versions
```

Upload this change to Gerrit, but do not send it to CQ yet.

Now file an infra ticket with the URL to the Gerrit change.
(TODO: insert link here) A member of the infra team will check out
your change and upload the package for you.

When the ticket is resolved, the Gerrit change is ready to be sent
to CQ. The firmware package will be downloaded by
`scripts/download-prebuilt` along with the toolchain and QEMU.

Optional: Verify locally the package has been mirroed to Google Storage by
the infra team before sending the patch to CQ:

```
rm -rf prebuilt/downloads
scripts/download-prebuilt --no-cipd
```

## Using the Firmware Package in the Driver

Add the following line to the driver's `rules.mk`:

```
MODULE_FIRMWARE := <name>/<path-to-binary-blob>
```

This will install the firmware to bootfs under
`/boot/lib/firmware/$(basename $(MODULE_FIRMWARE))`.

The `load_firmware()` API, defined in [`driver.h`](../../system/ulib/ddk/include/ddk/driver.h)
loads the firmware pointed to by the path in a VMO.
