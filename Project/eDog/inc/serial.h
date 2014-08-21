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

// serial.h
//
// David Bryant
// August 13, 2014

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SERIAL_H
#define __SERIAL_H

/* Exported functions ------------------------------------------------------- */

void Dbg_puts (const char *s);
void Dbg_printf (const char *format, ...);
void Dbg_dumpmem (char *memory, int bcount);
void Dbg_init (void);

#endif /* __SERIAL_H */
