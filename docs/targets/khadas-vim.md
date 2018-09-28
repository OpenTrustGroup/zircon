# Zircon on Khadas VIM2 Board

This document describes running Zircon on the Khadas VIM2 board.
Additional documentation can be found at [docs.khadas.com](http://docs.khadas.com/)

When describing the location of buttons, pins and other items on the board,
we will refer to the side with the USB, ethernet and HDMI connectors as the front of the board
and the opposite side the back of the board.

## Heat Sink

Before you start, you need a heat sink. A passive chip heat sink will allow you
to run 2 cores out of 8 at full speed before reaching 80C, the critical
temperature at which cores have to be throttled down.

## Setup

- USB C port: Connect to host. Provides power and `fastboot`.
- Ethernet: Connect cable directly to board (do not use a USB ethernet adapter).
- HDMI: Optional. Connects to display.
- Serial Console: Optional but very useful. See next section.

## Serial Console

The debug UART for the serial console is exposed on the 40 pin header at the back of the board.
You may use a 3.3v FTDI USB to serial cable to access the serial console.
On the front row of the header:

- 2nd from right: TX (Yellow wire)
- 3rd from right: RX (Orange wire)
- 4th from right: Ground (Black wire)

For FTDI serial cables with black, white, red and green wires, use this:

- 2nd from right: TX (White wire)
- 3rd from right: RX (Green wire)
- 4th from right: Ground (Black wire)

In [this diagram](http://docs.khadas.com/vim1/GPIOPinout.html) of the 40 pin header,
these correspond to pins 17 through 19.

## Buttons

The VIM2 has 3 buttons on the left side of the board. On the board schematic, SW1 (switch closest to the USB plug) is the reset switch. SW3 (farthest away from the USB plug on the schematic) can be used for entering flashing mode. If SW3 is held down while the board is reset or power cycled , the bootloader will enter flashing mode instead of booting the kernel normally.

## VIM2 Bootloader

Booting Zircon on the VIM2 requires a custom bootloader.

### [Googlers only]
Within Google, this can be found at [go/vim2-bootloader](http://go/vim2-bootloader). Download the .bin file and follow the instructions in the document.

If you are not at Google, hang on until we make this publicly available.

To find out what version of the bootloader you have, grep for "fuchsia-bootloader"
in the kernel boot log. You should see something like: "cmdline: fuchsia-bootloader=0.04"

## Building Zircon

```
make -j32 arm64
```

Be sure you've already set up your network before proceeding to the next step.

## Flashing Zircon

First enter fastboot mode by holding down SW3 (leftmost button), pressing SW1 (rightmost button) quickly and keeping pressing SW3 for a few seconds.

If you are working from the zircon layer, cd to the zircon directory and run:

```
scripts/flash-vim2 -m
```

The device should boot into zedboot by default.

If you are working from the garnet layer of above, run the following:

```
fx flash vim2 --pave
```

In order to get into zedboot you can reboot into the recovery:

```
dm reboot-recovery
```

Alternatively, you can get to zedboot by reseting your vim2 by pressing SW1(rightmost button) quickly and keeping pressing SW2 for a few seconds.

### netbooting

To netboot zircon, enter zedboot and run the following under the zircon directory:

```
scripts/netboot-zircon ./build-arm64
```

To netboot garnet, run the following under the fuchsia directory:

```
fx set arm64 --netboot && fx full-build && fx boot --netboot -- -1
```

You should be able to see "Issued boot command to ..." message printed out if this step is successful.

### Paving

Paving is available from garnet layers and above. Run the following under the fuchsia directory:

```
fx set arm64 && fx full-build && fx boot arm -1
```

### Fuchsia logo

To update the the boot splash screen to be the Fuchsia logo, do this in fastboot mode:
```
fastboot flash logo kernel/target/arm64/board/vim2/firmware/logo.img
```
