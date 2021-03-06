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
 *      CP0 EIC (Extended Interrupt Controller) support and emulation
 *
 *      Multiple IPLs are supported in guests
 *      Root works in IPL0 (for now) to prevent interrupt delay for
 *      more priority guest, it may cause a stray IRQ in some guests.
 *
 *      After IRQ injection into guest CP0 root starts GHFC tracking
 *      to catch a moment then interrupt really happens in guest
 *      (via change of Status.EXL bit).
 *      It stops tracking after that (or injects a next IRQ).
 *
 *      Special handling for guest's CP0 COMPARE IRQ in accordance with Arch
 */

#include    "mips.h"
#include    "irq.h"
#include    "ic.h"
#include    "thread.h"

//  Cancel a current "inject IRQ" operation for guest and
//   may be - inject and next one
//
//  If cancelled - stop GHFC tracking
//
void cancel_inject_IRQ(struct exception_frame *exfr)
{
	int irq;
	unsigned int tpl;
	unsigned int voff;

	if ((irq = pickup_irq(exfr, &tpl, &voff, 0)) >= 0) {
		inject_irq(irq, tpl, voff);

		current->injected_irq = irq;
		current->injected_ipl = tpl;
		return;
	}
	write_cp0_guestctl0ext(CP0_GUESTCTL0EXT_INIT);
	current->waiting_irq_number = 0;
	write_cp0_guestctl2(0);
	current->injected_irq = -1;
	current->injected_ipl = 0;
}

//  GHFC exception while IRQ was injected
//  Maintain IRQ history and cancel a current "injected" IRQ
//
void exc_injected_handler(struct exception_frame *exfr)
{
	char str[128];

	current->interrupted_irq = current->injected_irq;
	current->last_interrupted_irq = (current->last_interrupted_irq << 8) | (current->interrupted_irq & 0xff);
	current->interrupted_ipl = current->injected_ipl;

	if (is_int_trace()) {
		sprintf(str, "Thread%d: interrupt %d, ipl %d\n", current->tid, current->interrupted_irq, current->interrupted_ipl),
		uart_writeline(console_uart, str);
	}

	cancel_inject_IRQ(exfr);
}

//  insert IRQ to guest:
//      Check IPL vs IRQ guest IPL
//      inject it if guest is current and has no an injected IRQ
//      or prepare it for injection later on or at reschedule
//
//      If guest is not 'polling' guest then set tracking GHFC events to catch
//      that IRQ injection is completed via change of Status.ERL
//
void insert_IRQ(struct exception_frame *exfr, unsigned int thread, int irq, unsigned int ipl)
{
	unsigned int oldstatus;
	unsigned int oldtid;
	unsigned int voff;
	unsigned int gc2;
	struct thread *next;

	next  = get_thread_fp(thread);
	if (next->injected_ipl) {
		if (next == current)
			gc2 = read_cp0_guestctl2();
		else
			gc2 = next->cp0_guestctl2;
		if (!get__guestctl2__gripl(gc2)) {
			next->interrupted_irq = next->injected_irq;
			next->last_interrupted_irq = (next->last_interrupted_irq << 8) | (next->interrupted_irq & 0xff);
			next->interrupted_ipl = next->injected_ipl;
			next->waiting_irq_number--;
			next->injected_irq = -1;
			next->injected_ipl = 0;
		}
	}
	next->waiting_irq_number++;
	if (next->injected_ipl >= ipl)
		return;

	oldtid = current->tid;
	if (thread != oldtid) {
		if ((get_status__ipl(next->g_cp0_status) < ipl) ||
		    !request_switch_to_thread(exfr, thread)) {

			next->thread_flags |= THREAD_FLAGS_RUNNING;

			voff = get_thread_irq2off(next, irq);
			next->cp0_guestctl2 = inject_irq_gc2(irq, ipl, voff);
			next->injected_irq = irq;
			next->injected_ipl = ipl;
			if (next->injected_ipl &&
			    !(vm_options[next->tid] & VM_OPTION_POLLING))
				next->cp0_guestctl0ext = CP0_GUESTCTL0EXT_NOFCD;

			return;
		}
	}

	next->thread_flags |= THREAD_FLAGS_RUNNING;

	voff = get_irq2off(irq);
	inject_irq(irq, ipl, voff);
	next->injected_irq = irq;
	next->injected_ipl = ipl;
	if (next->injected_ipl && !(vm_options[next->tid] & VM_OPTION_POLLING))
			write_cp0_guestctl0ext(CP0_GUESTCTL0EXT_NOFCD);
	// return to guest
	return;
}

// SW emulated IRQ to thread from time.c
//
void execute_timer_IRQ(struct exception_frame *exfr, unsigned int thread)
{
	unsigned int oldipl;
	unsigned int oldstatus;
	unsigned int oldvm;
	unsigned int gstatus;
	unsigned int ipl;
	struct thread *next  = get_thread_fp(thread);

if (!next->gid) {
char str[128];
sprintf(str, "execute_timer_IRQ: tries a non-guest thread %d\n", thread);
uart_writeline(console_uart, str);
return;
}
	ipl = get_thread_timer_ipl(next);
	set_thread_timer_irq(next);       // write IFS
	if (current->tid == thread)
		set_g_cp0_cause(CP0_CAUSE_TI);
	else
		set_thread_g_cp0_cause__ti(next);

	insert_sw_irq(thread, IC_TIMER_IRQ);
	next->waiting_irq_number++;
	enforce_irq(exfr, thread, IC_TIMER_IRQ);
}

//  IC write hook to enable timer IRQ in IC  - move to proper file?
//  If there is a timer IRQ and enabled - insert it
//
void insert_timer_IRQ(struct exception_frame *exfr, int irq)
{
	unsigned int gipl;
	unsigned int tipl;

if (!current->gid) {
char str[128];
sprintf(str, "insert_timer_IRQ: tries a non-guest thread %d\n", current->tid);
uart_writeline(console_uart, str);
return;
}
	if ((read_g_cp0_cause() & CP0_CAUSE_TI) && get_thread_timer_mask(current)) {
		insert_IRQ(exfr, current->tid, irq, get_thread_timer_ipl(current));
	}
}

































