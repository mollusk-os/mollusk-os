#!/bin/bash
# Build Mollusk OS inside the Docker build environment
#
# Usage: docker/build.sh [make targets...]   (default: all)
#
# The source tree is bind-mounted into the container, so downloads, build
# outputs and images/ all land in this checkout. The Buildroot ccache lives
# in the "mollusk-os-ccache" Docker volume so it survives between runs.

set -e

cd "$(dirname "$0")/.."

IMAGE=mollusk-os-build
BUILD_UID=1000
BUILD_GID=1000

docker build -t "${IMAGE}" \
	--build-arg UID=${BUILD_UID} --build-arg GID=${BUILD_GID} docker

# Buildroot must not run as root: hand the tree to the container's build user
if [ "$(stat -c %u .)" != "${BUILD_UID}" ]; then
	echo "Changing owner of $(pwd) to ${BUILD_UID}:${BUILD_GID} for the build user"
	chown -R ${BUILD_UID}:${BUILD_GID} .
fi

# Only ask for a TTY when there is one (e.g. not when run from a CI job or in the background)
TTY=
[ -t 1 ] && TTY=-t

exec docker run --rm ${TTY} \
	-v "$(pwd)":/home/builder/mollusk-os \
	-v mollusk-os-ccache:/home/builder/.buildroot-ccache \
	"${IMAGE}" make "${@:-all}"
