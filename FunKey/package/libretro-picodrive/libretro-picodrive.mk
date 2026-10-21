################################################################################
#
# libretro-picodrive
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_PICODRIVE_VERSION = 3365b1774bc8680be9899968fe45b224ad2f11c1
LIBRETRO_PICODRIVE_SITE = https://github.com/libretro/picodrive.git
LIBRETRO_PICODRIVE_SITE_METHOD = git
LIBRETRO_PICODRIVE_GIT_SUBMODULES = YES
LIBRETRO_PICODRIVE_LICENSE = PicoDrive license (non-commercial)
LIBRETRO_PICODRIVE_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_PICODRIVE_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) -f Makefile.libretro \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_REVISION=-$(shell echo $(LIBRETRO_PICODRIVE_VERSION) | cut -c1-7)
endef

define LIBRETRO_PICODRIVE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/picodrive_libretro.so $(TARGET_DIR)/usr/games/picodrive_libretro.so
endef

$(eval $(generic-package))
