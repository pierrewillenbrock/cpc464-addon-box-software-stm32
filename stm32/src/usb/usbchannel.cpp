
#include "usbchannel.hpp"

#include <usb/usb.hpp>
#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>
#include <eventlogger.hpp>
#include "usbpriv.hpp"

#include "usbdev.h"

#include <bsp/stm32f4xx.h>

static OTG_Core_TypeDef * const otgc = OTGF_CORE;
static OTG_Host_TypeDef * const otgh = OTGF_HOST;

#define USB_CHAN_OPERATING_INTMASK					\
  ( OTG_HCINTMSK_XFRCM | OTG_HCINTMSK_DTERRM | OTG_HCINTMSK_BBERRM |	\
    OTG_HCINTMSK_FRMORM | OTG_HCINTMSK_ACKM |				\
    OTG_HCINTMSK_TXERRM | OTG_HCINTMSK_NAKM | OTG_HCINTMSK_STALLM )

#define USB_CHAN_DISABLE_INTMASK OTG_HCINTMSK_CHHM

#define USB_CHAN_IDLE_INTMASK (0)

USBChannel::USBChannel(unsigned index)
  : state(Unused)
  , regs(&otgh->channels[index])
  , channeldata(OTGF_EP->endpoints[index].DFIFO)
  , index(index)
{}

void USBChannel::PTXPossible() {
	if (!(state == TXWait))
		return;
	assert(current_urb);
	if (current_urb->endpoint->type != USBEndpoint::ISO &&
	    current_urb->endpoint->type != USBEndpoint::IRQ)
		return;
	int required_space = data_remaining;
	if (required_space > current_urb->endpoint->max_packet_length)
		required_space = current_urb->endpoint->max_packet_length;
	int available_space = (otgh->HPTXSTS & OTG_HPTXSTS_PTXFSAV_MASK) * 4;
	int available_queue = (otgh->HPTXSTS & OTG_HPTXSTS_PTXQSAV_MASK) >>
		OTG_HPTXSTS_PTXQSAV_SHIFT;
	if (available_queue == 0 ||
	    required_space > available_space)
		return;
	LogEvent("USBChannel: writing periodic");
	//we got a queue space and we got space for the data. push a packet.
	unsigned int nt = required_space >> 2;
	if (required_space & 3)
		nt++;
	uint32_t *d = data;
	for(unsigned int i = 0; i < nt; i++) {
		*channeldata = *d++;
	}
	data_packetsize = nt;
	state = TXResult;
}

volatile uint32_t usb_frame_pos = 0;

void USBChannel::NPTXPossible() {
	if (!(state == TXWait || state == CtlSetupTXWait ||
	      state == CtlDataTXWait || state == CtlStatusTXWait))
		return;
	assert(current_urb);
	if (current_urb->endpoint->type != USBEndpoint::Control &&
	    current_urb->endpoint->type != USBEndpoint::Bulk)
		return;
	int required_space = data_remaining;
	if (required_space > current_urb->endpoint->max_packet_length)
		required_space = current_urb->endpoint->max_packet_length;
	int available_space = (otgc->HNPTXSTS & OTG_HNPTXSTS_NPTXFSAV_MASK) * 4;
	int available_queue = (otgc->HNPTXSTS & OTG_HNPTXSTS_NPTXQSAV_MASK) >>
		OTG_HNPTXSTS_NPTXQSAV_SHIFT;
	if (available_queue == 0 ||
	    required_space > available_space)
		return;
	LogEvent("USBChannel: writing non periodic");
	usb_frame_pos = otgh->HFNUM;
	//we got a queue space and we got space for the data. push a packet.
	unsigned int nt = required_space >> 2;
	if (required_space & 3)
		nt++;
	uint32_t *d = data;
	for(unsigned int i = 0; i < nt; i++) {
		*channeldata = *d++;
	}
	data_packetsize = nt;
	switch(state) {
	case TXWait: state = TXResult; break;
	case CtlSetupTXWait: state = CtlSetupTXResult; break;
	case CtlDataTXWait: state = CtlDataTXResult; break;
	case CtlStatusTXWait: state = CtlStatusTXResult; break;
	default: assert(0); break;
	}
}

void USBChannel::RXData(unsigned bcnt, unsigned dpid) {
	if (!(state == RXWait || state == CtlDataRXWait ||
	      state == CtlStatusRXWait))
		return;
	LogEvent("USBChannel: reading");
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
	uint32_t hcint = regs->HCINT & regs->HCINTMSK;
	LogEvent("USBChannel::INT()");
	if (!(regs->HCCHAR & OTG_HCCHAR_CHENA)) {
		//enumerate all the states+event combos where we know that
		//the hardware disables the channel
		if ((hcint & OTG_HCINT_XFRC) &&
		    (state == TXResult || state == CtlSetupTXResult ||
		     state == CtlDataTXResult || state == CtlDataRXWait ||
		     state == CtlStatusTXResult || state == CtlStatusRXWait ||
		     state == RXWait)) {
		} else if ((hcint & OTG_HCINT_ACK) &&
		    (state == TXResult || state == CtlSetupTXResult ||
		     state == CtlDataTXResult || state == CtlDataRXWait ||
		     state == CtlStatusTXResult || state == CtlStatusRXWait ||
		     state == RXWait)) {
		} else if ((regs->HCINT & OTG_HCINT_CHH) &&
			   (state == Disabling || state == DisablingPeriodic ||
			    state == DisablingTX ||
			    state == DisablingCtlSetupTX ||
			    state == DisablingCtlDataTX ||
			    state == DisablingCtlStatusTX)) {
		} else {
			assert(0);
		}
	}
	URB *u = current_urb;
	if (hcint & OTG_HCINT_ACK) {
		hcint &= ~OTG_HCINT_ACK;
		regs->HCINT = OTG_HCINT_ACK;
		//ACK means the packet was handled correctly.
		switch(state) {
		case TXResult:
		case CtlSetupTXResult:
		case CtlStatusTXResult:
		case CtlDataTXResult:
			LogEvent("USBChannel: ACK in *TXResult");
			//this is used for all TX transmissions.
			data += data_packetsize;
			if (data_remaining <= (data_packetsize << 2)) {
				data_remaining = 0;
				data_packetsize = 0;
			} else {
				data_remaining -= data_packetsize << 2;
				data_packetsize = 0;
				switch(state) {
				case TXResult: state = TXWait; break;
				case CtlSetupTXResult: state = CtlSetupTXWait; break;
				case CtlStatusTXResult: state = CtlStatusTXWait; break;
				case CtlDataTXResult: state = CtlDataTXWait; break;
				default: assert(0); break;
				}
				switch(current_urb->endpoint->type) {
				case USBEndpoint::Control:
				case USBEndpoint::Bulk:
					otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM; break;
				case USBEndpoint::ISO:
				case USBEndpoint::IRQ:
					otgc->GINTMSK |= OTG_GINTMSK_PTXFEM; break;
				}
			}
			break;
		case RXWait:
		case CtlDataRXWait:
		case CtlStatusRXWait:
			//handled automatically.
			break;
		default: assert(0); break;
		}
	}
	if (hcint & OTG_HCINT_NAK) {
		//it looks like TXERR and NAK are generally handled the same
		//for OUT transactions. for IN, it may be safe to just wait?
		//so, todo: add infrastructure to track the current packets
		//starting address+size and only increment on ACK
		hcint &= ~OTG_HCINT_NAK;
		regs->HCINT = OTG_HCINT_NAK;
		//NAK means the device is busy. in TX case, we need to resend
		//the last packet? (need more infrastructure for that),
		//in RX case, we "just" need to retrigger sending IN tokens?.
		//the data pointer already points to the right place.
		//need to regenerate TX toggle status from register, i think.
		switch(state) {
		case RXWait:
			LogEvent("USBChannel: NAK in RXWait");
			//only BULK and IRQ(and Control above) can generate NAK
			if (current_urb->endpoint->type == USBEndpoint::IRQ) {
				//we need to shutdown the channel here.
				regs->HCINT = OTG_HCINT_CHH;
				regs->HCCHAR |= OTG_HCCHAR_CHDIS;
				regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
				state = DisablingPeriodic;
			} else {
				//bulk read just keeps waiting?
				regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			}
			break;
		case CtlDataRXWait:
			LogEvent("USBChannel: NAK in CtlDataRXWait");
			//doINTransfer(data, data_remaining);//this seems to work. todo: check if we can get away with just the state advance.
			regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			break;
		case CtlStatusRXWait:
			LogEvent("USBChannel: NAK in CtlStatusRXWait");
			//status is an empty DATA1 transfer
			//doINTransfer(u->buffer, 0);//this seems to work. todo: check if we can get away with just the state advance.
			regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			break;
		case CtlSetupTXResult:
			LogEvent("USBChannel: NAK in CtlStatusTXResult");
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			state = DisablingCtlSetupTX;
			data_packetsize = 0;
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			break;
		case CtlDataTXResult:
			LogEvent("USBChannel: NAK in CtlStatusTXResult");
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			state = DisablingCtlDataTX;
			data_packetsize = 0;
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			break;
		case CtlStatusTXResult:
			LogEvent("USBChannel: NAK in CtlStatusTXResult");
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			state = DisablingCtlStatusTX;
			data_packetsize = 0;
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			break;
		default: assert(0); break;
		}
	}
	if (hcint & OTG_HCINT_XFRC) {
		hcint &= ~OTG_HCINT_XFRC;
		regs->HCINT = OTG_HCINT_XFRC;
		//transfer done.
		switch(state) {
		case TXResult:
			LogEvent("USBChannel: XFRC in TXResult");
			//done.
			//the channel disabled itself at this point.
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			regs->HCINTMSK = USB_CHAN_IDLE_INTMASK;
			if (current_urb->endpoint->type != USBEndpoint::IRQ &&
			    current_urb->endpoint->type != USBEndpoint::ISO) {
				state = Unused;
				current_urb = NULL;
				u->result = URB::Ack;
				u->completion(0, u);
			} else {
				state = PeriodicWait;
				u->result = URB::Ack;
				u->completion(0, u);
			}
			break;
		case CtlSetupTXResult:
			LogEvent("USBChannel: XFRC in CtlSetupTXResult");
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
			LogEvent("USBChannel: XFRC in CtlDataTXResult");
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			//the channel disabled itself at this point.
			state = CtlStatusRXWait;
			doINTransfer(u->buffer, 0);
			break;
		case CtlDataRXWait:
			LogEvent("USBChannel: XFRC in CtlDataRXWait");
			//the channel disabled itself at this point.
			state = CtlStatusTXWait;
			u->buffer_received = u->buffer_len - data_remaining;
			doOUTTransfer(u->buffer, 0);
			break;
		case CtlStatusTXResult:
			LogEvent("USBChannel: XFRC in CtlStatusTXResult");
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			//fall through
		case CtlStatusRXWait:
			LogEvent("USBChannel: XFRC in CtlStatusRXWait");
			//the channel disabled itself at this point.
			state = Unused;
			current_urb = NULL;
			u->result = URB::Ack;
			u->completion(0, u);
			break;
		case RXWait:
			LogEvent("USBChannel: XFRC in RXWait");
			//the channel disabled itself at this point.
			regs->HCINTMSK = USB_CHAN_IDLE_INTMASK;
			u->buffer_received = u->buffer_len - data_remaining;
			if (current_urb->endpoint->type != USBEndpoint::IRQ &&
			    current_urb->endpoint->type != USBEndpoint::ISO) {
				state = Unused;
				current_urb = NULL;
				u->result = URB::Ack;
				u->completion(0, u);
			} else {
				state = PeriodicWait;
				u->result = URB::Ack;
				u->completion(0, u);
			}
			break;
		case TXWait: //failed for some reason. keep the chain going.
		case CtlSetupTXWait:
		case CtlDataTXWait:
		case CtlStatusTXWait:
			assert(0);
			LogEvent("USBChannel: XFRC in *TXWait");
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			state = Disabling;
			u->buffer_received = u->buffer_len - data_remaining;
			current_urb = NULL;
			u->result = URB::Ack;
			u->completion(1, u);
			break;
		default: assert(0); break;
		}
	}
	if (hcint & OTG_HCINT_DTERR) {
		//must disable the channel when getting DTERR (stm32f4 34.17.4 Halting a channel)
		LogEvent("USBChannel: DTERR");
		hcint &= ~OTG_HCINT_DTERR;
		regs->HCINT = OTG_HCINT_DTERR;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCCHAR |= OTG_HCCHAR_CHDIS;
		regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
		state = Disabling;
		u->buffer_received = u->buffer_len - data_remaining;
		current_urb = NULL;
		u->result = URB::DTErr;
		u->completion(1, u);
	}
	if (hcint & OTG_HCINT_FRMOR) {
		hcint &= ~OTG_HCINT_FRMOR;
		assert(0);
	}
	if (hcint & OTG_HCINT_BBERR) {
		//must disable the channel when getting BBERR (stm32f4 34.17.4 Halting a channel)
		hcint &= ~OTG_HCINT_BBERR;
		assert(0);
	}
	if (hcint & OTG_HCINT_TXERR) {
		//TXERR means the device failed to send data.
		hcint &= ~OTG_HCINT_TXERR;
		regs->HCINT = OTG_HCINT_TXERR;
		switch(state) {
		case CtlSetupTXResult:
			LogEvent("USBChannel: TXERR in CtlSetupTXResult");
			state = DisablingCtlSetupTX;
			break;
		case CtlDataTXResult:
			LogEvent("USBChannel: TXERR in CtlDataTXResult");
			state = DisablingCtlDataTX;
			break;
		case CtlStatusTXResult:
			LogEvent("USBChannel: TXERR in CtlStatusTXResult");
			state = DisablingCtlStatusTX;
			break;
		case RXWait:
			LogEvent("USBChannel: TXERR in RXWait");
			state = Disabling;
			current_urb = NULL;
			u->result = URB::TXErr;
			u->completion(1, u);
			break;
		default: assert(0); break;
		}
		data_packetsize = 0;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCCHAR |= OTG_HCCHAR_CHDIS;
		regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
	}
	if (hcint & OTG_HCINT_STALL) {
		//must disable the channel when getting STALL (stm32f4 34.17.4 Halting a channel)
		//STALL during any part of a Control transfer means the device
		//is unhappy with it and cannot proceed.
		hcint &= ~OTG_HCINT_STALL;
		regs->HCINT = OTG_HCINT_STALL;
		switch(state) {
		case CtlDataRXWait:
		case CtlStatusRXWait:
		case CtlSetupTXResult:
		case CtlDataTXResult:
		case CtlStatusTXResult:
			LogEvent("USBChannel: STALL in Ctl*RXWait/*TXResult");
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			state = Disabling;
			u->result = URB::Stall;
			u->buffer_received = u->buffer_len - data_remaining;
			current_urb = NULL;
			u->completion(1, u);
			break;
		case RXWait:
			LogEvent("USBChannel: STALL in RXWait");
			//only BULK and IRQ(and Control) can generate STALL
			if (current_urb->endpoint->type == USBEndpoint::IRQ) {
				state = PeriodicWait;
			}
			break;
		default: assert(0); break;
		}

	}
	if (hcint & OTG_HCINT_CHH) {
		hcint &= ~OTG_HCINT_CHH;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCINTMSK = USB_CHAN_IDLE_INTMASK;
		switch(state) {
		case Disabling:
			state = Unused;
			break;
		case DisablingPeriodic:
			state = PeriodicWait;
			break;
		case DisablingTX:
			state = TXWait;
			doOUTTransfer(data, data_remaining);
			break;
		case DisablingCtlSetupTX:
			state = CtlSetupTXWait;
			doOUTTransfer(data, data_remaining);
			break;
		case DisablingCtlDataTX:
			state = CtlDataTXWait;
			doOUTTransfer(data, data_remaining);
			break;
		case DisablingCtlStatusTX:
			state = CtlStatusTXWait;
			doOUTTransfer(data, data_remaining);
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
	assert(hcint == 0);
}

void USBChannel::doSETUPTransfer() {
	assert((regs->HCCHAR & OTG_HCCHAR_CHENA) == 0);
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
	regs->HCINTMSK = USB_CHAN_OPERATING_INTMASK;
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
	assert((regs->HCCHAR & OTG_HCCHAR_CHENA) == 0);
	uint32_t hcchar = (current_urb->endpoint->device.eaddress << 22)
		| ((current_urb->endpoint->address & 0xf) << 11)
		| current_urb->endpoint->max_packet_length;
	if ((otgh->HFNUM & 1) == 0)
		hcchar |= OTG_HCCHAR_ODDFRM;
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
	regs->HCINTMSK = USB_CHAN_OPERATING_INTMASK;
	regs->HCTSIZ = hctsiz;
	regs->HCCHAR |= OTG_HCCHAR_CHENA;
	//and now, we can go ahead and stream the data into the fifo.
	//probably need to acquire exclusive access on the fifo first?
	//for control transfers, SETUP, IN/OUT DATA and STATUS phases
	//are seperate transactions.
	//STATUS for IN control transfers is an OUT packet with 0 or more data bytes.
	//STATUS for OUT control transfers is an IN packet with 0 data bytes
}

void USBChannel::doOUTTransfer(void *data, size_t xfrsiz) {
	assert((regs->HCCHAR & OTG_HCCHAR_CHENA) == 0);
	uint32_t hcchar = (current_urb->endpoint->device.eaddress << 22)
		| ((current_urb->endpoint->address & 0xf) << 11)
		| current_urb->endpoint->max_packet_length;
	if ((otgh->HFNUM & 1) == 0)
		hcchar |= OTG_HCCHAR_ODDFRM;
	uint32_t hctsiz = 0;

	this->data = (uint32_t*)data;
	data_packetsize = 0;
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
	regs->HCINTMSK = USB_CHAN_OPERATING_INTMASK;
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
	assert(u->endpoint);
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
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
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

void USBChannel::killEndpoint(USBEndpoint *endpoint) {
	if (!current_urb)
		return;
	if (current_urb->endpoint == endpoint)
		retireURB(current_urb);
}

