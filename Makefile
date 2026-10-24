# CMake is the single build definition. Override CMAKE, BUILD_DIR, JOBS as needed.
CMAKE ?= cmake
CTEST ?= ctest
BUILD_DIR ?= build
JOBS ?= 2
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Release
# The managed cloud image uses a non-root tool prefix. Native installs need none.
CLOUD_ENV ?= $(wildcard /workspace/eq29-tools/activate.sh)
ACTIVATE = $(if $(CLOUD_ENV),. "$(CLOUD_ENV)" &&,)
.DEFAULT_GOAL := all
.PHONY: all configure test install clean deps
all: configure
	$(ACTIVATE) $(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)
configure:
	$(ACTIVATE) $(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)
test: all
	$(ACTIVATE) $(CTEST) --test-dir $(BUILD_DIR) --output-on-failure
install: all
	$(ACTIVATE) $(CMAKE) --install $(BUILD_DIR) --prefix "$(HOME)/.local"
clean:
	$(ACTIVATE) $(CMAKE) --build $(BUILD_DIR) --target clean
deps:
	git submodule update --init --recursive
