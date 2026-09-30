# Native PS3 diagnostic build. Requires PS3DEV/PSL1GHT in the environment.
.SUFFIXES:
.DEFAULT_GOAL := all

ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment")
endif

PS3_MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
include $(PSL1GHT)/ppu_rules

TARGET      := movebeat
BUILD       := build
SOURCES     := . ../core/src
RSX_NOTES   ?= 0
ifeq ($(RSX_NOTES),1)
SHADERS     := shaders
else
SHADERS     :=
endif
DATA        :=
INCLUDES    := . ../core/include

TITLE       := MoveBeat
APPID       := MOVEBEAT1
CONTENTID   := UP0001-$(APPID)_00-0000000000000000
ICON0       := $(PS3_MAKEFILE_DIR)package/ICON0.PNG

CFLAGS       = $(CFLAGS_INIT) -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS     = $(CFLAGS) $(CXXFLAGS_INIT) -std=gnu++11 \
               $(if $(filter 1,$(RSX_NOTES)),-DMOVEBEAT_EXPERIMENTAL_RSX_NOTES,)
LDFLAGS      = $(MACHDEP) -Wl,-Map,$@.map
LIBS        := -lvorbisfile -lvorbis -logg -laudio -lsimdmath -lrsx \
               -lgcm_sys -lio -lsysutil -lrt -llv2 -lm -lcamera -lgem \
               -lspurs -lsysmodule
LIBDIRS     := $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT    := $(CURDIR)/$(TARGET)
export VPATH     := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(CURDIR)/../tests
export VPATH     += $(CURDIR)/shaders
export DEPSDIR   := $(CURDIR)/$(BUILD)
export BUILDDIR  := $(CURDIR)/$(BUILD)
export PKGFILES  := $(CURDIR)/package

CPPFILES := main.cpp framebuffer.cpp library_card.cpp move_input.cpp demo_input.cpp gem_runtime.cpp audio_player.cpp \
            run_diagnostics.cpp newlib_malloc_lock.cpp chart.cpp game.cpp level.cpp \
            records.cpp settings.cpp
ifeq ($(RSX_NOTES),1)
VCGFILES := note3d.vcg
FCGFILES := note3d.fcg
else
VCGFILES :=
FCGFILES :=
endif
VPOFILES := $(VCGFILES:.vcg=.vpo)
FPOFILES := $(FCGFILES:.fcg=.fpo)

export LD       := $(CXX)
export OFILES   := $(CPPFILES:.cpp=.o) $(VPOFILES:.vpo=.vpo.o) $(FPOFILES:.fpo=.fpo.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   $(LIBPSL1GHT_INC) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) $(LIBPSL1GHT_LIB)

.PHONY: all $(BUILD) clean run pkg core-tests core-tests-pkg

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).pkg $(OUTPUT).elf.map

run: $(BUILD)
	ps3load $(OUTPUT).self

# Dispatch packaging only after the recursive compiler/linker has finished.
# Sibling prerequisites here would race when the caller uses make -j.
pkg: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile package

core-tests:
	@[ -d $(BUILD) ] || mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile core-tests

core-tests-pkg:
	@[ -d $(BUILD) ] || mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile core-tests-pkg

else

DEPENDS := $(OFILES:.o=.d)

.PHONY: all package core-tests core-tests-pkg
all: $(OUTPUT).self
package: $(OUTPUT).pkg
core-tests: $(OUTPUT)-tests.self
core-tests-pkg: $(OUTPUT)-tests.pkg

TEST_PKGDIR := $(BUILDDIR)/test-pkg-stage
TEST_CONTENTID := UP0001-MOVEBTST1_00-0000000000000000

$(OUTPUT)-tests.pkg: $(OUTPUT)-tests.self
	$(VERB) echo building isolated test pkg ... $(notdir $@)
	$(VERB) rm -fr $(TEST_PKGDIR)
	$(VERB) mkdir -p $(TEST_PKGDIR)/USRDIR
	$(VERB) cp $(ICON0) $(TEST_PKGDIR)/ICON0.PNG
	$(VERB) $(SELF_NPDRM) $(BUILDDIR)/$(basename $(notdir $<)).elf \
		$(TEST_PKGDIR)/USRDIR/EBOOT.BIN $(TEST_CONTENTID) >> /dev/null
	$(VERB) $(SFO) --title "MoveBeat Core Tests" --appid "MOVEBTST1" \
		-f $(SFOXML) $(TEST_PKGDIR)/PARAM.SFO
	$(VERB) $(PKG) --contentid $(TEST_CONTENTID) $(TEST_PKGDIR)/ $@ >> /dev/null
	$(VERB) cp $@ $(basename $@).gnpdrm.pkg
	$(VERB) $(PACKAGE_FINALIZE) $(basename $@).gnpdrm.pkg

$(OUTPUT)-tests.elf: game_tests.o gem_runtime_tests.o gem_runtime.o move_input.o newlib_malloc_lock.o chart.o game.o level.o records.o settings.o
	$(LD) $^ $(LDFLAGS) $(LIBPATHS) -lm -llv2 -o $@

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
$(OUTPUT).pkg: $(OUTPUT).self $(wildcard $(PKGFILES)/USRDIR/levels/*/*)

ifeq ($(RSX_NOTES),1)
framebuffer.o: note3d.vpo.o note3d.fpo.o
endif

%.vpo.o: %.vpo
	$(VERB) echo $(notdir $<)
	$(bin2o)

%.fpo.o: %.fpo
	$(VERB) echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)
-include game_tests.d gem_runtime_tests.d gem_runtime.d

endif
