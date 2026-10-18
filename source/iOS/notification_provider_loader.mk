# Foreground staging diagnostic only, not part of any installed package.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath ../..)
PRODUCTS = strappy-provider-loader
strappy-provider-loader_TYPE = executable
strappy-provider-loader_SOURCES = ../daemon/tests/notification_provider_device/loader.m
strappy-provider-loader_LDFLAGS = -framework Foundation -framework CoreFoundation
strappy-provider-loader_SIGN = 1
strappy-provider-loader_PACKAGES = zip
include $(ALTIVEC_ROOT)/altivec_common_phone.mk
endif
