# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

FIDL_DIR := $(patsubst %/compiler,%,$(LOCAL_DIR))

MODULE := $(LOCAL_DIR)

# Built as fidlc-legacy, beside this zircon's fidlc; exported as fidlc.
MODULE_NAME := fidlc-legacy

MODULE_PACKAGE_NAME := fidlc

MODULE_TYPE := hostapp

MODULE_COMPILEFLAGS := -O0 -g -I$(FIDL_DIR)/include

# The library's sources are built into the tool rather than as a host
# library, whose name would be this zircon's fidl library's.
MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \
    $(FIDL_DIR)/lib/c_generator.cpp \
    $(FIDL_DIR)/lib/error_reporter.cpp \
    $(FIDL_DIR)/lib/flat_ast.cpp \
    $(FIDL_DIR)/lib/identifier_table.cpp \
    $(FIDL_DIR)/lib/json_generator.cpp \
    $(FIDL_DIR)/lib/lexer.cpp \
    $(FIDL_DIR)/lib/names.cpp \
    $(FIDL_DIR)/lib/parser.cpp \
    $(FIDL_DIR)/lib/source_file.cpp \
    $(FIDL_DIR)/lib/source_location.cpp \
    $(FIDL_DIR)/lib/source_manager.cpp \
    $(FIDL_DIR)/lib/tables_generator.cpp \

MODULE_PACKAGE := bin

include make/module.mk
