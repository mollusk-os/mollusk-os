################################################################################
#
# libretro-gambatte
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_GAMBATTE_VERSION = 9fe223d9c4b615c55840170c6e85e6e9fa4bd1d2
LIBRETRO_GAMBATTE_SITE = https://github.com/libretro/gambatte-libretro.git
LIBRETRO_GAMBATTE_SITE_METHOD = git
LIBRETRO_GAMBATTE_LICENSE = GPL-2.0
LIBRETRO_GAMBATTE_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_GAMBATTE_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_GAMBATTE_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_GAMBATTE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gambatte_libretro.so $(TARGET_DIR)/usr/games/gambatte_libretro.so
endef

$(eval $(generic-package))
