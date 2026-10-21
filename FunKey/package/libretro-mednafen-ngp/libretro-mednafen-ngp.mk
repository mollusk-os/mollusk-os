################################################################################
#
# libretro-mednafen-ngp
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_MEDNAFEN_NGP_VERSION = 139fe34c8dfc5585d6ee1793a7902bca79d544de
LIBRETRO_MEDNAFEN_NGP_SITE = https://github.com/libretro/beetle-ngp-libretro.git
LIBRETRO_MEDNAFEN_NGP_SITE_METHOD = git
LIBRETRO_MEDNAFEN_NGP_LICENSE = GPL-2.0
LIBRETRO_MEDNAFEN_NGP_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_MEDNAFEN_NGP_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_MEDNAFEN_NGP_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_MEDNAFEN_NGP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mednafen_ngp_libretro.so $(TARGET_DIR)/usr/games/mednafen_ngp_libretro.so
endef

$(eval $(generic-package))
