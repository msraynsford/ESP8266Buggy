esptool.exe -v -cd nodemcu -cb 921600 -cp COM5 -ca 0x000000 -cf d1_mini.bin
sleep 1
esptool.exe -v -cd nodemcu -cb 921600 -cp COM5 -ca 0x300000 -cf spiffs.bin
