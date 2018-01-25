# Copyright 2018 Open Trust Group
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Project file to build gzOS ARM64 userspace only.

ARCH := arm64

TARGET := user
PLATFORM := user

include kernel/project/virtual/otg/gzos.mk
