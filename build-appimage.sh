mkdir appimage-build
cd appimage-build

cmake .. -DCMAKE_INSTALL_PREFIX=/usr

make -j$(nproc)

make install DESTDIR=AppDir

cp ./assets/logo.png ./AppDir/usr/share/icons/hicolor/128x128/apps/aviateur.png
cp -r ./appimage-build/assets/ ./appimage-build/AppDir/

wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage

# Make it executable
chmod +x linuxdeploy*.AppImage

./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage -i ../assets/logo.png -d ../aviateur.desktop
