################################################################################
#
# libretro-fbalpha2012
#
################################################################################

# Commit the official FunKey-OS binary was built from
LIBRETRO_FBALPHA2012_VERSION = c547d8cf3f7748f4094cee658a5d31ec1b79ece4
LIBRETRO_FBALPHA2012_SITE = https://github.com/libretro/fbalpha2012.git
LIBRETRO_FBALPHA2012_SITE_METHOD = git
LIBRETRO_FBALPHA2012_LICENSE = FB Alpha license (non-commercial)
LIBRETRO_FBALPHA2012_LICENSE_FILES = docs/license.txt

# Same recipe as picoarch's cores target for platform=funkey-s, with the
# version string the core reports set as a git checkout of it would set it
define LIBRETRO_FBALPHA2012_BUILD_CMDS
	$(TARGET_MAKE_ENV) GIT_CEILING_DIRECTORIES=$(BUILD_DIR) $(MAKE) -C $(@D)/svn-current/trunk -f makefile.libretro \
		platform=unix-armv7-hardfloat-neon \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
		GIT_VERSION='" $(shell echo $(LIBRETRO_FBALPHA2012_VERSION) | cut -c1-7)"'
endef

define LIBRETRO_FBALPHA2012_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/svn-current/trunk/fbalpha2012_libretro.so $(TARGET_DIR)/usr/games/fbalpha2012_libretro.so
endef

$(eval $(generic-package))
