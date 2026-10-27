include $(sort $(wildcard $(BR2_EXTERNAL_RECOVERY_PATH)/package/*/*.mk))

# The Mollusk OS Recovery boot logo (branding/boot/) in place of the FunKey one in the
# kernel sources
define RECOVERY_MOLLUSK_BOOT_LOGO
	cp $(BR2_EXTERNAL_RECOVERY_PATH)/../branding/boot/logo_funkey_recovery_clut224.ppm \
		$(@D)/drivers/video/logo/logo_funkey_recovery_clut224.ppm
endef
LINUX_POST_PATCH_HOOKS += RECOVERY_MOLLUSK_BOOT_LOGO
