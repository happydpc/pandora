# Copy OpenEXR file fixes (from their master branch) because the current release is not C++17 compatible
sudo cp -f ./travis/openexr_patches/ImathMatrix.h /usr/include/OpenEXR/
sudo cp -f ./travis/openexr_patches/ImathVec.h /usr/include/OpenEXR/

sudo mkdir build
cd build
sudo cmake -GNinja -DBUILD_TESTS=ON -DBUILD_ATLAS=OFF -DCMAKE_CXX_FLAGS="-W -Wall -Wpedantic -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-unused-private-field -mavx2" ../
sudo ninja -j1
ctest --output-on-failure
cd ../
sudo rm -rf build