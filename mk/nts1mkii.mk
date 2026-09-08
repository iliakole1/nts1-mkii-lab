##############################################################################
# Shared build wrapper for out-of-tree NTS-1 mkII units.
#
# A unit's Makefile only needs:
#
#   include ../../mk/nts1mkii.mk
#
# Everything below points the stock logue-sdk build at this repo's layout:
# sources stay here, the SDK stays a pristine submodule.
#

MK_SELF   := $(realpath $(lastword $(MAKEFILE_LIST)))
REPO_ROOT := $(realpath $(dir $(MK_SELF))/..)

SDKDIR      ?= $(REPO_ROOT)/logue-sdk
PLATFORMDIR := $(SDKDIR)/platform/nts-1_mkii

# Overrides for the SDK Makefile (all of its own defaults use ?=)
PROJECT_ROOT    := $(CURDIR)
COMMON_INC_PATH := $(PLATFORMDIR)/common
COMMON_SRC_PATH := $(PLATFORMDIR)/common
TOOLSDIR        := $(SDKDIR)/tools
EXTDIR          := $(SDKDIR)/platform/ext
LDDIR           := $(PLATFORMDIR)/ld
SANDBOXDIR      := $(SDKDIR)/websim
INSTALLDIR      ?= $(REPO_ROOT)/dist

# PROJECT_TYPE lives in the unit's config.mk. The SDK Makefile includes that
# itself, but we need the value first to pick which of its dummy Makefiles to
# include. config.mk only ever assigns, so reading it twice is harmless.
include $(CURDIR)/config.mk

# The SDK's four dummy Makefiles are byte-identical except for the websim shell
# they hand emscripten: osc.html for oscillators, fx.html for effects. Picking
# the matching one is what makes `make wasm` open a sandbox with an audio
# source in it for a modfx/delfx/revfx unit instead of a keyboard.
ifeq ($(PROJECT_TYPE),osc)
  SDK_TEMPLATE := dummy-osc
else
  SDK_TEMPLATE := dummy-$(PROJECT_TYPE)
endif

include $(PLATFORMDIR)/$(SDK_TEMPLATE)/Makefile

# The SDK's install target moves the unit into INSTALLDIR but never creates it.
install: | $(INSTALLDIR)

$(INSTALLDIR):
	@mkdir -p $@
