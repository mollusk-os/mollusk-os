<!-- ![FunKey OS Build](https://github.com/FunKey-Project/FunKey-OS/workflows/FunKey-OS%20Build/badge.svg) -->
# FunKey OS

> **Mollusk OS** is a fork of [DrUm78's FunKey-OS](https://github.com/DrUm78/FunKey-OS), which was archived in April 2026. All the sources the build fetches from the FunKey Project and DrUm78 are mirrored in the [mollusk-os](https://github.com/mollusk-os) organization and pinned to fixed commits.

## Intro
This repository contains all the sources required to build FunKey OS, the Open-Source firmware at the heart of the [FunKey S retro-gaming console](https://www.funkey-project.com/).

As the FunKey-S console is based on a sophisticated [Allwinner V3s ARM Cortex-A7 1.2GHz CPU](http://www.allwinnertech.com/index.php?c=product&a=index&id=38), an Operating System is mandatory in order to access all the hardware resources without re-inventing the wheel.

FunKey OS is based on Linux, and is built from scratch using the [buildroot](http://nightly.buildroot.org/) tool that simplifies and automates the process of building a complete Linux system for an embedded system like this.

Technically, Funkey OS is a [buildroot (v2) based external tree](https://buildroot.org/downloads/manual/manual.html#outside-br-custom) for building the bootloader, the Linux kernel and user utilities, as well as the optimized retro-game launcher and console emulators.

## Build host requirements
Even if the resulting disk image and firmware update files are relatively small (202 MB and 55MB, respectively), the size of the corresponding sources and the compilation by-products tend to be rather large, such that an available disk space of at least 12GB is required during the build.

And even if the resulting FunKey OS boots in less than 5s, it still requires a considerable amount of time to compile: please account for 1 1/2 hour on a modern multi-core CPU with SSD drives and a decent Internet bandwidth.

As the target CPU is probably different from the one running on your build host machine, a process known as [_cross-compilation_](https://en.wikipedia.org/wiki/Cross_compiler) is required for the build, and as the target system will eventually be Linux, this is much better handled on hosts running a Linux-based operating system too.

As a matter of fact, the FunKey OS is meant to be built on a native Ubuntu or Debian Linux host machine (Ubuntu 20.04 LTS in our case, but this should also work with other versions, too). And with only a few changes to the prerequisites, it can certainly be adapted to build on other common Linux distros.

However, if your development machine does not match this setup, there are still several available solutions:
 -  use a lightweight container system such as [Docker](https://www.docker.com/) and run an Ubuntu or Debian Linux container in it
 - use a VM (Virtual Machine) , such as provided by [VirtualBox](https://www.virtualbox.org/) and run an Ubuntu or Debian Linux in it
 - for Windows 10/11 users, use the [WSL2](https://learn.microsoft.com/en-us/windows/wsl/install) (Windows System for Linux 2) subsystem and run an Ubuntu Linux distro in it

In order to install one of these virtualized environments on your machine, please refer to the corresponding documentation.

## Build on a Physical/Virtual Machine

### Prerequisites
While Buildroot itself will build most host packages it needs for the compilation, some standard Linux utilities are expected to be already installed on the host system. If not already present, you will need to install the following packages beforehand:
 - bash
 - bc
 - binutils
 - build-essential
 - bzip2
 - ca-certificates
 - cpio
 - cvs
 - expect
 - file
 - g++
 - gcc
 - git
 - gzip
 - liblscp-dev
 - libncurses5-dev
 - locales
 - make
 - mercurial
 - openssh-client
 - patch
 - perl
 - procps
 - python
 - python-dev
 - python3
 - python3-dev
 - python3-distutils
 - python3-setuptools
 - rsync
 - rsync
 - sed
 - subversion
 - sudo
 - tar
 - unzip
 - wget
 - which
 - xxd

On Ubuntu/Debian Linux, this is achieved by running the following command:
```bash
$ sudo apt install bash bc binutils build-essential bzip2 ca-certificates cpio cvs expect file g++ gcc git gzip liblscp-dev libncurses5-dev locales make mercurial openssh-client patch perl procps python python-dev python3 python3-dev python3-distutils python3-setuptools rsync rsync sed subversion sudo tar unzip wget which xxd
```

### How to get the sources
When using either physical or virtual Linux machines, you must clone the FunKey OS repository from Github (here we place it into a `FunKey-OS` directory):

```bash
$ git clone https://github.com/mollusk-os/mollusk-os.git mollusk-os
```

Then enter into the created directory:

```bash
$ cd mollusk-os
```

### Build the disk image & firmware update files
You may now build your FunKey with:

```bash
$ make sdk all
```
This may take a while (~1h30), so consider getting yourself a cup, a glass or a bottle of your favorite beverage ;-)

<ins>Note</ins>: you will need to have access to the network, since buildroot will download the package sources.

### Result of the build
After building, you should obtain the SD Card image `FunKey-MolluskOS-X.Y.Z-sdcard.img`, the firmware update file `FunKey-MolluskOS-X.Y.Z.fwu` and the add-on packs `MolluskOS-Games-X.Y.zip` and `MolluskOS-Themes-X.Y.zip` in the `images` directory. The version, X.Y.Z, is `MOLLUSK_VERSION` in the `Makefile`.

## Build in a container

### Prerequisites
When using a Docker container, all the prerequisites are automatically installed.

### How to get the sources
Clone the repository as described above. On Windows, clone it inside WSL2's Linux filesystem (e.g. `~/src/mollusk-os`), not under `/mnt/c`: Buildroot needs a case-sensitive filesystem with symlinks, and builds on `/mnt/c` are very slow.

### Build the disk image & firmware update files
The `docker/build.sh` script builds an Ubuntu 22.04 image with all the prerequisites, then runs `make` in a container with the checkout bind-mounted into it:
```bash
$ docker/build.sh
```

Any `make` target can be passed instead of the default `all`, for example `docker/build.sh sdk all`. The Buildroot ccache is kept in the `mollusk-os-ccache` Docker volume, so rebuilds are faster.

<ins>Note</ins>: Buildroot must not run as root, so the script builds as UID 1000 and changes the owner of the checkout to it if needed.

This may take a while (~1h30), so consider getting yourself a cup, a glass or a bottle of your favorite beverage ;-)

<ins>Note</ins>: you will need to have access to the network, since buildroot will download the package sources.

### Result of the build
As the checkout is shared with the container, the SD Card image, the firmware update file and the add-on packs end up directly in the `images` directory.

## How to write to the SD card
You can copy the bootable `images/FunKey-MolluskOS-X.Y.Z-sdcard.img` onto an SD card using "dd":

```bash
$ sudo dd if=images/FunKey-MolluskOS-X.Y.Z-sdcard.img of=/dev/sdX
```
<ins>Warning</ins>: Please make sure that */dev/sdX* device corresponds to your SD Card, otherwise you may wipe out one of your hard drive partitions!

Alternatively, you can use the Balena-Etcher graphical tool to burn the image
to the SD card safely and on any platform:

https://www.balena.io/etcher/

Once the SD card is burnt, insert it into your FunKey S console slot, and
power it up. Your new system should come up now and start a console on
the UART0 serial port and display the retro game launcher on the graphical screen.

The first boot sets up the card and installs the games that come with the
image (the games pack, see below).

## How to update the FunKey S firmware
It is possible to update a FunKey-S over USB:
 - Connect the FunKey S console to your host machine using the USB cable
 - From the retro-game launcher, press the **ON/OFF** button to access the menu
 - Using the **Up/Down** keys, select the "**MOUNT USB**" screen ad press the "**A**" key twice to mount the FunKey S on your machine as an USB mass storage drive
 - Drag and drop the images/FunKey-MolluskOS-X.Y.Z.fwu file into it (keep its name: the FunKey only picks up update files named `FunKey-*.fwu`)
 - When finished, eject the USB mass storage from your host machine
 - Back on the FunKey S console, press the "**A**" key twice to eject the USB mass storage drive
 - The FunKey S console will automatically detect the firmware update file and proceed with the update before returning to the retro game launcher screen once finished

Firmware updates don't carry the games anymore: the games on the USB drive are left as they are (deleted ones stay deleted), and the launcher theme you picked is kept.

## Add-on packs
Games and extra launcher themes come in add-on packs: `MolluskOS-Games-X.Y.zip` (the freeware and homebrew games, the FPS demos and EDuke32) and `MolluskOS-Themes-X.Y.zip` (animated launcher themes). To install one, copy the zip file as it is to the root of the FunKey's USB drive (with **MOUNT USB**, as above), then eject: the FunKey installs it and restarts the launcher. `MolluskOS-Remove-Games-X.Y.zip`, copied the same way, removes the games pack's games again but keeps your saves, settings and own games. See [docs/packs.md](docs/packs.md), which also explains how to make your own packs.

## Credits
Mollusk OS builds on the work of:
 - **The FunKey Project**, Michel Stempin and Vincent Buso: the [FunKey S console](https://www.funkey-project.com/) and the original [FunKey OS](https://github.com/FunKey-Project/FunKey-OS).
 - **DrUm78**: [DrUm78's FunKey-OS](https://github.com/DrUm78/FunKey-OS), the community FunKey OS that Mollusk OS is a fork of.
 - The open-source projects it is built from (Buildroot, Linux, U-Boot, RetroFE, GMenu2X, picoarch, the libretro cores, SDL and many more), each under its own license.

Their commits are kept in this repository's history. Mollusk OS is an independent fork, not an official FunKey Project release, so its boot screen shows the Mollusk OS logo (see [branding/](branding/README.md)) rather than the FunKey S and DrUm78 logos; the FunKey name and logo belong to the FunKey Project.
