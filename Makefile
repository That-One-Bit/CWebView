PROJECT_ROOT := $(abspath $(CURDIR)/..)
BUILD_DIR := $(CURDIR)/build
VITASDK ?= /usr/local/vitasdk
WEBAPP_DIR ?= $(PROJECT_ROOT)/webapp
WEBPAGE ?= index.html
TITLE_ID ?= CWV00001
VERSION ?= 01.00
APP_NAME ?= CWebView
SHORTNAME ?= CWV
TARGET ?= CWebView
AUTHOR ?= CWebView
.PHONY: all vpk configure clean extension extension-clean
all: vpk
vpk:
	@test -x "$(VITASDK)/bin/arm-vita-eabi-gcc" || (echo "VitaSDK not found at $(VITASDK)"; exit 1)
	@test -f "$(WEBAPP_DIR)/$(WEBPAGE)" || (echo "Webpage entry point not found: $(WEBAPP_DIR)/$(WEBPAGE)"; exit 1)
	cmake -S $(CURDIR) -B $(BUILD_DIR) -DCMAKE_TOOLCHAIN_FILE="$(VITASDK)/share/vita.toolchain.cmake" -DVITA_WEBAPP_DIR="$(WEBAPP_DIR)" -DCWV_WEBPAGE="$(WEBPAGE)" -DVITA_TITLEID="$(TITLE_ID)" -DVITA_VERSION="$(VERSION)" -DVITA_APP_NAME="$(APP_NAME)" -DVITA_SHORTNAME="$(SHORTNAME)" -DVITA_TARGET="$(TARGET)" -DVITA_AUTHOR="$(AUTHOR)"
	cmake --build $(BUILD_DIR) --target $(TARGET)-vpk --clean-first -j$$(nproc)
	cp -f $(BUILD_DIR)/$(TARGET).vpk $(PROJECT_ROOT)/$(TARGET).vpk
configure:
	cmake -S $(CURDIR) -B $(BUILD_DIR) -DCMAKE_TOOLCHAIN_FILE="$(VITASDK)/share/vita.toolchain.cmake" -DVITA_WEBAPP_DIR="$(WEBAPP_DIR)" -DCWV_WEBPAGE="$(WEBPAGE)" -DVITA_TITLEID="$(TITLE_ID)" -DVITA_VERSION="$(VERSION)" -DVITA_APP_NAME="$(APP_NAME)" -DVITA_SHORTNAME="$(SHORTNAME)" -DVITA_TARGET="$(TARGET)" -DVITA_AUTHOR="$(AUTHOR)"
extension:
	cmake -S $(CURDIR)/extensions/esm_modules -B $(BUILD_DIR)/esm_modules -DCMAKE_TOOLCHAIN_FILE="$(VITASDK)/share/vita.toolchain.cmake"
	cmake --build $(BUILD_DIR)/esm_modules --target CwvESM-package
extension-clean:
	rm -rf $(BUILD_DIR)/es6_modules
clean:
	./clean.sh -y
	@rm -f $(PROJECT_ROOT)/$(TARGET).vpk
