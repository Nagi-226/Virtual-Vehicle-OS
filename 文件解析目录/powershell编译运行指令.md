如果没做过一次 configure（生成过 build-linux），先执行：

wsl bash -lc "cd /mnt/e/Virtual-Vehicle-OS \&\& cmake -S . -B build-linux -DCMAKE\_BUILD\_TYPE=Debug"

***如果已经先配置过一次：***

**wsl bash -lc "cd /mnt/e/Virtual-Vehicle-OS \&\& cmake --build build-linux -j"**

如果已经进入 WSL shell（提示符变成 Linux 风格），则用：

cd /mnt/e/Virtual-Vehicle-OS

cmake -S . -B build-linux -DCMAKE\_BUILD\_TYPE=Debug

cmake --build build-linux -j

