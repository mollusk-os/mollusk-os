################################################################################
#
# libretro-pcsx-rearmed
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_PCSX_REARMED_VERSION = 32e2193bcc9d937d9b1a16db43258aef51cddae8
LIBRETRO_PCSX_REARMED_SITE = https://github.com/libretro/pcsx_rearmed.git
LIBRETRO_PCSX_REARMED_SITE_METHOD = git
LIBRETRO_PCSX_REARMED_GIT_SUBMODULES = YES
LIBRETRO_PCSX_REARMED_LICENSE = GPL-2.0
LIBRETRO_PCSX_REARMED_LICENSE_FILES = COPYING

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_PCSX_REARMED_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D) -f Makefile.libretro \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_PCSX_REARMED_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_PCSX_REARMED_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pcsx_rearmed_libretro.so $(TARGET_DIR)/usr/games/pcsx_rearmed_libretro.so
endef

$(eval $(generic-package))
