################################################################################
#
# libretro-mednafen-pce-fast
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_MEDNAFEN_PCE_FAST_VERSION = 6d182b22f6b9430c76ea71579ffb2eee0e2e9521
LIBRETRO_MEDNAFEN_PCE_FAST_SITE = https://github.com/libretro/beetle-pce-fast-libretro.git
LIBRETRO_MEDNAFEN_PCE_FAST_SITE_METHOD = git
LIBRETRO_MEDNAFEN_PCE_FAST_LICENSE = GPL-2.0
LIBRETRO_MEDNAFEN_PCE_FAST_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_MEDNAFEN_PCE_FAST_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_MEDNAFEN_PCE_FAST_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_MEDNAFEN_PCE_FAST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mednafen_pce_fast_libretro.so $(TARGET_DIR)/usr/games/mednafen_pce_fast_libretro.so
endef

$(eval $(generic-package))
