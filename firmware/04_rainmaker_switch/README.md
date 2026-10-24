# 04 RainMaker switch (ESP32-CAM)

Espressif's [ESP RainMaker switch example](https://github.com/espressif/esp-rainmaker/tree/master/examples/switch),
adapted for the AI-Thinker ESP32-CAM: the switch drives the red LED on **GPIO33** (active-low), and the
button is **IO0** (touch it to GND).

Build (needs a checkout of esp-rainmaker):

```
git clone --recursive https://github.com/espressif/esp-rainmaker.git ~/esp/esp-rainmaker
export RMAKER_PATH=~/esp/esp-rainmaker
idf.py set-target esp32 build
```

Add it in the ESP RainMaker phone app with the QR code the board prints on its serial console.
