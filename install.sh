#!/bin/bash
set -e  # остановка при любой ошибке

sudo apt-get update
sudo apt-get install -y \
  python3-pip python3-setuptools python3-numpy swig python3-matplotlib \
  cmake g++ libpython3-dev libxml2 libxml2-dev bison flex libcdk5-dev \
  libusb-1.0-0-dev libaio-dev pkg-config libavahi-common-dev libavahi-client-dev

cd pluto

build_repo() {
  local repo_url="$1"
  local branch="$2"
  local name=$(basename "$repo_url" .git)

  if [ ! -d "$name" ]; then
    git clone --branch "$branch" "$repo_url"
  else
    echo "Updating existing repo: $name"
    git -C "$name" fetch origin "$branch"
    git -C "$name" checkout "$branch"
    git -C "$name" pull
  fi

  pushd "$name" > /dev/null
  mkdir -p build && cd build
  cmake ..
  make -j"$(nproc)"
  sudo make install
  sudo ldconfig
  popd > /dev/null
}

build_repo https://github.com/TelecomDep/SoapySDR.git soapy-sdr-0.8.1
build_repo https://github.com/TelecomDep/libiio.git v0.24
build_repo https://github.com/TelecomDep/libad9361-iio.git v0.3
build_repo https://github.com/TelecomDep/SoapyPlutoSDR.git sdr_gadget_timestamping
