This project is an android Skylander's Portal emulator, inspired by NicoAICP's project for a Rasberry Pi Pico (https://github.com/NicoAICP/KAOS).

In order to use this app, the phone it is running on must have root access, as well as OTG functionality. If this core functionalty and permission level is not present, the app WILL NOT WORK. Additionally, the USB cable you use to connect your phone to your console MUST support data transfer.

In order to present itself as a Skylander's portal, the app focibly unbinds the default USB gadget on the phone it's running on, and rebinds to a new USB gadget it creates, emulating the functionality and characteristics of a SKylander's portal. While the gagdet is running, and before the app cleans up after itself, your phone WILL NO LONGER CONNECT over USB as a phone. In order to restore basic USB functionality, you need to either run the cleanup from the app, or simply restart your phone.

***Current Issues: Phone Appears as HID device before eventually failing device descriptor request***
