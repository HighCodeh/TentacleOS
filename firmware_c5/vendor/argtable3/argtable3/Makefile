################################################################################
# This file is part of the argtable3 library.
#
# Copyright (C) 2014-2025 Tom G. Huang
# <tomghuang@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of STEWART HEITMANN nor the  names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL STEWART HEITMANN BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
################################################################################

DOCKER_IMAGE_NAME     := argtable3-builder
DOCKER_CONTAINER_NAME := argtable3-builder-container

BUILD_DIR             := build
OUTPUT_DIR	          := output
DOCS_DIR              := docs
DOXYGEN_XML_DIR       := docs/source/xml
DOCS_BUILD_DIR        := docs/build
ARCHIVE_DIR           := .archive
AMALGAMATION_DIST_DIR := dist
MAKEFILE_PATH         := $(abspath $(lastword $(MAKEFILE_LIST)))
MAKEFILE_DIR          := $(abspath $(dir $(MAKEFILE_PATH)))

# For Windows, we don't use --user as it can cause issues with Docker Desktop's
# volume mounting. Docker Desktop handles permissions for mounted volumes.
ifeq ($(OS),Windows_NT)
	MAKEFILE_PATH := $(shell cygpath -m $(MAKEFILE_PATH))
	MAKEFILE_DIR := $(shell cygpath -m $(MAKEFILE_DIR))
	PLATFORM_CURDIR := $(shell cygpath -m $(CURDIR))
else
	PLATFORM_CURDIR := $(CURDIR)
endif

IN_CONTAINER := $(shell \
    if [ -f /.dockerenv ] || env | grep -q container; then \
        echo "true"; \
    else \
        echo "false"; \
    fi \
)

ifeq ($(IN_CONTAINER),true)
    DOCKER_CMD_PREFIX :=
else
    DOCKER_CMD_PREFIX := docker run --rm -v "$(PLATFORM_CURDIR):/workdir" -w /workdir $(DOCKER_IMAGE_NAME)
endif


.PHONY: help
help: build-docker-image
	@$(DOCKER_CMD_PREFIX) sh -c "printf \"Usage: make <target> [options]\n\
  make help                   - Display this message (default)\n\
  make co TAG=<TAG_NAME>      - Checkout the specified tag\n\
  make archive TAG=<TAG_NAME> - Create distribution archives in the .archive directory\n\
  make tag TAG=<TAG_NAME>     - Tag the master branch\n\
  make taglist                - List all available tags\n\
  make clean                  - Remove build artifacts\n\
  make cleanall               - Remove build artifacts and temp files\n\
  make githead                - Show the first 7-digit of the HEAD commit SHA-1\n\
  make build-docker-image     - Build the Docker image for the builder environment\n\
  make output-dir             - Create the host output directory\n\
  make shell                  - Open an interactive shell inside the Docker container\n\
\n\
The <TAG_NAME> format follows the SemVer standard but excludes build metadata:\n\
v<MAJOR>.<MINOR>.<PATCH>[-<PRE_RELEASE>], where the <PRE_RELEASE> field is optional.\n\
Here are some <TAG_NAME> examples (use 'make taglist' to get all available tags):\n\
  v0.0.1\n\
  v1.1.0-alpha.1\n\
  v2.0.0-rc.3\n\""


.PHONY: build-docker-image
build-docker-image: tools/Dockerfile
	@if [ "$(IN_CONTAINER)" != "true" ] && ! docker image inspect $(DOCKER_IMAGE_NAME) >/dev/null 2>&1; then \
		docker buildx build -f tools/Dockerfile -t $(DOCKER_IMAGE_NAME) --load .; \
	fi


.PHONY: output-dir
output-dir: build-docker-image
	@$(DOCKER_CMD_PREFIX) sh -c "mkdir -p output"


.PHONY: co
co: build-docker-image
	@$(DOCKER_CMD_PREFIX) sh -c "\
		printf 'Export tag %s to temp directory %s...\n' $(TAG) $(ARCHIVE_DIR)/argtable-$(TAG) && \
		mkdir -p $(ARCHIVE_DIR) && \
		mkdir -p $(ARCHIVE_DIR)/argtable-$(TAG) \
	"
	@git archive $(TAG) | tar -x -C $(ARCHIVE_DIR)/argtable-$(TAG)
	@$(DOCKER_CMD_PREFIX) sh -c "\
		printf "$(TAG)+git.$(shell git rev-parse --short=7 HEAD)" > $(ARCHIVE_DIR)/argtable-$(TAG)/version.tag \
	"


.PHONY: archive
archive: build-docker-image co
	@$(DOCKER_CMD_PREFIX) bash -c "\
		printf 'Archive tag %s in %s...\n' $(TAG) $(ARCHIVE_DIR)/argtable-$(TAG).zip && \
		find $(ARCHIVE_DIR)/argtable-$(TAG) -type f -exec unix2dos {} \; && \
		pushd $(ARCHIVE_DIR) && \
		zip -rq argtable-$(TAG).zip argtable-$(TAG) && \
		popd && \
		printf '\n************************************************************\n' && \
		printf 'Archive tag %s in %s...\n' $(TAG) $(ARCHIVE_DIR)/argtable-$(TAG).tar.gz && \
		find $(ARCHIVE_DIR)/argtable-$(TAG) -type f -exec dos2unix {} \; && \
		pushd $(ARCHIVE_DIR) && \
		tar -zcf argtable-$(TAG).tar.gz argtable-$(TAG) && \
		popd && \
		printf '\n************************************************************\n' && \
		printf 'Archive tag %s in %s...\n' $(TAG) $(ARCHIVE_DIR)/argtable-$(TAG)-amalgamation.tar.gz && \
		pushd $(ARCHIVE_DIR)/argtable-$(TAG)/tools && \
		./build tar && \
		popd && \
		mv $(ARCHIVE_DIR)/argtable-$(TAG)/tools/argtable-$(TAG)-amalgamation.tar.gz $(ARCHIVE_DIR) && \
		printf '\n************************************************************\n' && \
		printf 'Archive tag %s in %s...\n' $(TAG) $(ARCHIVE_DIR)/argtable-$(TAG)-amalgamation.zip && \
		pushd $(ARCHIVE_DIR)/argtable-$(TAG)/tools && \
		./build zip && \
		popd && \
		mv $(ARCHIVE_DIR)/argtable-$(TAG)/tools/argtable-$(TAG)-amalgamation.zip $(ARCHIVE_DIR) && \
		printf '\n************************************************************\n' && \
		printf 'Clean %s...\n' $(ARCHIVE_DIR)/argtable-$(TAG) && \
		rm -rf $(ARCHIVE_DIR)/argtable-$(TAG) \
	"


.PHONY: docs
docs: build-docker-image
	@$(DOCKER_CMD_PREFIX) bash -c "\
		printf 'Generate documentation...\n' && \
		pushd $(DOCS_DIR) && \
		doxygen && \
		make html \
	"


.PHONY: shell
shell: build-docker-image
	@if [ "$(IN_CONTAINER)" != "true" ]; then \
		$(DOCKER_CMD_PREFIX) sh -c "echo 'Opening shell in Docker container...'"; \
		docker run --rm -it -v "$(PLATFORM_CURDIR):/workdir" -w /workdir $(DOCKER_IMAGE_NAME) bash; \
	else \
		$(DOCKER_CMD_PREFIX) sh -c "echo 'You are already inside the Docker container.'"; \
	fi


.PHONY: tag
tag: build-docker-image
	@git tag -a $(TAG) -m "tagging: $(TAG)"


.PHONY: taglist
taglist: build-docker-image
	@$(DOCKER_CMD_PREFIX) sh -c "printf 'Available tags:\n'"
	@git tag -l


.PHONY: githead
githead: build-docker-image
	@git rev-parse --short=7 HEAD


.PHONY: clean
clean: build-docker-image
	@$(DOCKER_CMD_PREFIX) sh -c "rm -rf $(BUILD_DIR) $(OUTPUT_DIR)"


.PHONY: cleanall
cleanall: build-docker-image
	@$(DOCKER_CMD_PREFIX) sh -c "rm -rf \
		$(BUILD_DIR) \
		$(OUTPUT_DIR) \
		$(DOXYGEN_XML_DIR) \
		$(DOCS_BUILD_DIR) \
		$(ARCHIVE_DIR) \
		$(AMALGAMATION_DIST_DIR) \
	"
