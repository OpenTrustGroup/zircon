# Copyright 2018 Open Trust Group
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Project file to build gzOS + user space on top of qemu
# for 64bit arm (cortex-a53)

ARCH := arm64
TARGET := arm64

include kernel/project/virtual/test.mk
include kernel/project/virtual/otg/gzos.mk

