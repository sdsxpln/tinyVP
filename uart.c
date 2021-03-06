/*
 * Copyright (c) 2016 Leonid Yegoshin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 *      PIC32MZEF UART access functions
 *
 *      It can work in sync, with wait-until-finished or
 *      work via RX/TX interrupts in background.
 */

#include    "mips.h"
#include    "irq.h"
#include    "uart.h"
#include    "log.h"
#include    "console.h"

int uart_writechar(volatile struct uart_device *uad, unsigned char c)
{
	while (uad->UxSTA & UART_STA_UTXBF) ;
	uad->UxTXREG = c;

	return 1;
}

int uart_writeline(volatile struct uart_device *uad, unsigned char *l)
{
	unsigned int ie;

	im_up(ie);
	while (*l) {
		uart_writechar(uad,*l);
		if (*(l++) == '\n')
			uart_writechar(uad,'\r');
	}
	im_down(ie);

	return(1);
}

int uart_write(volatile struct uart_device *uad, unsigned char *l, unsigned len)
{
	unsigned int ie;

	im_up(ie);
	while (len-- > 0)
		uart_writechar(uad,*(l++));
	im_down(ie);

	return(1);
}

unsigned int uart_readchar(volatile struct uart_device *uad)
{
	while ( !(uad->UxSTA & UART_STA_URXDA) ) ;

	return(uad->UxRXREG);
}

static int uart_sendchar(volatile struct uart_device *uad, unsigned char c)
{
	if (uad->UxSTA & UART_STA_UTXBF)
		return 0;
	uad->UxTXREG = c;

	return 1;
}

static int uart_sendlog(volatile struct uart_device *uad)
{
	unsigned char *pc;
	unsigned char c;

	if (!(pc = log_char()))
		return 0;

	do {
		if (!uart_sendchar(uad, (c = *pc)))
			break;
		if (c == '\n')
			uart_writechar(uad, '\r');
		if (!(pc = log_nextchar()))
			return 0;
	} while (1);

	return 1;
}

void uart_start_console(void)
{
	unsigned int ie;

	im_up(ie);
	if (uart_sendlog(console_uart))
		irq_unmask(console_irq_tx);
	im_down(ie);
}

int uart_tx_irq(unsigned int irq)
{
	if (irq != console_irq_tx)
		return(0);

	irq_mask_and_ack(irq);

	if (uart_sendlog(console_uart))
		irq_unmask(console_irq_tx);

	return 1;
}

int uart_rx_irq(struct exception_frame *exfr, unsigned int irq)
{
	unsigned int value;
	volatile struct uart_device *uad;

	if (irq != console_irq_rx)
		return(0);

	irq_ack(console_irq_rx);
	uad = console_uart;

	while (uad->UxSTA & UART_STA_URXDA) {
		value = uad->UxRXREG;
		console_rx(exfr, value);
	}

	return(1);
}

int uart_init(volatile struct uart_device *uad)
{
	uad->UxBRG  =   UART_BRG_CLOCK_RATIO_115209;
	uad->UxMODE =   UART_MODE_ENABLE|UART_MODE_WAKEUP_ON_START|
			UART_MODE_STSEL_2STOPBIT;
	uad->UxSTA  =   UART_STA_TXMODE_TXBEMPTY|UART_STA_RXENA|UART_STA_TXENA;

//        irq_register(&uart_tx_irq_interface);
	return 1;
}
