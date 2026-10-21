################################################################################
#
# eduke32
#
################################################################################

EDUKE32_VERSION = 08ae09155efa6dec8b58bcf1ca44717d90cef199
EDUKE32_SITE = https://github.com/mollusk-os/eduke32.git
EDUKE32_SITE_METHOD = git
EDUKE32_LICENSE = GPL-2.0, BUILD license (non-commercial)
EDUKE32_LICENSE_FILES = polymer/eduke32/GNU.TXT polymer/eduke32/buildlic.txt

# libvorbis/libvorbisfile are only needed at link time: they come with the SDK
# sysroot in staging, and the OPK ships its own copies for the target
EDUKE32_DEPENDENCIES = sdl sdl_mixer libmikmod libogg host-squashfs

EDUKE32_SRC_DIR = $(@D)/polymer/eduke32
EDUKE32_OPK_DIR = $(TARGET_DIR)/usr/local/share/OPKs/Native games

define EDUKE32_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(EDUKE32_SRC_DIR) \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" LD="$(TARGET_LD)" \
		AR="$(TARGET_AR)" RANLIB="$(TARGET_RANLIB)" STRIP="$(TARGET_STRIP)" \
		SDLCONFIG="$(STAGING_DIR)/usr/bin/sdl-config" \
		eduke32
endef

# Same layout as build_funkey.sh: the game data in opk/ plus the binary
define EDUKE32_INSTALL_TARGET_CMDS
	rm -rf $(@D)/opk-build
	cp -a $(EDUKE32_SRC_DIR)/opk $(@D)/opk-build
	$(INSTALL) -m 0755 $(EDUKE32_SRC_DIR)/eduke32 $(@D)/opk-build/eduke32
	$(TARGET_STRIP) $(@D)/opk-build/eduke32
	$(INSTALL) -d -m 0755 "$(EDUKE32_OPK_DIR)"
	$(HOST_DIR)/usr/bin/mksquashfs $(@D)/opk-build "$(EDUKE32_OPK_DIR)/eduke32_funkey-s.opk" \
		-all-root -noappend -no-exports -no-xattrs
	chmod 0644 "$(EDUKE32_OPK_DIR)/eduke32_funkey-s.opk"
endef

$(eval $(generic-package))
