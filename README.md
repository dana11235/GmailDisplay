# GMail Display
This project contains some code I wrote to show my inbox count on a little OLED display. The idea is that I won't need to have my inbox open all the time if I have this. It uses (right now) a Raspberry Pi Pico, an ESP8266-01 (as a Wifi adapter), and an SSH1106 SPI OLED display. It took me a lot of trial and error to get this working. Hopefully this will make it easier for someone else who wants to do something similar.

## Hardware Info
* The Raspberry Pi Pico is just the standard one. This might have been simpler if I used the one with built-in WiFi, but I already had the original lying around
* The ESP-01 is [this one](https://www.amazon.com/HiLetgo-Wireless-Transceiver-Development-Compatible/dp/B010N1ROQS/ref). It came with firmware 1.7.4, and I found that I needed to update it to firmware 2.2.2 with the instructions [here](https://www.sigmdel.ca/michel/ha/esp8266/ESP01_AT_Firmware_en.html) in order to get it to work with Google's APIs. I was able to do that with an Arduino Uno, although you can also just buy the programmer for a few bucks, and that might be easier.
* The OLED is [this one](https://www.amazon.com/HiLetgo-128x64-SSH1106-Display-Arduino/dp/B01N1LZT8L/ref). It's in SPI mode, and I would prefer to have it in I2C mode so that I can potentially just use the ESP01 for everything. But I'm not excited about trying to move the tiny SMD resistors.

## Setup Instructions
To get this to compile, you will need to add a file called constants.h that contains a few things.
* WIFI_USERNAME - the username for your wifi network
* WIFI_PASSWORD - the password for your wifi network
* OAUTH_REFRESH_BODY - in order to get this, you will need to go to the OAUTH PLayground (https://developers.google.com/oauthplayground). You will need to authorize the following APIs (Gmail API v1 - gmail.labels, gmail.readonly, Google OAuth2 API v2 - userinfo.profile). Once you authorize that, you will be given the body you will need for the refresh call (you can use the refresh call once to see it in action). If you want this refresh token to work in perpetuity, you will need to create your own application OAuth credentials using the configuration panel.