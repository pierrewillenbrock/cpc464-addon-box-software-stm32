
#include "usbchannel.hpp"

#include <usb/usb.hpp>
#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>
#include "usbpriv.hpp"

#include "usbdev.h"

#include <bsp/stm32f4xx.h>

static OTG_Core_TypeDef * const otgc = OTGF_CORE;
static OTG_Host_TypeDef * const otgh = OTGF_HOST;

USBChannel::USBChannel(unsigned index)
  : state(Unused)
  , regs(&otgh->channels[index])
  , channeldata(OTGF_EP->endpoints[index].DFIFO)
  , index(index)
{}

void USBChannel::PTXPossible() {
	if (!(state == TXWait))
		return;
	int required_space = data_remaining;
	if (required_space > current_urb->endpoint->max_packet_length)
		required_space = current_urb->endpoint->max_packet_length;
	int available_space = otgh->HPTXSTS & OTG_HPTXSTS_PTXFSAV_MASK;
	int available_queue = otgh->HPTXSTS & OTG_HPTXSTS_PTXQSAV_MASK;
	if (available_queue == 0 ||
	    required_space > available_space)
		return;
	//we got a queue space and we got space for the data. push a packet.
	unsigned int nt = required_space >> 2;
	if (required_space & 3)
		nt++;
	for(unsigned int i = 0; i < nt; i++) {
		*channeldata = *data++;
	}
	if (data_remaining <= (nt << 2)) {
		data_remaining = 0;
		state = TXResult;
	} else {
		data_remaining -= nt << 2;
	}
}

void USBChannel::NPTXPossible() {
	if (!(state == TXWait || state == CtlSetupTXWait ||
	      state == CtlDataTXWait || state == CtlStatusTXWait))
		return;
	int required_space = data_remaining;
	if (required_space > current_urb->endpoint->max_packet_length)
		required_space = current_urb->endpoint->max_packet_length;
	int available_space = otgc->GNPTXSTS & OTG_GNPTXSTS_NPTXFSAV_MASK;
	int available_queue = otgc->GNPTXSTS & OTG_GNPTXSTS_NPTXQSAV_MASK;
	if (available_queue == 0 ||
	    required_space > available_space)
		return;
	//we got a queue space and we got space for the data. push a packet.
	unsigned int nt = required_space >> 2;
	if (required_space & 3)
		nt++;
	for(unsigned int i = 0; i < nt; i++) {
		*channeldata = *data++;
	}
	if (data_remaining <= (nt << 2)) {
		data_remaining = 0;
		switch(state) {
		case TXWait: state = TXResult; break;
		case CtlSetupTXWait: state = CtlSetupTXResult; break;
		case CtlDataTXWait: state = CtlDataTXResult; break;
		case CtlStatusTXWait: state = CtlStatusTXResult; break;
		default: assert(0); break;
		}
	} else {
		data_remaining -= nt << 2;
	}
}

void USBChannel::RXData(unsigned bcnt, unsigned dpid) {
	if (!(state == RXWait || state == CtlDataRXWait ||
	      state == CtlStatusRXWait))
		return;
	//okay to read bcnt bytes from our fifo!
	//we assume data is 4 byte aligned so we don't overwrite important
	//data with doing 4 byte accesses.
	while(bcnt > 0) {
		uint32_t d = *channeldata;
		unsigned c = (bcnt >= 4)?4:bcnt;//size of this uint32_t copy
		if (data_remaining >= c) {
			*data++ = d;
			data_remaining -= c;
		} else if (data_remaining > 0) {
			*data++ = d;
			data_remaining = 0;
		}
		bcnt -= c;
	}
	regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
	current_urb->endpoint->dataToggleIN = dpid != 0x2;
}

void USBChannel::INT() {
	/* observations:
	   if (HCCHAR & 0xc0000000) == 0, trying to disable it will not
	   create any irqs.
	   if (HCCHAR & 0xc0000000) == 0xc0000000, the only thing that works
           is disabling it(it does not disable by itself). emits an irq when
	   done.
	   if (HCCHAR & 0xc0000000) == 0x80000000, there will be an irq at some
	   point.
	   (HCCHAR & 0xc0000000) == 0x40000000 does not appear to happen.
	 */
	URB *u = current_urb;
	if (regs->HCINT & OTG_HCINT_XFRC) {
		regs->HCINT = OTG_HCINT_XFRC;
		//transfer done.
		switch(state) {
		case TXResult:
			//done.
			//the channel disabled itself at this point.
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
					    OTG_HCINTMSK_TXERRM |
					    OTG_HCINTMSK_NAKM |
					    OTG_HCINTMSK_STALLM |
					    OTG_HCINTMSK_CHHM);
			if (current_urb->endpoint->type != USBEndpoint::IRQ &&
			    current_urb->endpoint->type != USBEndpoint::ISO) {
				state = Unused;
				current_urb = NULL;
				u->completion(0, u);
			} else {
				state = PeriodicWait;
				u->completion(0, u);
			}
			break;
		case CtlSetupTXResult:
			current_urb->endpoint->dataToggleOUT = true;
			//the channel disabled itself at this point.
			if (u->buffer_len > 0) {
				if (u->setup.bmRequestType & 0x80) {
					//that's IN
					state = CtlDataRXWait;
					doINTransfer(u->buffer, u->buffer_len);
				} else {
					state = CtlDataTXWait;
					doOUTTransfer(u->buffer, u->buffer_len);
				}
			} else {
				if (u->setup.bmRequestType & 0x80) {
					state = CtlStatusTXWait;
					doOUTTransfer(u->buffer, 0);
				} else {
					state = CtlStatusRXWait;
					doINTransfer(u->buffer, 0);
				}
			}
			break;
		case CtlDataTXResult:
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			//the channel disabled itself at this point.
			state = CtlStatusRXWait;
			doINTransfer(u->buffer, 0);
			break;
		case CtlDataRXWait:
			//the channel disabled itself at this point.
			state = CtlStatusTXWait;
			u->buffer_received = u->buffer_len - data_remaining;
			doOUTTransfer(u->buffer, 0);
			break;
		case CtlStatusTXResult:
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			//fall through
		case CtlStatusRXWait:
			//the channel disabled itself at this point.
			state = Unused;
			current_urb = NULL;
			u->completion(0, u);
			break;
		case RXWait:
			//the channel disabled itself at this point.
			regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
					    OTG_HCINTMSK_TXERRM |
					    OTG_HCINTMSK_NAKM |
					    OTG_HCINTMSK_STALLM |
					    OTG_HCINTMSK_CHHM);
			u->buffer_received = u->buffer_len - data_remaining;
			if (current_urb->endpoint->type != USBEndpoint::IRQ &&
			    current_urb->endpoint->type != USBEndpoint::ISO) {
				state = Unused;
				current_urb = NULL;
				u->completion(0, u);
			} else {
				state = PeriodicWait;
				u->completion(0, u);
			}
			break;
		case TXWait: //failed for some reason. keep the chain going.
		case CtlSetupTXWait:
		case CtlDataTXWait:
		case CtlStatusTXWait:
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
					    OTG_HCINTMSK_TXERRM |
					    OTG_HCINTMSK_NAKM |
					    OTG_HCINTMSK_STALLM);
			regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
			state = Disabling;
			u->buffer_received = u->buffer_len - data_remaining;
			current_urb = NULL;
			u->completion(1, u);
			break;
		default: assert(0); break;
		}
	}
	if (regs->HCINT & OTG_HCINT_TXERR) {
		regs->HCINT = OTG_HCINT_TXERR;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCCHAR |= OTG_HCCHAR_CHDIS;
		regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
				    OTG_HCINTMSK_TXERRM |
				    OTG_HCINTMSK_NAKM |
				    OTG_HCINTMSK_STALLM);
		regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
		state = Disabling;
		u->buffer_received = u->buffer_len - data_remaining;
		current_urb = NULL;
		u->completion(1, u);
	}
	if (regs->HCINT & OTG_HCINT_STALL) {
		//STALL during any part of a Control transfer means the device
		//is unhappy with it and cannot proceed.
		regs->HCINT = OTG_HCINT_STALL;
		switch(state) {
		case CtlDataRXWait:
		case CtlStatusRXWait:
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
					    OTG_HCINTMSK_TXERRM |
					    OTG_HCINTMSK_NAKM |
					    OTG_HCINTMSK_STALLM);
			regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
			state = Disabling;
			u->buffer_received = u->buffer_len - data_remaining;
			current_urb = NULL;
			u->completion(1, u);
			break;
		case RXWait:
			//only BULK and IRQ(and Control) can generate STALL
			if (current_urb->endpoint->type == USBEndpoint::IRQ) {
				state = PeriodicWait;
			}
			break;
		default: assert(0); break;
		}

	}
	if (regs->HCINT & OTG_HCINT_NAK) {
		regs->HCINT = OTG_HCINT_NAK;
		//NAK means the device is busy. in TX case, we need to resend
		//the last packet (need more infrastructure for that),
		//in RX case, we "just" need to retrigger sending IN tokens.
		//the data pointer already points to the right place.
		//need to regenerate TX toggle status from register, i think.
		switch(state) {
		case CtlDataRXWait:
			doINTransfer(data, data_remaining);
			return;
		case CtlStatusRXWait:
			doINTransfer(u->buffer, 0);
			return;
		case RXWait:
			//only BULK and IRQ(and Control) can generate NAK
			if (current_urb->endpoint->type == USBEndpoint::IRQ) {
				//we need to shutdown the channel here.
				regs->HCINT = OTG_HCINT_CHH;
				regs->HCCHAR |= OTG_HCCHAR_CHDIS;
				regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
						    OTG_HCINTMSK_TXERRM |
						    OTG_HCINTMSK_NAKM |
						    OTG_HCINTMSK_STALLM);
				regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
				state = DisablingPeriodic;
			}
			break;
		default: assert(0); break;
		}

		/*
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCCHAR |= OTG_HCCHAR_CHDIS;
		regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
				    OTG_HCINTMSK_TXERRM |
				    OTG_HCINTMSK_NAKM |
				    OTG_HCINTMSK_STALLM);
		regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
		state = Disabling;
		u->buffer_received = u->buffer_len - data_remaining;
		current_urb = NULL;
		u->completion(1, u);
		*/
	}
	if (regs->HCINT & OTG_HCINT_CHH) {
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCINTMSK &= ~OTG_HCINTMSK_CHHM;
		switch(state) {
		case Disabling:
			state = Unused;
			break;
		case DisablingPeriodic:
			state = PeriodicWait;
			break;
		default: assert(0); break;
		}
		if (state == Unused) {
			otgh->HAINTMSK &= ~(1 << index);
			//and now look at the queue.
			URB *u = USB_getNextURB();
			if (u)
				setupForURB(u);
			return;
		}
	}
}

void USBChannel::doSETUPTransfer() {
	uint32_t hcchar = (current_urb->endpoint->device.eaddress << 22)
		| ((current_urb->endpoint->address & 0xf) << 11)
		| current_urb->endpoint->max_packet_length;
	if ((otgh->HFNUM & 1) == 0)
		hcchar |= OTG_HCCHAR_ODDFRM;
	uint32_t hctsiz = 0;
	hctsiz = 0x60000000;
	if (current_urb->endpoint->device.speed == USBSpeed::Low)
		hcchar |= 0x20000;
	hctsiz |= 1 << 19;
	hctsiz |= 8;
	regs->HCCHAR = hcchar;
	regs->HCINTMSK = OTG_HCINTMSK_XFRCM | OTG_HCINTMSK_TXERRM |
		OTG_HCINTMSK_NAKM | OTG_HCINTMSK_STALLM;
	regs->HCTSIZ = hctsiz;
	regs->HCCHAR |= OTG_HCCHAR_CHENA;

	current_urb->endpoint->dataToggleIN = true;
	current_urb->endpoint->dataToggleOUT = true;
	//now we need to wait for enough space in the non-periodic tx fifo,
	//write the 8 setup bytes and wait for a result.

	data = (uint32_t*)&current_urb->setup;
	data_remaining = 8;

	otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM;
}

void USBChannel::doINTransfer(void *data, size_t xfrsiz) {
	uint32_t hcchar = (current_urb->endpoint->device.eaddress << 22)
		| ((current_urb->endpoint->address & 0xf) << 11)
		| current_urb->endpoint->max_packet_length;
	uint32_t hctsiz = 0;

	this->data = (uint32_t*)data;
	data_remaining = xfrsiz;

	switch(current_urb->endpoint->type) {
	case USBEndpoint::Control: hcchar |= 0; break;
	case USBEndpoint::Bulk: hcchar |= 0x80000; break;
	case USBEndpoint::ISO: hcchar |= 0x40000; break;
	case USBEndpoint::IRQ: hcchar |= 0xc0000; break;
	}
	if (current_urb->buffer_len <=
	    current_urb->endpoint->max_packet_length) {
		hcchar |= 0x100000;
	} else if (current_urb->buffer_len <=
		   current_urb->endpoint->max_packet_length*2) {
		hcchar |= 0x200000;
	} else {
		hcchar |= 0x300000;
	}
	hctsiz = current_urb->endpoint->dataToggleIN?0x40000000:0;
	if (current_urb->endpoint->device.speed == USBSpeed::Low)
		hcchar |= 0x20000;
	//if it is evenly divisable, we need to add one more (empty) packet.
	//if it is not evenly divisable, we need to add one to round up.
	unsigned pktcnt = xfrsiz / current_urb->endpoint->max_packet_length + 1;
	hctsiz |= pktcnt << 19;
	hcchar |= 0x8000;
	xfrsiz = pktcnt * current_urb->endpoint->max_packet_length;

	hctsiz |= xfrsiz;

	regs->HCCHAR = hcchar;
	regs->HCINTMSK = OTG_HCINTMSK_XFRCM | OTG_HCINTMSK_TXERRM |
		OTG_HCINTMSK_NAKM | OTG_HCINTMSK_STALLM;
	regs->HCTSIZ = hctsiz;
	regs->HCCHAR |= OTG_HCCHAR_CHENA;
	//and now, we can go ahead and stream the data into the fifo.
	//probably need to acquire exclusive access on the fifo first?
	//for control transfers, SETUP, IN/OUT DATA and STATUS phases
	//are seperate transactions.
	//STATUS for IN control transfers is an OUT packet with 0 or more data bytes.
	//STATUS for OUT control transfers is an IN packet with 0 data bytes

	otgc->GINTMSK |= OTG_GINTMSK_RXFLVLM;
}

void USBChannel::doOUTTransfer(void *data, size_t xfrsiz) {
	uint32_t hcchar = (current_urb->endpoint->device.eaddress << 22)
		| ((current_urb->endpoint->address & 0xf) << 11)
		| current_urb->endpoint->max_packet_length;
	uint32_t hctsiz = 0;

	this->data = (uint32_t*)data;
	data_remaining = xfrsiz;

	switch(current_urb->endpoint->type) {
	case USBEndpoint::Control: hcchar |= 0; break;
	case USBEndpoint::Bulk: hcchar |= 0x80000; break;
	case USBEndpoint::ISO: hcchar |= 0x40000; break;
	case USBEndpoint::IRQ: hcchar |= 0xc0000; break;
	}
	hctsiz = current_urb->endpoint->dataToggleOUT?0x40000000:0;
	if (current_urb->endpoint->device.speed == USBSpeed::Low)
		hcchar |= 0x20000;
	//if it is evenly divisable, we need to add one more (empty) packet.
	//if it is not evenly divisable, we need to add one to round up.
	unsigned pktcnt = xfrsiz / current_urb->endpoint->max_packet_length+1;
	hctsiz |= pktcnt << 19;
	hctsiz |= xfrsiz;
	regs->HCCHAR = hcchar;
	regs->HCINTMSK = OTG_HCINTMSK_XFRCM | OTG_HCINTMSK_TXERRM |
		OTG_HCINTMSK_NAKM | OTG_HCINTMSK_STALLM;
	regs->HCTSIZ = hctsiz;
	regs->HCCHAR |= OTG_HCCHAR_CHENA;
	//and now, we can go ahead and stream the data into the fifo.
	//probably need to acquire exclusive access on the fifo first?
	//for control transfers, SETUP, IN/OUT DATA and STATUS phases
	//are seperate transactions.
	//STATUS for IN control transfers is an OUT packet with 0 or more data bytes.
	//STATUS for OUT control transfers is an IN packet with 0 data bytes

	switch(current_urb->endpoint->type) {
	case USBEndpoint::Control:
	case USBEndpoint::Bulk:
		otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM; break;
	case USBEndpoint::ISO:
	case USBEndpoint::IRQ:
		otgc->GINTMSK |= OTG_GINTMSK_PTXFEM; break;
	}
}

void USBChannel::setupForURB(URB *u) {
	//okay, now we are talking. we'll have to remember how much we
	//wrote already, and the odd/even data packet numbers.
	//(SETUP packets reset that one)
	current_urb = u;
	switch(u->endpoint->type) {
	case USBEndpoint::Control:
		state = CtlSetupTXWait;
		doSETUPTransfer();
		break;
	case USBEndpoint::Bulk:
		if (u->endpoint->direction == USBEndpoint::DeviceToHost) {
			state = RXWait;
			doINTransfer(u->buffer, u->buffer_len);
		} else {
			state = TXWait;
			doOUTTransfer(u->buffer, u->buffer_len);
		}
		break;
		//todo: plan for ISO/IRQ: wait for SOF, which makes
		//all channels push their payloads on the bus
	case USBEndpoint::ISO:
	case USBEndpoint::IRQ:
		otgc->GINTMSK |= OTG_GINTMSK_SOFM;
		state = PeriodicWait;
		frameCounter = u->pollingInterval-1;
		break;
	}
	otgh->HAINTMSK |= 1 << index;
}

void USBChannel::retireURB(URB *u) {
	if (current_urb == u) {
		if (regs->HCCHAR & OTG_HCCHAR_CHENA) {
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK &= ~(OTG_HCINTMSK_XFRCM |
					    OTG_HCINTMSK_TXERRM |
					    OTG_HCINTMSK_NAKM |
					    OTG_HCINTMSK_STALLM);
			regs->HCINTMSK |= OTG_HCINTMSK_CHHM;
			state = Disabling;
		} else {
			state = Unused;
		}
		current_urb = NULL;
	}
}

void USBChannel::SOF() {
	if (!current_urb)
		return;
	if (current_urb->endpoint->type != USBEndpoint::IRQ &&
	    current_urb->endpoint->type != USBEndpoint::ISO)
		return;
	if (state != PeriodicWait)
		return;
	//okay, now we need to check if it is time to do a transfer.
	frameCounter++;
	if (frameCounter >= current_urb->pollingInterval) {
		frameCounter = 0;
		if (current_urb->endpoint->direction ==
		    USBEndpoint::DeviceToHost) {
			state = RXWait;
			doINTransfer(current_urb->buffer,
				     current_urb->buffer_len);
		} else {
			state = TXWait;
			doOUTTransfer(current_urb->buffer,
				      current_urb->buffer_len);
		}
	}
}
