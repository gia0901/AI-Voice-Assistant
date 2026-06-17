#!/bin/bash
vi BBB-VOICE-ASSISTANT.dts
dtc -O dtb -o BBB-VOICE-ASSISTANT.dtbo -b 0 -@ BBB-VOICE-ASSISTANT.dts
sudo cp BBB-VOICE-ASSISTANT.dtbo /lib/firmware/