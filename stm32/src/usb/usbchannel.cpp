
#include "usbchannel.hpp"

#include <usb/usb.hpp>
#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>
#include <eventlogger.hpp>
#include "usbpriv.hpp"
#include <deferredwork.hpp>

#include "usbdev.h"

#define USB_CHAN_OPERATING_INTMASK					\
  ( OTG_HCINTMSK_DTERRM | OTG_HCINTMSK_BBERRM |				\
    OTG_HCINTMSK_FRMORM |						\
    OTG_HCINTMSK_TXERRM | OTG_HCINTMSK_NAKM | OTG_HCINTMSK_STALLM )

#define USB_CHAN_DISABLE_INTMASK OTG_HCINTMSK_CHHM

#define USB_CHAN_IDLE_INTMASK (0)

usb::Channel::Channel (unsigned index)
  : state(Unused)
  , regs(&otgh->channels[index])
  , channeldata(OTGF_EP->endpoints[index].DFIFO)
  , index(index)
{}

void usb::Channel::PTXPossible() {
	if (!(state == TXWait))
		return;
	assert(transfer.state == Transfer::Wait);
	assert(current_urb);
	if (current_urb->endpoint->type != usb::Endpoint::ISO &&
	    current_urb->endpoint->type != usb::Endpoint::IRQ)
		return;
	int required_space = transfer.packet_remaining;
	int available_space = (otgh->HPTXSTS & OTG_HPTXSTS_PTXFSAV_MASK) * 4;
	int available_queue = (otgh->HPTXSTS & OTG_HPTXSTS_PTXQSAV_MASK) >>
		OTG_HPTXSTS_PTXQSAV_SHIFT;
	if (available_queue == 0 ||
	    required_space > available_space)
		return;
	LogEvent("USBChannel: writing periodic", this);
	//we got a queue space and we got space for the data. push a packet.
	unsigned int nt = required_space >> 2;
	if (required_space & 3)
		nt++;
	for(unsigned int i = 0; i < nt; i++) {
		*channeldata = *transfer.packet_data++;
	}
	if (transfer.packet_remaining > nt << 2) {
		transfer.packet_handled += nt << 2;
		transfer.packet_remaining -= nt << 2;
	} else {
		transfer.packet_handled += transfer.packet_remaining;
		transfer.packet_remaining = 0;
	}
	transfer.packet_size = nt;
	state = TXResult;
	regs->HCINTMSK |= OTG_HCINTMSK_ACKM;
	transfer.state = Transfer::Result;
}

void usb::Channel::NPTXPossible() {
	if (!(state == TXWait || state == CtlSetupTXWait ||
	      state == CtlDataTXWait || state == CtlStatusTXWait))
		return;
	assert(transfer.state == Transfer::Wait);
	assert(current_urb);
	if (current_urb->endpoint->type != usb::Endpoint::Control &&
	    current_urb->endpoint->type != usb::Endpoint::Bulk)
		return;
	int required_space = transfer.packet_remaining;
	int available_space = (otgc->HNPTXSTS & OTG_HNPTXSTS_NPTXFSAV_MASK) * 4;
	int available_queue = (otgc->HNPTXSTS & OTG_HNPTXSTS_NPTXQSAV_MASK) >>
		OTG_HNPTXSTS_NPTXQSAV_SHIFT;
	if (available_queue == 0 ||
	    required_space > available_space)
		return;
	LogEvent("USBChannel: writing non periodic", this);
	//we got a queue space and we got space for the data. push a packet.
	unsigned int nt = required_space >> 2;
	if (required_space & 3)
		nt++;
	for(unsigned int i = 0; i < nt; i++) {
		*channeldata = *transfer.packet_data++;
	}
	if (transfer.packet_remaining > nt << 2) {
		transfer.packet_handled += nt << 2;
		transfer.packet_remaining -= nt << 2;
	} else {
		transfer.packet_handled += transfer.packet_remaining;
		transfer.packet_remaining = 0;
	}
	transfer.packet_size = nt;
	switch(state) {
	case TXWait: state = TXResult; break;
	case CtlSetupTXWait: state = CtlSetupTXResult; break;
	case CtlDataTXWait: state = CtlDataTXResult; break;
	case CtlStatusTXWait: state = CtlStatusTXResult; break;
	default: assert(0); break;
	}
	regs->HCINTMSK |= OTG_HCINTMSK_ACKM;
	transfer.state = Transfer::Result;
}

void usb::Channel::RXData(unsigned bcnt, unsigned dpid) {
	if (!(state == RXWait || state == CtlDataRXWait ||
	      state == CtlStatusRXWait))
		return;
	assert(transfer.state == Transfer::Wait);
	LogEvent("USBChannel: reading", this);
	//okay to read bcnt bytes from our fifo!
	//we assume data is 4 byte aligned so we don't overwrite important
	//data with doing 4 byte accesses.
	while(bcnt > 0) {
		uint32_t d = *channeldata;
		unsigned c = (bcnt >= 4)?4:bcnt;//size of this uint32_t copy
		if (transfer.packet_remaining >= c) {
			*transfer.packet_data++ = d;
			transfer.packet_handled += c;
			transfer.packet_remaining -= c;
		} else if (transfer.packet_remaining > 0) {
			*transfer.packet_data++ = d;
			transfer.packet_handled += transfer.packet_remaining;
			transfer.packet_remaining = 0;
		}
		bcnt -= c;
	}
	regs->HCINTMSK |= OTG_HCINTMSK_ACKM;
	current_urb->endpoint->dataToggleIN = dpid != 0x2;
	transfer.state = Transfer::Result;
}

void usb::Channel::INT() {
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
	LogEvent("USBChannel::INT()", this);
	if (hcint == 0)
		LogEvent("USBChannel::INT: no events", this);
	if (!(regs->HCCHAR & OTG_HCCHAR_CHENA)) {
		//enumerate all the states+event combos where we know that
		//the hardware disables the channel
		if ((hcint & OTG_HCINT_XFRC) &&
		    (state == TXResult || state == CtlSetupTXResult ||
		     state == CtlDataTXResult || state == CtlDataRXWait ||
		     state == CtlStatusTXResult || state == CtlStatusRXWait ||
		     state == RXWait) && (transfer.state == Transfer::Result)) {
		} else if ((hcint & OTG_HCINT_ACK) &&
		    (state == TXResult || state == CtlSetupTXResult ||
		     state == CtlDataTXResult || state == CtlDataRXWait ||
		     state == CtlStatusTXResult || state == CtlStatusRXWait ||
		     state == RXWait) && (transfer.state == Transfer::Result)) {
		} else if ((regs->HCINT & OTG_HCINT_CHH) &&
			   (state == Disabling ||
			    state == DisablingTX ||
			    state == DisablingCtlSetupTX ||
			    state == DisablingCtlDataTX ||
			    state == DisablingCtlDataRX ||
			    state == DisablingCtlStatusTX ||
			    state == DisablingCtlStatusRX) &&
			  (transfer.state == Transfer::Disabling)) {
		} else {
			assert(0);
		}
	}
	usb::URB *u = current_urb;
	if (hcint & OTG_HCINT_ACK) {
		hcint &= ~OTG_HCINT_ACK;
		regs->HCINT = OTG_HCINT_ACK;
		//ACK means the packet was handled correctly.
		switch(state) {
		case TXResult:
		case CtlSetupTXResult:
		case CtlStatusTXResult:
		case CtlDataTXResult:
			LogEvent("USBChannel: ACK in *TXResult", this);
			assert(transfer.state == Transfer::Result);
			//this is used for all TX transmissions.
			transfer.data += transfer.packet_handled;
			if (transfer.data_remaining > transfer.packet_handled) {
				transfer.data_handled += transfer.packet_handled;
				transfer.data_remaining -= transfer.packet_handled;
			} else {
				transfer.data_handled += transfer.data_remaining;
				transfer.data_remaining = 0;
			}
			transfer.packet_handled = 0;
			transfer.packet_count--;
			if (transfer.packet_count > 0) {
				//setup for next packet
				transfer.packet_handled = 0;
				transfer.packet_remaining = transfer.data_remaining;
				if (transfer.packet_remaining > transfer.max_packet_length)
					transfer.packet_remaining = transfer.max_packet_length;
				transfer.packet_data = (uint32_t*) transfer.data;

				switch(state) {
				case TXResult: state = TXWait; break;
				case CtlSetupTXResult: state = CtlSetupTXWait; break;
				case CtlStatusTXResult: state = CtlStatusTXWait; break;
				case CtlDataTXResult: state = CtlDataTXWait; break;
				default: assert(0); break;
				}
				transfer.state = Transfer::Wait;
				switch(current_urb->endpoint->type) {
				case usb::Endpoint::Control:
				case usb::Endpoint::Bulk:
					               otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM; break;
				case usb::Endpoint::ISO:
				case usb::Endpoint::IRQ:
					               otgc->GINTMSK |= OTG_GINTMSK_PTXFEM; break;
				}
			} else {
				//done with this transfer
				//update data pointer if needed, or do this during XFRC?
				if (state == TXResult || state == CtlDataTXResult) {
					data_urb += transfer.data_handled;
					if (data_urb_remaining > transfer.data_handled) {
						data_urb_remaining -= transfer.data_handled;
					} else {
						data_urb_remaining = 0;
					}
					transfer.data_handled = 0;
					//rest is handled during XFRC
				}
				regs->HCINTMSK |= OTG_HCINTMSK_XFRCM;
			}
			regs->HCINTMSK &= ~OTG_HCINTMSK_ACKM;

			break;
		case RXWait:
		case CtlDataRXWait:
		case CtlStatusRXWait:
			LogEvent("USBChannel: ACK in *RXResult", this);
			assert(transfer.state == Transfer::Result);
			//this is used for all RX transmissions.
			transfer.data += transfer.packet_handled;
			if (transfer.data_remaining > transfer.packet_handled) {
				transfer.data_handled += transfer.packet_handled;
				transfer.data_remaining -= transfer.packet_handled;
			} else {
				transfer.data_handled += transfer.data_remaining;
				transfer.data_remaining = 0;
			}
			transfer.packet_handled = 0;
			transfer.packet_count--;
			if (transfer.packet_count > 0 && transfer.packet_remaining == 0) {
				//setup for next packet
				transfer.packet_handled = 0;
				transfer.packet_remaining = transfer.data_remaining;
				if (transfer.packet_remaining > transfer.max_packet_length)
					transfer.packet_remaining = transfer.max_packet_length;
				transfer.packet_data = (uint32_t*) transfer.data;

				transfer.state = Transfer::Wait;
			} else {
				//done with this transfer
				//rest is handled during XFRC
				regs->HCINTMSK |= OTG_HCINTMSK_XFRCM;
			}
			regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			regs->HCINTMSK &= ~OTG_HCINTMSK_ACKM;
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
			LogEvent("USBChannel: NAK in RXWait", this);
			//reset packet information
			transfer.packet_data = (uint32_t*)transfer.data;
			transfer.packet_remaining = transfer.data_remaining;
			if (transfer.packet_remaining > transfer.max_packet_length)
				transfer.packet_remaining = transfer.max_packet_length;
			transfer.packet_handled = 0;
			//only BULK and IRQ(and Control below) can generate NAK
			if (current_urb->endpoint->type == usb::Endpoint::IRQ) {
				//we need to shutdown the channel here.
				regs->HCINT = OTG_HCINT_CHH;
				regs->HCCHAR |= OTG_HCCHAR_CHDIS;
				regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
				state = Disabling;
				assert(transfer.state != Transfer::Idle &&
					transfer.state != Transfer::Disabling);
				usb::frameChannelTime -= transfer.transfer_time;
				transfer.state = Transfer::Disabling;
				current_urb = NULL;
			} else {
				//bulk read just keeps waiting?
				regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			}
			break;
		case CtlDataRXWait:
			LogEvent("USBChannel: NAK in CtlDataRXWait", this);
			//reset packet information
			transfer.packet_data = (uint32_t*)transfer.data;
			transfer.packet_remaining = transfer.data_remaining;
			if (transfer.packet_remaining > transfer.max_packet_length)
				transfer.packet_remaining = transfer.max_packet_length;
			transfer.packet_handled = 0;
			regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			break;
		case CtlStatusRXWait:
			LogEvent("USBChannel: NAK in CtlStatusRXWait", this);
			//reset packet information
			transfer.packet_data = (uint32_t*)transfer.data;
			transfer.packet_remaining = transfer.data_remaining;
			if (transfer.packet_remaining > transfer.max_packet_length)
				transfer.packet_remaining = transfer.max_packet_length;
			transfer.packet_handled = 0;
			//status is an empty DATA1 transfer
			regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			break;
		case CtlSetupTXResult:
			LogEvent("USBChannel: NAK in CtlStatusTXResult", this);
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			state = DisablingCtlSetupTX;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			transfer.packet_size = 0;
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			break;
		case CtlDataTXResult:
			LogEvent("USBChannel: NAK in CtlDataTXResult", this);
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			state = DisablingCtlDataTX;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			transfer.packet_size = 0;
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			break;
		case CtlStatusTXResult:
			LogEvent("USBChannel: NAK in CtlStatusTXResult", this);
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			state = DisablingCtlStatusTX;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			transfer.packet_size = 0;
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
		assert(transfer.state == Transfer::Result);
		usb::frameChannelTime -= transfer.transfer_time;
		transfer.state = Transfer::Idle;

		//transfer done.
		switch(state) {
		case TXResult:
			LogEvent("USBChannel: XFRC in TXResult", this);
			//done.
			//the channel disabled itself at this point.
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			regs->HCINTMSK = USB_CHAN_IDLE_INTMASK;
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			if (data_urb_remaining == 0) {
				//done with the URB.
				state = Unused;
				completeCurrentURB(0,usb::URB::Ack);
			} else
				state = TXWaitSOF;
			break;
		case CtlSetupTXResult:
			LogEvent("USBChannel: XFRC in CtlSetupTXResult", this);
			current_urb->endpoint->dataToggleOUT = true;
			//the channel disabled itself at this point.
			if (u->buffer_len > 0) {
				if (u->setup.bmRequestType & 0x80) {
					//that's IN
					if(continueBulkTransfer(true, false)) {
						state = CtlDataRXWait;
					} else {
						state = CtlDataRXWaitSOF;
					}
				} else {
					if(continueBulkTransfer(false, false)) {
						//that's IN
						state = CtlDataTXWait;
					} else {
						state = CtlDataTXWaitSOF;
					}
				}
			} else {
				if (u->setup.bmRequestType & 0x80) {
					if (current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
						state = CtlStatusTXWait;
						doOUTTransfer(transfer.data, 0, true);
					} else
						state = CtlStatusTXWaitSOF;
				} else {
					if (current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
						state = CtlStatusRXWait;
						doINTransfer(transfer.data, 0, true);
					} else
						state = CtlStatusRXWaitSOF;
				}
			}
			break;
		case CtlDataTXResult:
			LogEvent("USBChannel: XFRC in CtlDataTXResult", this);
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			if (data_urb_remaining == 0) {
				current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
				//the channel disabled itself at this point.
				if (current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
					state = CtlStatusRXWait;
					doINTransfer(transfer.data, 0, true);
				} else
					state = CtlStatusRXWaitSOF;
			} else
				state = CtlDataTXWaitSOF;
			break;
		case CtlDataRXWait:
			LogEvent("USBChannel: XFRC in CtlDataRXWait", this);
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			if (data_urb_remaining == 0) {
				//the channel disabled itself at this point.
				u->buffer_received = u->buffer_len - data_urb_remaining;
				if (current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
					state = CtlStatusTXWait;
					doOUTTransfer(transfer.data, 0, true);
				} else
					state = CtlStatusTXWaitSOF;
			} else
				state = CtlDataRXWaitSOF;
			break;
		case CtlStatusTXResult:
			LogEvent("USBChannel: XFRC in CtlStatusTXResult", this);
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			//fall through
		case CtlStatusRXWait:
			LogEvent("USBChannel: XFRC in CtlStatusRXWait", this);
			//the channel disabled itself at this point.
			state = Unused;
			completeCurrentURB(0,usb::URB::Ack);
			break;
		case RXWait:
			LogEvent("USBChannel: XFRC in RXWait", this);
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			//the channel disabled itself at this point.
			regs->HCINTMSK = USB_CHAN_IDLE_INTMASK;
			if (data_urb_remaining == 0) {
				u->buffer_received = u->buffer_len - data_urb_remaining;
				state = Unused;
				completeCurrentURB(0,usb::URB::Ack);
			} else
				state = RXWaitSOF;
			break;
		case TXWait: //failed for some reason. keep the chain going.
		case CtlSetupTXWait:
		case CtlDataTXWait:
		case CtlStatusTXWait:
			assert(0);
			LogEvent("USBChannel: XFRC in *TXWait", this);
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			state = Disabling;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			//cannot automatically recover from this, so communicate URB error
			u->buffer_received = u->buffer_len - data_urb_remaining;
			completeCurrentURB(1,usb::URB::TXErr);
			break;
		default: assert(0); break;
		}
	}
	if (hcint & OTG_HCINT_DTERR) {
		//must disable the channel when getting DTERR (stm32f4 34.17.4 Halting a channel)
		LogEvent("USBChannel: DTERR", this);
		hcint &= ~OTG_HCINT_DTERR;
		regs->HCINT = OTG_HCINT_DTERR;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCCHAR |= OTG_HCCHAR_CHDIS;
		regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
		switch (state) {
		case TXResult:
		case TXWait:
		case RXWait:
		case CtlDataTXWait:
		case CtlDataTXResult:
		case CtlDataRXWait:
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			break;
			transfer.data_handled = 0;
		default: break;
		}
		state = Disabling;
		assert(transfer.state != Transfer::Idle &&
			transfer.state != Transfer::Disabling);
		usb::frameChannelTime -= transfer.transfer_time;
		transfer.state = Transfer::Disabling;
		//cannot automatically recover from this, so communicate URB error
		u->buffer_received = u->buffer_len - data_urb_remaining;
		completeCurrentURB(1,usb::URB::DTErr);
	}
	if (hcint & OTG_HCINT_FRMOR) {
		hcint &= ~OTG_HCINT_FRMOR;//todo: this keeps happening, need to estimate transaction time before commiting one and check if it fits into the frame. also need to put the periodic urbs in a (priority-)queue instead of having them eat up channels that idle.
		LogEvent("USBChannel: FRMOR", this);
		//so, plan:
		// when an urb gets submitted:
		//  * conservative estimate of the time needed for a given urbs
		//  * if there is not enough time in the current frame, given all the active channels and urbs waiting in queues, put it into the
		//    queue be done. there is a queue for periodic and non-periodics.
		//  * if there is a free channel, directly occupy it. otherwise, put it in the queue for the current channel.
		// when a channels finishes work:
		//  * trigger the queue emptying mechanism, which then looks at an urb out of the periodic, then non-periodic queue. it checks if
		//    there is still enough time left to do the transaction, and if so occopys a channel
		// on start of frame:
		//  * the queue emptying mechanism is triggered for all channels.
		// tracking needed:
		//  * urbs need to gain a frame time estimate
		//  * usb needs to track the sum of all channels time use.
		// the "is time left" decision is based on the sum of all channels and current usb frame time from the device.
		assert(0);
	}
	if (hcint & OTG_HCINT_BBERR) {
		//must disable the channel when getting BBERR (stm32f4 34.17.4 Halting a channel)
		hcint &= ~OTG_HCINT_BBERR;
		LogEvent("USBChannel: BBERR", this);
		assert(0);
	}
	if (hcint & OTG_HCINT_TXERR) {
		//TXERR means the device failed to send data.
		hcint &= ~OTG_HCINT_TXERR;
		regs->HCINT = OTG_HCINT_TXERR;
		switch(state) {
		case CtlSetupTXResult:
			LogEvent("USBChannel: TXERR in CtlSetupTXResult", this);
			state = DisablingCtlSetupTX;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			break;
		case CtlDataTXResult:
			LogEvent("USBChannel: TXERR in CtlDataTXResult", this);
			state = DisablingCtlDataTX;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			break;
		case CtlDataRXWait:
			LogEvent("USBChannel: TXERR in CtlDataRXWait", this);
			state = DisablingCtlDataRX;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			break;
		case CtlStatusTXResult:
			LogEvent("USBChannel: TXERR in CtlStatusTXResult", this);
			state = DisablingCtlStatusTX;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			break;
		case CtlStatusRXWait:
			LogEvent("USBChannel: TXERR in CtlStatusRXWait", this);
			state = DisablingCtlStatusRX;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			break;
		case RXWait:
			LogEvent("USBChannel: TXERR in RXWait", this);
			state = Disabling;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			completeCurrentURB(1,usb::URB::TXErr);
			break;
		default: assert(0); break;
		}
		transfer.packet_size = 0;
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
		case RXWait:
			LogEvent("USBChannel: STALL in Ctl*RXWait/*TXResult or RXWait", this);
			regs->HCINT = OTG_HCINT_CHH;
			regs->HCCHAR |= OTG_HCCHAR_CHDIS;
			regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
			state = Disabling;
			assert(transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling);
			usb::frameChannelTime -= transfer.transfer_time;
			transfer.state = Transfer::Disabling;
			data_urb += transfer.data_handled;
			if (data_urb_remaining >= transfer.data_handled)
				data_urb_remaining -= transfer.data_handled;
			else
				data_urb_remaining = 0;
			transfer.data_handled = 0;
			u->buffer_received = u->buffer_len - data_urb_remaining;
			completeCurrentURB(1,usb::URB::Stall);
			break;
		default: assert(0); break;
		}

	}
	if (hcint & OTG_HCINT_CHH) {
		hcint &= ~OTG_HCINT_CHH;
		regs->HCINT = OTG_HCINT_CHH;
		regs->HCINTMSK = USB_CHAN_IDLE_INTMASK;
		assert(transfer.state == Transfer::Disabling);
		transfer.state = Transfer::Idle;
		LogEvent("USBChannel: Channel halted", this);
		switch(state) {
		case Disabling:
			state = Unused;
			assert(!current_urb);
			break;
		case DisablingCtlSetupTX:
			if (current_urb->setupTime() <= usb::frameTimeRemaining()) {
				state = CtlSetupTXWait;
				doSETUPTransfer();
			} else
				state = CtlSetupTXWaitSOF;
			break;
		case DisablingCtlDataTX:
			if (continueBulkTransfer(false, false))
				state = CtlDataTXWait;
			else
				state = CtlDataTXWaitSOF;
			break;
		case DisablingCtlDataRX:
			if (continueBulkTransfer(true, false))
				state = CtlDataRXWait;
			else
				state = CtlDataRXWaitSOF;
			break;
		case DisablingCtlStatusTX:
			if (current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
				state = CtlStatusTXWait;
				doOUTTransfer(transfer.data, 0, true);
			} else
				state = CtlStatusTXWaitSOF;
			break;
		case DisablingCtlStatusRX:
			if (current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
				state = CtlStatusRXWait;
				doINTransfer(transfer.data, 0, true);
			} else
				state = CtlStatusRXWaitSOF;
			break;
		case DisablingTX:
			if (continueBulkTransfer(false, false))
				state = TXWait;
			else
				state = TXWaitSOF;
			break;
		default: assert(0); break;
		}
	}
	{
		ISR_Guard g;
		if (state == Unused) {
			         otgh->HAINTMSK &= ~(1 << index);
			//and now look at the queue.
			usb::URB *u = usb::getNextURB();
			if (u)
				setupForURB(u);
			return;
		}
	}
	assert(hcint == 0);
}

void usb::Channel::SOF() {
	switch(state) {
	case CtlSetupTXWaitSOF:
		if (current_urb->setupTime() <= usb::frameTimeRemaining()) {
			LogEvent("USBChannel: SOF for CtlSetupTXWait", this);
			state = CtlSetupTXWait;
			doSETUPTransfer();
		} else
			LogEvent("USBChannel: SOF frame full for CtlSetupTXWait", this);
		break;
	case CtlDataRXWaitSOF:
		if (continueBulkTransfer(true, false)) {
			LogEvent("USBChannel: SOF for CtlDataRXWait", this);
			state = CtlDataRXWait;
		} else
			LogEvent("USBChannel: SOF frame full for CtlDataRXWait", this);
		break;
	case CtlDataTXWaitSOF:
		if (continueBulkTransfer(false, false)) {
			LogEvent("USBChannel: SOF for CtlDataTXWait", this);
			state = CtlDataTXWait;
		} else
			LogEvent("USBChannel: SOF frame full for CtlDataTXWait", this);
		break;
	case CtlStatusTXWaitSOF:
		if (current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
			LogEvent("USBChannel: SOF for CtlStatusTXWait", this);
			state = CtlStatusTXWait;
			doOUTTransfer(transfer.data, 0, true);
		} else
			LogEvent("USBChannel: SOF frame full for CtlStatusTXWait", this);
		break;
	case CtlStatusRXWaitSOF:
		if (current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
			LogEvent("USBChannel: SOF for CtlStatusRXWait", this);
			state = CtlStatusRXWait;
			doINTransfer(transfer.data, 0, true);
		} else
			LogEvent("USBChannel: SOF frame full for CtlStatusRXWait", this);
		break;
	case RXWaitSOF:
		if (continueBulkTransfer(true, false)) {
			LogEvent("USBChannel: SOF for RXWait", this);
			state = RXWait;
		} else
			LogEvent("USBChannel: SOF frame full for RXWait", this);
		break;
	case TXWaitSOF:
		if (continueBulkTransfer(false, false)) {
			LogEvent("USBChannel: SOF for TXWait", this);
			state = TXWait;
		} else
			LogEvent("USBChannel: SOF frame full for TXWait", this);
		break;
	case CtlSetupTXResult:
	case CtlSetupTXWait:
	case CtlDataRXWait:
	case Unused:
		break;
	default:
//		LogEvent("USBChannel: Unhandled SOF while not Unused", this);
//		assert(0);
		break;
	}
}

void usb::Channel::doSETUPTransfer() {
	LogEvent("USBChannel: doSETUPTransfer", this);
	assert(transfer.state == Transfer::Idle);
	assert((regs->HCCHAR & OTG_HCCHAR_CHENA) == 0);

	transfer.data_handled = 0;
	transfer.data_remaining = 8;
	transfer.data = (uint8_t*)&current_urb->setup;

	transfer.packet_data = (uint32_t*)transfer.data;
	transfer.packet_size = 0;
	transfer.packet_handled = 0;
	transfer.packet_remaining = transfer.data_remaining;
	if (transfer.packet_remaining > transfer.max_packet_length)
		transfer.packet_remaining = transfer.max_packet_length;

	transfer.packet_count = 1;

	transfer.transfer_time = current_urb->setupTime();
	usb::frameChannelTime += transfer.transfer_time;
	transfer.state = Transfer::Wait;

	uint32_t hcchar = (current_urb->endpoint->device.eaddress << 22)
		| ((current_urb->endpoint->address & 0xf) << 11)
		| transfer.max_packet_length;
	if ((otgh->HFNUM & 1) == 0)
		hcchar |= OTG_HCCHAR_ODDFRM;
	uint32_t hctsiz = 0;
	hctsiz = 0x60000000;
	if (current_urb->endpoint->device.speed == usb::Speed::Low)
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

	   otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM;
}

void usb::Channel::doINTransfer(void *data, size_t xfrsiz, bool lastTransfer) {
	LogEvent("USBChannel: doINTransfer", this);
	assert(transfer.state == Transfer::Idle);
	assert((regs->HCCHAR & OTG_HCCHAR_CHENA) == 0);

	transfer.data_handled = 0;
	transfer.data_remaining = xfrsiz;
	transfer.data = (uint8_t*)data;

	transfer.packet_data = (uint32_t*)transfer.data;
	transfer.packet_size = 0;
	transfer.packet_handled = 0;
	transfer.packet_remaining = transfer.data_remaining;
	if (transfer.packet_remaining > transfer.max_packet_length)
		transfer.packet_remaining = transfer.max_packet_length;

	//if it is evenly divisable, we need to add one more (empty) packet.
	//if it is not evenly divisable, we need to add one to round up.
	transfer.packet_count = xfrsiz / transfer.max_packet_length;
	if (lastTransfer) {
		//if the final size is evenly divisable, we need to add one more (empty) packet.
		//if the final size is not evenly divisable, we need to add one to round up.
		//if this is not the last transfer, xfrsiz is evenly divisable, but we don't
		//want to add another packet.
		if (xfrsiz == transfer.packet_count * transfer.max_packet_length) {
			switch(current_urb->endpoint->type) {
				case usb::Endpoint::Control:
				case usb::Endpoint::Bulk:
					transfer.packet_count++;
					break;
				case usb::Endpoint::ISO:
				case usb::Endpoint::IRQ:
					break;
			}
		} else
			transfer.packet_count++;
	} else {
		assert(xfrsiz == transfer.packet_count * transfer.max_packet_length);
	}

	//adjust the size seen by the USB controller. must be multiple of max_packet_length
	xfrsiz = transfer.packet_count * transfer.max_packet_length;

	transfer.transfer_time = current_urb->dataTime(transfer.max_packet_length) * transfer.packet_count;
	usb::frameChannelTime += transfer.transfer_time;
	transfer.state = Transfer::Wait;

	uint32_t hcchar = (current_urb->endpoint->device.eaddress << 22)
		| ((current_urb->endpoint->address & 0xf) << 11)
		| transfer.max_packet_length;
	if ((otgh->HFNUM & 1) == 0)
		hcchar |= OTG_HCCHAR_ODDFRM;
	uint32_t hctsiz = 0;

	switch(current_urb->endpoint->type) {
	case usb::Endpoint::Control: hcchar |= 0; break;
	case usb::Endpoint::Bulk: hcchar |= 0x80000; break;
	case usb::Endpoint::ISO: hcchar |= 0x40000; break;
	case usb::Endpoint::IRQ: hcchar |= 0xc0000; break;
	}
	if (current_urb->buffer_len <= transfer.max_packet_length) {
		hcchar |= 0x100000;
	} else if (current_urb->buffer_len <= transfer.max_packet_length*2) {
		hcchar |= 0x200000;
	} else {
		hcchar |= 0x300000;
	}
	hctsiz = current_urb->endpoint->dataToggleIN?0x40000000:0;
	if (current_urb->endpoint->device.speed == usb::Speed::Low)
		hcchar |= 0x20000;

	hctsiz |= transfer.packet_count << 19;
	hcchar |= 0x8000;

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

void usb::Channel::doOUTTransfer(void *data, size_t xfrsiz, bool lastTransfer) {
	LogEvent("USBChannel: doOUTTransfer", this);
	assert(transfer.state == Transfer::Idle);
	assert((regs->HCCHAR & OTG_HCCHAR_CHENA) == 0);

	transfer.data_handled = 0;
	transfer.data_remaining = xfrsiz;
	transfer.data = (uint8_t*)data;

	transfer.packet_data = (uint32_t*)transfer.data;
	transfer.packet_size = 0;
	transfer.packet_handled = 0;
	transfer.packet_remaining = transfer.data_remaining;
	if (transfer.packet_remaining > transfer.max_packet_length)
		transfer.packet_remaining = transfer.max_packet_length;

	transfer.packet_count = xfrsiz / transfer.max_packet_length;
	if (lastTransfer) {
		//if the final size is evenly divisable, we need to add one more (empty) packet.
		//if the final size is not evenly divisable, we need to add one to round up.
		//if this is not the last transfer, xfrsiz is evenly divisable, but we don't
		//want to add another packet.
		transfer.packet_count++;
	} else {
		assert(xfrsiz == transfer.packet_count * transfer.max_packet_length);
	}

	transfer.transfer_time = 0;
	if (transfer.packet_count > 1) {
		transfer.transfer_time += current_urb->dataTime(transfer.max_packet_length) *
		(transfer.packet_count-1);
		transfer.transfer_time += current_urb->dataTime(xfrsiz -
		transfer.max_packet_length * (transfer.packet_count-1));
	} else
		transfer.transfer_time += current_urb->dataTime(xfrsiz);

	usb::frameChannelTime += transfer.transfer_time;
	transfer.state = Transfer::Wait;

	uint32_t hcchar = (current_urb->endpoint->device.eaddress << 22)
		| ((current_urb->endpoint->address & 0xf) << 11)
		| transfer.max_packet_length;
	if ((otgh->HFNUM & 1) == 0)
		hcchar |= OTG_HCCHAR_ODDFRM;
	uint32_t hctsiz = 0;

	switch(current_urb->endpoint->type) {
	case usb::Endpoint::Control: hcchar |= 0; break;
	case usb::Endpoint::Bulk: hcchar |= 0x80000; break;
	case usb::Endpoint::ISO: hcchar |= 0x40000; break;
	case usb::Endpoint::IRQ: hcchar |= 0xc0000; break;
	}
	hctsiz = current_urb->endpoint->dataToggleOUT?0x40000000:0;
	if (current_urb->endpoint->device.speed == usb::Speed::Low)
		hcchar |= 0x20000;

	hctsiz |= transfer.packet_count << 19;
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
	case usb::Endpoint::Control:
	case usb::Endpoint::Bulk:
		      otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM; break;
	case usb::Endpoint::ISO:
	case usb::Endpoint::IRQ:
		      otgc->GINTMSK |= OTG_GINTMSK_PTXFEM; break;
	}

}

bool usb::Channel::continueBulkTransfer(bool deviceToHost, bool force) {
	LogEvent("USBChannel: continueBulkTransfer", this);
	unsigned int timeRemain = usb::frameTimeRemaining();
	unsigned int packetTime = current_urb->dataTime(transfer.max_packet_length);
	unsigned int pktcnt = timeRemain / packetTime;
	//if force is true, we are guaranteed(more or less) to be able to push at least on packet.
	//otherwise, the URB would not have been selected by USB_getNextURB(coming from setupForURB).
	if (pktcnt == 0) {
		if (force)
			pktcnt = 1;
		else
			return false;
	}

	unsigned int firstTransferSize = pktcnt * transfer.max_packet_length;
	if (firstTransferSize < data_urb_remaining && data_urb_remaining >
		transfer.max_packet_length) {
		if (deviceToHost)
			doINTransfer(data_urb, firstTransferSize, false);
		else
			doOUTTransfer(data_urb, firstTransferSize, false);
	} else {
		if (deviceToHost)
			doINTransfer(data_urb, data_urb_remaining, true);
		else
			doOUTTransfer(data_urb, data_urb_remaining, true);
	}
	return true;
}

void usb::Channel::completeCurrentURB(int resultcode, URB::USBResult usbresult) {
	URB *u = current_urb;
	current_urb = NULL;
	u->result = usbresult;
	if(u->slot)
		addDeferredWork(sigc::bind(u->slot,resultcode));
}

void usb::Channel::setupForURB( usb::URB *u) {
	LogEvent("USBChannel: setupForURB", this);
	//okay, now we are talking. we'll have to remember how much we
	//wrote already, and the odd/even data packet numbers.
	//(SETUP packets reset that one)
	assert(u->endpoint);
	assert(state == Unused);
	assert(transfer.state == Transfer::Idle);
	current_urb = u;
	data_urb = (char*)u->buffer;
	data_urb_remaining = u->buffer_len;
	transfer.max_packet_length = u->endpoint->max_packet_length;
	switch(u->endpoint->type) {
	case usb::Endpoint::Control:
		state = CtlSetupTXWait;
		doSETUPTransfer();
		break;
	case usb::Endpoint::Bulk:
		if (current_urb->endpoint->direction == usb::Endpoint::DeviceToHost) {
			state = RXWait;
			continueBulkTransfer(true, true);
		} else {
			state = TXWait;
			continueBulkTransfer(false, true);
		}
		break;
	case usb::Endpoint::ISO:
	case usb::Endpoint::IRQ:
		if (u->endpoint->direction == usb::Endpoint::DeviceToHost) {
			state = RXWait;
			doINTransfer(data_urb, data_urb_remaining, true);
		} else {
			state = TXWait;
			doOUTTransfer(data_urb, data_urb_remaining, true);
		}
		break;
	default:
		assert(0);
		break;
	}
	   otgh->HAINTMSK |= 1 << index;
}

void usb::Channel::retireURB( usb::URB *u) {
	LogEvent("USBChannel: retireURB", this);
	if (current_urb == u) {
		if (regs->HCCHAR & OTG_HCCHAR_CHENA) {
			assert(transfer.state != Transfer::Idle);
			if (transfer.state != Transfer::Disabling) {
				usb::frameChannelTime -= transfer.transfer_time;
				regs->HCINT = OTG_HCINT_CHH;
				regs->HCCHAR |= OTG_HCCHAR_CHDIS;
				regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
				state = Disabling;
				transfer.state = Transfer::Disabling;
			}
		} else {
			if (transfer.state != Transfer::Idle &&
				transfer.state != Transfer::Disabling) {
				usb::frameChannelTime -= transfer.transfer_time;
			}
			transfer.state = Transfer::Idle;
			state = Unused;
		}
		current_urb = NULL;
	}
}

void usb::Channel::killEndpoint( usb::Endpoint *endpoint) {
	if (!current_urb)
		return;
	if (current_urb->endpoint == endpoint)
		retireURB(current_urb);
}

