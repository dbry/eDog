////////////////////////////////////////////////////////////////////////////
//                             **** eDog ****                             //
//                                                                        //
//                  Electronic Dog Home Security System                   //
//                                 on the                                 //
//                           STM32F4-Discovery                            //
//                                                                        //
//                    Copyright (c) 2014 David Bryant                     //
//                          All Rights Reserved                           //
//        Distributed under the GNU Software License (see COPYING)        //
////////////////////////////////////////////////////////////////////////////

To flash a STM32F4-Discovery board with the eDog application without
installing a toolchain, use the two binary files in this folder and flash
them using STM32 ST-LINK Utility available from ST.

      eDog.hex -- application code -- flash to 0x08000000
dog-30secs.bin -- canned pcm audio -- flash to 0x08010000

Note that without building the code yourself, you will not be able to
customize the ring detector for a doorbell that is not exactly 770 Hz
(the knock detector does not require customization).

Even if you are building the code yourself, you will still have to manually
flash the canned audio because this is not included in the source code or
the hex image.
