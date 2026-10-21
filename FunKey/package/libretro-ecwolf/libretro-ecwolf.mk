################################################################################
#
# libretro-ecwolf
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_ECWOLF_VERSION = c57ad894d5942740b4896511e8554c9a776b04a6
LIBRETRO_ECWOLF_SITE = https://github.com/libretro/ecwolf.git
LIBRETRO_ECWOLF_SITE_METHOD = git
LIBRETRO_ECWOLF_LICENSE = GPL-2.0+, Id Software license, MAME license (non-commercial)
LIBRETRO_ECWOLF_LICENSE_FILES = docs/license-gpl.txt docs/license-id.txt docs/license-mame.txt

# The deps/SDL* submodules point to bitbucket repos that are gone, and the
# libretro build does not use them: only fetch libretro-common, at the commit
# the src/libretro/libretro-common submodule records
LIBRETRO_ECWOLF_LIBRETRO_COMMON_VERSION = 996376e36d3f4f56eba202cb96230568628d2583
LIBRETRO_ECWOLF_EXTRA_DOWNLOADS = \
	https://github.com/libretro/libretro-common/archive/$(LIBRETRO_ECWOLF_LIBRETRO_COMMON_VERSION).tar.gz

define LIBRETRO_ECWOLF_EXTRACT_LIBRETRO_COMMON
	mkdir -p $(@D)/src/libretro/libretro-common
	$(call suitable-extractor,$(LIBRETRO_ECWOLF_LIBRETRO_COMMON_VERSION).tar.gz) \
		$(LIBRETRO_ECWOLF_DL_DIR)/$(LIBRETRO_ECWOLF_LIBRETRO_COMMON_VERSION).tar.gz | \
		$(TAR) -C $(@D)/src/libretro/libretro-common --strip-components=1 $(TAR_OPTIONS) -
endef
LIBRETRO_ECWOLF_POST_EXTRACT_HOOKS += LIBRETRO_ECWOLF_EXTRACT_LIBRETRO_COMMON

# Same recipe as picoarch's cores target for platform=funkey-s
define LIBRETRO_ECWOLF_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D)/src/libretro \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_ECWOLF_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/src/libretro/ecwolf_libretro.so $(TARGET_DIR)/usr/local/share/OPKs/Libretro/cores/ecwolf_libretro.so
endef

$(eval $(generic-package))
