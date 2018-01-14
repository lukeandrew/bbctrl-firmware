#!/bin/bash -ex

IMG_DATE=2017-11-29
IMG_BASE=${IMG_DATE}-raspbian-stretch-lite
BASE_URL=https://downloads.raspberrypi.org/raspbian_lite/images
IMG_URL=$BASE_URL/raspbian_lite-2017-12-01/$IMG_BASE.zip
GPLAN_IMG=gplan-dev.img

# Create dev image
if [ ! -e $GPLAN_IMG ]; then

    # Get base image
    if [ ! -e $IMG_BASE.img ]; then
        if [ ! -e $IMG_BASE.zip ]; then
            wget $IMG_URL
        fi

        unzip $IMG_BASE.zip
    fi

    # Copy base image
    cp $IMG_BASE.img $GPLAN_IMG.tmp

    # Init image
    mkdir -p rpi-share
    cp ./scripts/gplan-init-dev-img.sh rpi-share
    sudo ./scripts/rpi-chroot.sh $GPLAN_IMG.tmp /mnt/host/gplan-init-dev-img.sh

    # Move image
    mv $GPLAN_IMG.tmp $GPLAN_IMG
fi

# Get repos
mkdir -p rpi-share || true

if [ ! -e rpi-share/cbang ]; then
    if [ "$CBANG_HOME" != "" ]; then
        git clone $CBANG_HOME rpi-share/cbang
    else
        git clone https://github.com/CauldronDevelopmentLLC/cbang \
            rpi-share/cbang
    fi
fi

if [ ! -e rpi-share/camotics ]; then
    if [ "$CAMOTICS_HOME" != "" ]; then
        git clone $CAMOTICS_HOME rpi-share/camotics
    else
        git clone https://github.com/CauldronDevelopmentLLC/camotics \
            rpi-share/camotics
    fi
fi