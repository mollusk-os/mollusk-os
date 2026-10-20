FROM ubuntu:22.04

LABEL \
	description="Container with everything needed to build Mollusk OS (based on FunKey-OS)"

# Setup environment
ENV DEBIAN_FRONTEND noninteractive

# UID/GID of the build user, so files written to the bind-mounted tree keep
# a predictable owner (Buildroot must not be run as root)
ARG UID=1000
ARG GID=1000

RUN \
	# Install dependencies
	# See https://buildroot.org/downloads/manual/manual.html#requirement
	apt-get update && \
	apt-get install -y -q --no-install-recommends \
	# MANDATORY build tools
	make \
	binutils \
	build-essential \
	gcc \
	g++ \
	patch \
	bzip2 \
	perl \
	cpio \
	unzip \
	rsync \
	file \
	bc \
	# MANDATORY source fetching tools
	wget \
	# OPTIONAL recommended dependencies
	python2 \
	python2-dev \
	xxd \
	# OPTIONAL configuration interface dependencies
	libncurses5-dev \
	# OPTIONAL source fetching tools
	cvs \
	git \
	mercurial \
	subversion \
	python3 \
	python3-dev \
	python3-distutils \
	python3-setuptools \
	# ADDITIONAL dependency to get root certificates
	ca-certificates \
	# ADDITIONAL dependency to get client ssh
	openssh-client \
	# ADDITIONAL dependency to get unbuffer (used by buildroot/utils/brmake)
	expect \
	# ADDITIONAL dependency to get locale-gen
	locales \
	# ADDITIONAL nice to have dependencies
	procps \
	&& \
	apt-get -y autoremove && \
	apt-get -y clean && \
	rm -rf /var/lib/apt/lists/* && \
	#
	# The original buster image provided "python" as Python 2
	ln -sf /usr/bin/python2 /usr/bin/python && \
	#
	# Set locale
	sed -i 's/# \(en_US.UTF-8\)/\1/' /etc/locale.gen && \
	locale-gen --purge --lang en_US.UTF-8 && \
	#
	# Add user
	groupadd -g ${GID} builder && \
	useradd -ms /bin/bash -u ${UID} -g ${GID} builder && \
	mkdir -p /home/builder/.buildroot-ccache && \
	chown -R builder:builder /home/builder && \
	# The tree is bind-mounted and may be owned by another host UID
	git config --system --add safe.directory '*'

# Set user
USER builder

# Source tree is bind-mounted here by docker/build.sh
WORKDIR /home/builder/mollusk-os

# Set environment
ENV \
	HOME=/home/builder \
	LC_ALL=en_US.UTF-8

CMD ["/usr/bin/make", "all"]
