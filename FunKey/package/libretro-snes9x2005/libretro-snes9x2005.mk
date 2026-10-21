################################################################################
#
# libretro-snes9x2005
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_SNES9X2005_VERSION = 74d871db9b4dba6dbe6c5ecebc88cbf255be5349
LIBRETRO_SNES9X2005_SITE = https://github.com/libretro/snes9x2005.git
LIBRETRO_SNES9X2005_SITE_METHOD = git
LIBRETRO_SNES9X2005_LICENSE = Snes9x license (non-commercial)
LIBRETRO_SNES9X2005_LICENSE_FILES = copyright

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_SNES9X2005_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_SNES9X2005_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_SNES9X2005_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/snes9x2005_libretro.so $(TARGET_DIR)/usr/games/snes9x2005_libretro.so
endef

$(eval $(generic-package))
