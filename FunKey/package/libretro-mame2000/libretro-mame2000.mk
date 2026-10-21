################################################################################
#
# libretro-mame2000
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_MAME2000_VERSION = 2ec60f6e1078cf9ba173e80432cc28fd4eea200f
LIBRETRO_MAME2000_SITE = https://github.com/libretro/mame2000-libretro.git
LIBRETRO_MAME2000_SITE_METHOD = git
LIBRETRO_MAME2000_LICENSE = MAME license (non-commercial)
LIBRETRO_MAME2000_LICENSE_FILES = readme.txt

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_MAME2000_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_MAME2000_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_MAME2000_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mame2000_libretro.so $(TARGET_DIR)/usr/games/mame2000_libretro.so
endef

$(eval $(generic-package))
