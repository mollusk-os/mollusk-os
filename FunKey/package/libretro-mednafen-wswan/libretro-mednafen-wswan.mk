################################################################################
#
# libretro-mednafen-wswan
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_MEDNAFEN_WSWAN_VERSION = 2aeb47d3a58bf0360c686f842d9bb5bd201306fe
LIBRETRO_MEDNAFEN_WSWAN_SITE = https://github.com/libretro/beetle-wswan-libretro.git
LIBRETRO_MEDNAFEN_WSWAN_SITE_METHOD = git
LIBRETRO_MEDNAFEN_WSWAN_LICENSE = GPL-2.0
LIBRETRO_MEDNAFEN_WSWAN_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_MEDNAFEN_WSWAN_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_MEDNAFEN_WSWAN_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_MEDNAFEN_WSWAN_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mednafen_wswan_libretro.so $(TARGET_DIR)/usr/games/mednafen_wswan_libretro.so
endef

$(eval $(generic-package))
