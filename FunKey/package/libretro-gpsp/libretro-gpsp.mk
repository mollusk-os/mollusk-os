################################################################################
#
# libretro-gpsp
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_GPSP_VERSION = 6250979f01db3f9ee5ebca179316e1319f017b48
LIBRETRO_GPSP_SITE = https://github.com/libretro/gpsp.git
LIBRETRO_GPSP_SITE_METHOD = git
LIBRETRO_GPSP_LICENSE = GPL-2.0
LIBRETRO_GPSP_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_GPSP_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='"$(shell echo $(LIBRETRO_GPSP_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_GPSP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gpsp_libretro.so $(TARGET_DIR)/usr/games/gpsp_libretro.so
endef

$(eval $(generic-package))
