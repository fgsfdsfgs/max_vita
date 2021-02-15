## Max Payne Mobile PS Vita port

This is a wrapper/port of the Android version of Max Payne Mobile. It loads the original game binary, patches it and runs it.
This is probably not entirely stable yet, so expect issues.

### How to install

Before installing the game, you should do the following on your Vita:
1. [Install kubridge](https://github.com/TheOfficialFloW/kubridge/releases/) same way you install other `.skprx` plugins: download `kubridge.suprx`, copy it to `ux0:/tai/` and add these lines to your `ux0:/tai/config.txt`:
```
*KERNEL
ux0:tai/kubridge.skprx
```
2. (optional) [Install fdfix](https://github.com/TheOfficialFloW/FdFix) if you don't want your game to crash on suspend.
2. [Extract the runtime shader compiler](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).
3. Reboot.

You're going to need:
* `.apk` file for version 1.7 (latest version at the time of writing);
* `.obb` file for version 1.6 or 1.7 (usually located at `/sdcard/android/obb/com.rockstar.maxpayne/main.3.com.rockstar.maxpayne.obb`).

Both files [can be obtained](https://stackoverflow.com/questions/11012976/how-do-i-get-the-apk-of-an-installed-app-without-root-access) from your phone if you have a copy of the game installed.
Both files can be opened or extracted with anything that can extract `.zip` files.

To install:
1. Install the latest VPK from the Releases page.
2. Extract the `assets` folder from your `.apk` to `ux0:/data` and rename it to `maxpayne`.
3. Extract `lib/armeabi-v7a/libMaxPayne.so` from your `.apk` to `ux0:/data/maxpayne`.
4. Extract the contents of the `.obb` file into `ux0:/data/maxpayne`. You can skip all the `.msf` files except for `MaxPayneSoundsv2.msf` and any extra languages that you want.
5. Extract the contents of the `data.zip` from the latest release into `ux0:/data`. Replace everything.

### Notes

You can put the game data onto `uma0:` or `imc0:` instead of `ux0:`. Paths after `:` remain the same. This document uses `ux0` in most paths as an example.

The Vita port has an extra config file, located at `ux0:/data/maxpayne/config.txt`. It is created when you first run the game and allows you to tweak some internal settings. For more detailed descriptions of said settings check the [wiki article](https://github.com/fgsfdsfgs/max_vita/wiki/Config-variables).

To enable bullet casings and ejected magazines (and possibly other debris missing from the mobile version), open up `ux0:/data/maxpayne/data/globalconfig.txt` and change

The game will show an error message if it detects that anything is wrong. Please read those and check the relevant parts of your setup.

If the error says `Could not find symbol ...`, that likely means you have the wrong `.apk`. You need one for v1.7, others **will not work**.

If the game crashes on startup, please post an issue with your last crash dump attached.

### How to build the `master` branch

You're going to need these things recompiled with the `-mfloat-abi=softfp` compiler flag:
* The entirety of [VitaSDK](https://github.com/vitasdk) ([Linux build](https://github.com/vitasdk/buildscripts/suites/1824103476/artifacts/35161735), [Windows build](https://github.com/vitasdk/buildscripts/suites/1836262288/artifacts/35501612))
* [openal-soft](https://github.com/isage/openal-soft/tree/vita-1.19.1)
* [libmathneon](https://github.com/Rinnegatamante/math-neon)
* [vitashark](https://github.com/Rinnegatamante/vitaShaRK)
* [vitaGL](https://github.com/Rinnegatamante/vitaGL) (build with `HAVE_SBRK=1 SOFTFP_ABI=1 NO_DEBUG=1`)

You're also going to need the static library and header for [kubridge](https://github.com/TheOfficialFloW/kubridge):
```
git clone https://github.com/TheOfficialFloW/kubridge.git && cd kubridge
mkdir build && cd build
cmake -G"Unix Makefiles" ..
make && make install
```

After you've obtained all the dependencies and ensured VitaSDK is properly installed and in `PATH`, build this repository using the same commands:
```
git clone https://github.com/fgsfdsfgs/max_vita.git && cd max_vita
mkdir build && cd build
cmake -G"Unix Makefiles" ..
make
```

### Credits

* TheOfficialFloW for kubridge, figuring out how to do this shit, and much of the code;
* Rinnegatamante for vitaGL and help with graphics-related stuff;
* Bythos and frangarcj for help with graphics-related stuff;
* CBPS/SonicMastr for PIB, which was used on earlier stages of development;
* isage for the native audio backend for OpenAL-Soft;
* Brandonheat8 for providing the old LiveArea assets;
* Freakler for providing the new LiveArea assets.
