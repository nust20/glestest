# glestest
# gles offsceen render and computer shader test code 
make glestest
adb push glestest /data/local/tmp
adb shell
cd /data/local/tmp
chmod a+x gltest (optional)
./gltest ...
