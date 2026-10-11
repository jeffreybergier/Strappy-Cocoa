ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath $(dir $(firstword $(MAKEFILE_LIST))))
PLATFORMS = macOS iOS
PROJECT_macOS_DIR = source/macOS
PROJECT_iOS_DIR = source/iOS
TEST_HOST_COMMAND = $(MAKE) -C $(PROJECT_ROOT)/source/daemon/tests test
include $(ALTIVEC_ROOT)/make/projects.mk
endif
