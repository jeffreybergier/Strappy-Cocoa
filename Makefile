PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ALTIVEC_ROOT ?= /altivec
PLATFORMS = macOS iOS
PROJECT_macOS_DIR = source/macOS
PROJECT_iOS_DIR = source/iOS
TEST_HOST_COMMAND = $(MAKE) -C $(PROJECT_ROOT)/source/daemon/tests test
include $(ALTIVEC_ROOT)/make/projects.mk
