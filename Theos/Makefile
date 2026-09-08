## MobileDumper-7 — iOS Theos Tweak Makefile
## Build from this directory: make package

THEOS_PACKAGE_SCHEME ?= roothide
TARGET  = iphone:clang:latest:14.0
ARCHS   = arm64 #arm64e
DEBUG   ?= 0
FINALPACKAGE ?= $(DEBUG)

KITTYMEMORY_PATH = ../libs/KittyMemory/KittyMemory
KITTYMEMORY_SRC  = $(wildcard $(KITTYMEMORY_PATH)/*.cpp)

FMT_PATH = ../libs/fmt

DUMPER_PATH = ../Dumper
DUMPER_SRC = $(shell find $(DUMPER_PATH) -name "*.cpp" -o -name "*.mm" -o -name "*.c" \
               | grep -v "MemoryAndroid" \
               | grep -v "ProfileAndroid" \
               | grep -v "main_android")

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = MobileDumper-7

$(TWEAK_NAME)_FILES = $(DUMPER_SRC) $(KITTYMEMORY_SRC)

ifeq ($(DEBUG),1)
_OPT_CCFLAGS = -O0 -g3 -fno-omit-frame-pointer -DDEBUG
else
_OPT_CCFLAGS = -O3 -g0 -fomit-frame-pointer -DNDEBUG 
endif

DISABLED_WARNINGS = -Wno-unused-parameter \
    -Wno-unused-function \
    -Wno-missing-field-initializers \
    -Wno-missing-designated-field-initializers \
    -Wno-deprecated-declarations

$(TWEAK_NAME)_CCFLAGS = \
    -std=c++20 \
    -DFMT_HEADER_ONLY \
    -DkNO_KEYSTONE \
    -fexceptions \
    -fvisibility=hidden \
    -fvisibility-inlines-hidden \
    -fobjc-arc \
    $(_OPT_CCFLAGS) \
    -I$(FMT_PATH)/include \
    -I$(KITTYMEMORY_PATH)/.. \
    $(DISABLED_WARNINGS)

$(TWEAK_NAME)_FRAMEWORKS = Foundation UIKit CoreGraphics QuartzCore

include $(THEOS_MAKE_PATH)/tweak.mk

before-package::
	@find "$(THEOS_STAGING_DIR)" -name '.DS_Store' -delete 2>/dev/null || true

after-clean::
	@rm -rf .theos
	@rm -rf packages
	@mkdir -p .theos
	@touch .theos/build_session

# Copy the debug symbols next to the package, named after it.
after-package::
	@DEB="$(__THEOS_LAST_PACKAGE_FILENAME)"; \
	DSYM="$$(find "$(THEOS_OBJ_DIR)" -type d -name '$(TWEAK_NAME).dylib.dSYM' 2>/dev/null | sort | head -1)"; \
	if [ -z "$$DEB" ]; then \
		echo "==> dSYM: skipped, no package filename"; \
	elif [ -z "$$DSYM" ]; then \
		if [ -d "$${DEB%.deb}.dSYM" ]; then \
			echo "==> dSYM: kept $${DEB%.deb}.dSYM, nothing relinked"; \
		else \
			echo "==> dSYM: skipped, none found under $(THEOS_OBJ_DIR)"; \
		fi; \
	else \
		OUT="$${DEB%.deb}.dSYM"; \
		rm -rf "$$OUT"; \
		cp -R "$$DSYM" "$$OUT"; \
		mv "$$OUT/Contents/Resources/DWARF/$(TWEAK_NAME).dylib" \
		   "$$OUT/Contents/Resources/DWARF/$$(basename "$${OUT%.dSYM}")"; \
		echo "==> dSYM: $$OUT"; \
	fi

after-install::
	install.exec "killall -9 SpringBoard"