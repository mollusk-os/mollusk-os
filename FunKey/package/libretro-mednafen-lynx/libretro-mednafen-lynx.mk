################################################################################
#
# libretro-mednafen-lynx
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_MEDNAFEN_LYNX_VERSION = efd1797c7aa5a83c354507b1b61ac24222ebaa58
LIBRETRO_MEDNAFEN_LYNX_SITE = https://github.com/libretro/beetle-lynx-libretro.git
LIBRETRO_MEDNAFEN_LYNX_SITE_METHOD = git
LIBRETRO_MEDNAFEN_LYNX_LICENSE = GPL-2.0
LIBRETRO_MEDNAFEN_LYNX_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_MEDNAFEN_LYNX_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_MEDNAFEN_LYNX_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_MEDNAFEN_LYNX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mednafen_lynx_libretro.so $(TARGET_DIR)/usr/games/mednafen_lynx_libretro.so
endef

$(eval $(generic-package))
