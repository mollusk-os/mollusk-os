#!/bin/sh
# Make the SD card image's root filesystem: a copy of the firmware's, grown
# to SIZE, with the given packs stored in /usr/local/share/packs. First boot
# installs them onto the USB drive (see first_boot), so a fresh SD card comes
# with them, while firmware updates (made from the plain root filesystem)
# don't carry them.
#
# Usage: make-sdcard-rootfs.sh HOST_DIR BASE_IMAGE OUTPUT_IMAGE SIZE PACK...
#   HOST_DIR is Buildroot's host directory (for e2fsck, resize2fs, debugfs)

set -e

if [ $# -lt 4 ]; then
    echo "Usage: $0 HOST_DIR BASE_IMAGE OUTPUT_IMAGE SIZE PACK..." >&2
    exit 1
fi
host=$1
base=$2
out=$3
size=$4
shift 4

E2FSCK=${host}/sbin/e2fsck
RESIZE2FS=${host}/sbin/resize2fs
DEBUGFS=${host}/sbin/debugfs
DIR=/usr/local/share/packs

work=$(mktemp -d)
trap 'rm -rf "${work}"' EXIT

cp --sparse=always "${base}" "${out}.tmp"

# resize2fs wants a freshly checked filesystem (exit code 1 means fixed)
"${E2FSCK}" -fy "${out}.tmp" >/dev/null || [ $? -eq 1 ]
"${RESIZE2FS}" "${out}.tmp" "${size}" >/dev/null 2>&1

# debugfs's "write" takes a plain name in its current directory, hence the cd
{
    echo "mkdir ${DIR}"
    echo "sif ${DIR} mode 040755"
    echo "sif ${DIR} uid 0"
    echo "sif ${DIR} gid 0"
    echo "cd ${DIR}"
    for pack in "$@"; do
	name=$(basename "${pack}")
	echo "write $(realpath "${pack}") ${name}"
	echo "sif ${name} mode 0100644"
	echo "sif ${name} uid 0"
	echo "sif ${name} gid 0"
    done
} > "${work}/commands"
"${DEBUGFS}" -w -f "${work}/commands" "${out}.tmp" > "${work}/log" 2>&1

# debugfs doesn't fail on errors (e.g. a full filesystem): check every pack
for pack in "$@"; do
    name=$(basename "${pack}")
    rm -f "${work}/check"
    "${DEBUGFS}" -R "dump ${DIR}/${name} ${work}/check" "${out}.tmp" >/dev/null 2>&1
    if ! cmp -s "${pack}" "${work}/check"; then
	cat "${work}/log" >&2
	echo "$0: ${name} did not make it into ${out} whole" >&2
	exit 1
    fi
done

"${E2FSCK}" -fn "${out}.tmp" >/dev/null
mv "${out}.tmp" "${out}"
echo "${out}: $(basename "${base}") grown to ${size}, with $(for p in "$@"; do basename "$p"; done | tr '\n' ' ')in ${DIR}"
