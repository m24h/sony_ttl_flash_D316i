# sony_ttl_flash_D316i

A Sony P-TTL flash built on a Minolta D316i body.

## About Minolta D316i

It is a compact flash, with a main capacitor of 280uF, and its Guide-Number is approximately 16. It is specifically designed for the Dynax/Maxxum 3000i, requiring no battery and using the power provided by the camera. It has a very rare and interesting design. After turning on the power, the flash pops out, which is said to be inspired by Ferrari.

## What has been modified

The shell and mechanical parts were retained, the circuit, circuit board, and program were newly designed, a lithium battery was added, 3D printed transfer adapter parts were made, and changed to Sony Multi-Interface.

## Sony P-TTL and some details

Based on the discussion at [Discovering the flash protocol (long)](https://www.dyxum.com/dforum/topic8297.html), with some of my own research added, the camera will interact with the flash, reading the maximum flash power of the flash, indicating to the flash to fire at an appropriate energy. For specifics, see flash_sony.h.
FEL is not supported because I don't think it's needed, but if support is desired, flash.c needs to be modified, changing FLASH_SONY_READ_FEATURE_DEF to FLASH_SONY_READ_FEATURE_DEF | FLASH_SONY_READ_FEATURE_NEW, and it will increase the workload of communication (which can still be easily handled).
During macro photography, the camera's pre-flash seems too strong, which can lead to an underestimation of the flash level. Therefore, some corrections were made algorithmically, see FLASH_LOW_FLASH_ENHANCE in flash.h/flash.c.
The compiler is SDCC v4.5, and the compiled binary file is main.bin, designed based on STC8H1K08 (base on 8051, but MCU needs to support rising edge triggered interrupts), and the burning details refer to readme.txt and STC8H1K08.cfg.
The flash time of the level (flash_time_of_level in main.c) is specially calibrated for my actual xenon tube, and may require recalibration on other tubes.
It's tested only on my A7CR camera.

## Pulseview

A sony flash protocol plug-in for pulseview is provided in directory pulseview. 

## Note

Frequent use of maximum power flash may damage IGBTs that do not have perfect cooling design.


