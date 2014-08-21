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

// scan.h
//
// David Bryant
// August 13, 2014

// This module provides the functionality of scanning a 16 Khz mono audio stream and
// identifying possible instances of intentional "knocking" and doorbell "ringing".

#ifndef SCAN_H_
#define SCAN_H_

#include <inttypes.h>

#define SCAN_KNOCK_DETECTED     0x1
#define SCAN_BELL_DETECTED      0x2

#define SCAN_HIGH_SENSITIVITY   0x1     // select higher sensitivity mode

#define SCAN_DISP_THRESHOLDS    0x2     // display peak thresholds every 10 seconds
#define SCAN_DISP_EVENTS        0x4     // display detected events and special cases
#define SCAN_DISP_PEAKS         0x8     // display every processed peak

#define SCAN_OUTP_DECORR_AUDIO  0x10    // output decorrelated audio
#define SCAN_OUTP_DECORR_LEVEL  0x20    // output decorrelated audio level (decaying average)
#define SCAN_OUTP_NORMAL_AUDIO  0x40    // output normalized audio
#define SCAN_OUTP_WINDOW_LEVEL  0x80    // output windowed level
#define SCAN_OUTP_FILTER_AUDIO  0x100   // output biquad-filtered audio
#define SCAN_OUTP_FILTER_LEVEL  0x200   // output biquad-filtered audio level (decaying average)

void scan_audio_init (void);
int scan_audio (int16_t *in_samples, int num_samples, int16_t *out_samples, int flags);

#endif /* SCAN_H_ */
