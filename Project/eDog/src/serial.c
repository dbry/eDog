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

// serial.c
//
// David Bryant
// August 13, 2014

// This module supports initialization and use of the USART2 for debug logging output (or other output).
// The easiest way to access this is with a USB TTL serial adapter based on the Prolific PL2303HX (or
// at least that's all I've verified). Connect the grounds and connect the RXD pin to PA2 on the
// Discovery board. It's also possible to connect the TXD pin of the serial adapter to the PA3 pin of
// the Discovery board, but for now we do not use the serial data received (it is simply echoed).
//
// To actually have the transmission of serial data be useful, it must be buffered and transmitted by
// interrupt (otherwise the realtime response of the firmware is compromised). This is handled here
// with an 8K buffer. The software design should be such that we never fill this buffer (otherwise the
// caller will be blocked).

#include <stm32f4xx.h>
#include <misc.h>
#include <stm32f4xx_usart.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define TX_BUFLEN 8192
#define TX_BUFMASK (TX_BUFLEN-1)

static volatile uint8_t tx_buffer [TX_BUFLEN];
static volatile int tx_head, tx_tail;

/* This funcion initializes the USART2 peripheral
 *
 * Arguments: baudrate --> the baudrate at which the USART is
 * 						   supposed to operate
 */
static void init_USART2(uint32_t baudrate){

	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	/* enable APB1 peripheral clock for USART2
	 * note that only USART1 and USART6 are connected to APB2
	 * the other USARTs are connected to APB1
	 */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

	/* enable the peripheral clock for the pins used by
	 * USART2, PA2 for TX and PA3 for RX
	 */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

	/* GPIOA Configuration:  USART2_TX on PA2, USART2_RX on PA3
	 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP ;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* Connect USART2 pins to AF2 */
	// TX = PA2, RX = PA3
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

	USART_InitStructure.USART_BaudRate = baudrate;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_Init(USART2, &USART_InitStructure);

	/* Here the USART2 receive interrupt is enabled
	 * and the interrupt controller is configured
	 * to jump to the USART2_IRQHandler() function
	 * if the USART2 receive interrupt occurs
	 */
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); // enable the USART2 receive interrupt

	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;		 // we want to configure the USART2 interrupts
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;// this sets the priority group of the USART2 interrupts
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		 // this sets the subpriority inside the group
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			 // the USART2 interrupts are globally enabled
	NVIC_Init(&NVIC_InitStructure);							 // the properties are passed to the NVIC_Init function

	USART_Cmd(USART2, ENABLE); // enable USART2
}

// Write a single character to the USART2 transmit buffer. Note that we block here if the buffer is full
// (obviously that is not recommended).

static void USART2_putchar (char c)
{
    int buffer_was_empty;

    while (((tx_head + 1) & TX_BUFMASK) == tx_tail);    // block when buffer full (should not happen with a little care...)

    __disable_irq ();
    buffer_was_empty = (tx_head == tx_tail);
    tx_buffer [tx_head] = c;
    tx_head = (tx_head + 1) & TX_BUFMASK;
    __enable_irq ();

    if (buffer_was_empty)                               // if the buffer was empty when we disabled interrupts, then we
        USART_ITConfig(USART2, USART_IT_TXE, ENABLE);   // need to [re]enable the USART2 transmit interrupt because when
                                                        // the buffer goes empty the ISR disables it
}

// Debug puts. Expand "\n" to "\r\n"

void Dbg_puts (const char *s)
{
    while (*s) {
        if (*s == '\n')
            USART2_putchar ('\r');

        USART2_putchar (*s++);
    }
}

// Debug printf. Don't exceed 128 character width!

void Dbg_printf (const char *format, ...)
{
    char pstring [128];
    va_list args;

    va_start (args, format);
    vsprintf (pstring, format, args);
    Dbg_puts (pstring);
    va_end (args);
}

// Dump memory to the debug log (shows both ASCII and hex)

void Dbg_dumpmem (char *memory, int bcount)
{
    char hexstr [64], ascstr [24];
    int i, j;

    sprintf (hexstr, "--- %08x ---\n", (int) memory);
    Dbg_puts (hexstr);

    for (j = 0; j < bcount; j += 16) {
        ascstr [0] = 0;

        sprintf (hexstr, "%04x: ", j);

        for (i = 0; i < 16; ++i)
            if (i + j < bcount) {
                sprintf (hexstr + strlen (hexstr), "%02x ", (unsigned char) memory [i+j]);
                sprintf (ascstr + strlen (ascstr), "%c",
                    (memory [i+j] >= ' ' && memory [i+j] <= '~') ? memory [i+j] : '.');
            }
            else {
                strcat (hexstr, "   ");
                strcat (ascstr, " ");
            }

        Dbg_puts (hexstr);
        Dbg_puts (ascstr);
        Dbg_puts ("\n");
    }
}

// For debug logging output, initialize USART to 230400 baud

void Dbg_init (void)
{
    init_USART2 (230400); // initialize USART2 @ 230400 baud
}

// this is the interrupt request handler (IRQ) for all USART2 interrupts

void USART2_IRQHandler(void)
{
	// check if the USART2 transmit interrupt flag was set
	if( USART_GetITStatus(USART2, USART_IT_TXE) ){
        USART2->DR = tx_buffer [tx_tail];
        tx_tail = (tx_tail + 1) & TX_BUFMASK;
        if (tx_tail == tx_head)
            USART_ITConfig(USART2, USART_IT_TXE, DISABLE); // disable the USART2 transmit interrupt
    }

	// check if the USART2 receive interrupt flag was set and just echo character
	if( USART_GetITStatus(USART2, USART_IT_RXNE) )
        USART2_putchar (USART2->DR);
}
