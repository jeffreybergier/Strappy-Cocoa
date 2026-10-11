# Compatibility entry point: root Makefile owns orchestration and root discovery.
PROJECT_ROOT := $(abspath $(dir $(firstword $(MAKEFILE_LIST)))/..)
.DEFAULT_GOAL := release
_goals := $(or $(MAKECMDGOALS),release)
.PHONY: $(_goals) _forward
$(_goals): _forward ;
_forward:
	+$(MAKE) -C "$(PROJECT_ROOT)" $(_goals)
