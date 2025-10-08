sudo apt-get install python3-pip python3-setuptools python3-numpy swig python3-matplotlib cmake g++ libpython3-dev  libxml2 libxml2-dev bison flex libcdk5-dev libusb-1.0-0-dev libaio-dev pkg-config -y
sudo apt install libavahi-common-dev libavahi-client-dev -y

cd pluto

# SoapySDR
git clone --branch soapy-sdr-0.8.1 https://github.com/TelecomDep/SoapySDR.git
cd SoapySDR
mkdir build && cd build
cmake ../
make -j`nproc`  # nproc - количество потоков, например make -j16
sudo make install
sudo ldconfig

cd ../

# Libiio
git clone --branch v0.24 https://github.com/TelecomDep/libiio.git
cd libiio
mkdir build && cd build
cmake ../
make -j`nproc`
sudo make install

cd ../

# LibAD9361
git clone --branch v0.3 https://github.com/TelecomDep/libad9361-iio.git
cd libad9361-iio
mkdir build && cd build
cmake ../
make -j`nproc`
sudo make install
sudo ldconfig

cd ../

# SoapyPlutoSDR
git clone --branch sdr_gadget_timestamping https://github.com/TelecomDep/SoapyPlutoSDR.git
cd SoapyPlutoSDR
mkdir build && cd build
cmake ../
make -j`nproc`
sudo make install
sudo ldconfig

cd ../
