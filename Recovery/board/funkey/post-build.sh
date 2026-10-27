#!/bin/sh

# Add local path to init scripts
sed -i '3iexport PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/local/sbin:/usr/local/bin' ${TARGET_DIR}/etc/init.d/rcK
sed -i '3iexport PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/local/sbin:/usr/local/bin' ${TARGET_DIR}/etc/init.d/rcS

# Remove log daemon init scripts since they are loaded from inittab
rm -f ${TARGET_DIR}/etc/init.d/S01syslogd ${TARGET_DIR}/etc/init.d/S02klogd

# Remove dhcp lib dir and link to /tmp
rm -rf ${TARGET_DIR}/var/lib/dhcp/
ln -s /tmp ${TARGET_DIR}/var/lib/dhcp

# Remove dhcpcd dir and link to /tmp
rm -rf ${TARGET_DIR}/var/db/dhcpcd/
ln -s /tmp ${TARGET_DIR}/var/db/dhcpcd

# Change dropbear init sequence
mv ${TARGET_DIR}/etc/init.d/S50dropbear ${TARGET_DIR}/etc/init.d/S42dropbear

# Redirect drobear keys to /tmp
rm -rf ${TARGET_DIR}/etc/dropbear
ln -s /tmp ${TARGET_DIR}/etc/dropbear

# System name and version (/etc/os-release, /etc/issue, /etc/sw-versions): fill
# in the version from the VERSION file of the repository. Buildroot writes its
# own /usr/lib/os-release ("Buildroot 2021.02"); replace it with ours.
VERSION=$(cat "$(dirname "$0")/../../../VERSION")
[ -n "${VERSION}" ] || { echo "post-build.sh: no version in the VERSION file" >&2; exit 1; }
sed -i "s/@VERSION@/${VERSION}/g" "${TARGET_DIR}/etc/os-release" "${TARGET_DIR}/etc/issue" "${TARGET_DIR}/etc/sw-versions"
cp "${TARGET_DIR}/etc/os-release" "${TARGET_DIR}/usr/lib/os-release"

# Generate U-Boot environment for app
${HOST_DIR}/bin/mkenvimage -p 0x0 -s 0x20000 -o ${BINARIES_DIR}/u-boot-env.img ${TARGET_DIR}/etc/u-boot.env

# Generate U-Boot environment for production
cp ${TARGET_DIR}/etc/u-boot.env ${TARGET_DIR}/etc/u-boot-prod.env
echo "assembly_tests=1" >> ${TARGET_DIR}/etc/u-boot-prod.env
${HOST_DIR}/bin/mkenvimage -p 0x0 -s 0x20000 -o ${BINARIES_DIR}/u-boot-env-prod.img ${TARGET_DIR}/etc/u-boot-prod.env
rm ${TARGET_DIR}/etc/u-boot-prod.env
