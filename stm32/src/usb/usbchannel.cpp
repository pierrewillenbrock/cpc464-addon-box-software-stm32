
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
	if (!(state == TX))
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
	regs->HCINTMSK |= OTG_HCINTMSK_ACKM;
	transfer.state = Transfer::Result;
}

void usb::Channel::NPTXPossible() {
	if (!(state == TX || state == CtlSetupTX ||
	      state == CtlDataTX || state == CtlStatusTX))
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
	regs->HCINTMSK |= OTG_HCINTMSK_ACKM;
	transfer.state = Transfer::Result;
}

void usb::Channel::RXData(unsigned bcnt, unsigned dpid) {
	if (!(state == RX || state == CtlDataRX ||
	      state == CtlStatusRX))
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

void usb::Channel::cancelTransfer() {
	assert(transfer.state != Transfer::Idle &&
	       transfer.state != Transfer::Disabling);
	usb::frameChannelTime -= transfer.transfer_time;
	transfer.state = Transfer::Disabling;
	transfer.packet_size = 0;
	regs->HCINT = OTG_HCINT_CHH;
	regs->HCCHAR |= OTG_HCCHAR_CHDIS;
	regs->HCINTMSK = USB_CHAN_DISABLE_INTMASK;
}

void usb::Channel::update_data_urb_from_transfer_data_handled() {
	data_urb += transfer.data_handled;
	if(data_urb_remaining >= transfer.data_handled)
		data_urb_remaining -= transfer.data_handled;
	else
		data_urb_remaining = 0;
	transfer.data_handled = 0;
}

void usb::Channel::setupPacket() {
	transfer.packet_handled = 0;
	transfer.packet_remaining = transfer.data_remaining;
	if(transfer.packet_remaining > transfer.max_packet_length)
		transfer.packet_remaining = transfer.max_packet_length;
	transfer.packet_data = (uint32_t *) transfer.data;
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
		if((hcint & OTG_HCINT_XFRC) &&
		                (state == TX || state == CtlSetupTX ||
		                 state == CtlDataTX || state == CtlDataRX ||
		                 state == CtlStatusTX || state == CtlStatusRX ||
		                 state == RX) && (transfer.state == Transfer::Result)) {
		} else if((hcint & OTG_HCINT_ACK) &&
		                (state == TX || state == CtlSetupTX ||
		                 state == CtlDataTX || state == CtlDataRX ||
		                 state == CtlStatusTX || state == CtlStatusRX ||
		                 state == RX) && (transfer.state == Transfer::Result)) {
		} else if((regs->HCINT & OTG_HCINT_CHH) &&
		                (state == Disabling ||
		                 state == TX ||
		                 state == CtlSetupTX ||
		                 state == CtlDataTX ||
		                 state == CtlDataRX ||
		                 state == CtlStatusTX ||
		                 state == CtlStatusRX) &&
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
		assert(transfer.state == Transfer::Result);
		transfer.data += transfer.packet_handled;
		if(transfer.data_remaining > transfer.packet_handled) {
			transfer.data_handled += transfer.packet_handled;
			transfer.data_remaining -= transfer.packet_handled;
		} else {
			transfer.data_handled += transfer.data_remaining;
			transfer.data_remaining = 0;
		}
		transfer.packet_handled = 0;
		transfer.packet_count--;
		switch(state) {
		case TX:
		case CtlSetupTX:
		case CtlStatusTX:
		case CtlDataTX:
			LogEvent("USBChannel: ACK in *TXResult", this);
			//this is used for all TX transmissions.
			if (transfer.packet_count > 0) {
				//setup for next packet
				setupPacket();

				transfer.state = Transfer::Wait;
				switch(current_urb->endpoint->type) {
				case usb::Endpoint::Control:
				case usb::Endpoint::Bulk:
					otgc->GINTMSK |= OTG_GINTMSK_NPTXFEM;
					break;
				case usb::Endpoint::ISO:
				case usb::Endpoint::IRQ:
					otgc->GINTMSK |= OTG_GINTMSK_PTXFEM;
					break;
				}
			} else {
				//done with this transfer
				//update data pointer if needed, or do this during XFRC?
				if (state == TX || state == CtlDataTX) {
					update_data_urb_from_transfer_data_handled();
					//rest is handled during XFRC
				}
				regs->HCINTMSK |= OTG_HCINTMSK_XFRCM;
			}
			break;
		case RX:
		case CtlDataRX:
		case CtlStatusRX:
			LogEvent("USBChannel: ACK in *RXResult", this);
			//this is used for all RX transmissions.
			if (transfer.packet_count > 0 && transfer.packet_remaining == 0) {
				//setup for next packet
				setupPacket();

				transfer.state = Transfer::Wait;
			} else {
				//done with this transfer
				//rest is handled during XFRC
				regs->HCINTMSK |= OTG_HCINTMSK_XFRCM;
			}
			regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			break;
		default: assert(0); break;
		}
		regs->HCINTMSK &= ~OTG_HCINTMSK_ACKM;
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
		case RX:
			LogEvent("USBChannel: NAK in RXWait", this);
			assert(transfer.state == Transfer::Result ||
				transfer.state == Transfer::Wait);
			//reset packet information
			setupPacket();

			//only BULK and IRQ(and Control below) can generate NAK
			if (current_urb->endpoint->type == usb::Endpoint::IRQ) {
				//we need to shutdown the channel here.
				state = Disabling;
				cancelTransfer();
				current_urb = NULL;
			} else {
				//bulk read just keeps waiting?
				regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			}
			break;
		case CtlDataRX:
			LogEvent("USBChannel: NAK in CtlDataRXWait", this);
			assert(transfer.state == Transfer::Result ||
				transfer.state == Transfer::Wait);
			//reset packet information
			setupPacket();

			regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			break;
		case CtlStatusRX:
			LogEvent("USBChannel: NAK in CtlStatusRXWait", this);
			assert(transfer.state == Transfer::Result ||
				transfer.state == Transfer::Wait);
			//reset packet information
			setupPacket();

			//status is an empty DATA1 transfer
			regs->HCCHAR = regs->HCCHAR;//trigger advance of channel state
			break;
		case CtlSetupTX:
			LogEvent("USBChannel: NAK in CtlStatusTXResult", this);
			assert(transfer.state == Transfer::Result);
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			cancelTransfer();
			break;
		case CtlDataTX:
			LogEvent("USBChannel: NAK in CtlDataTXResult", this);
			assert(transfer.state == Transfer::Result);
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			update_data_urb_from_transfer_data_handled();
			cancelTransfer();
			break;
		case CtlStatusTX:
			LogEvent("USBChannel: NAK in CtlStatusTXResult", this);
			assert(transfer.state == Transfer::Result);
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			cancelTransfer();
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
		case TX:
			LogEvent("USBChannel: XFRC in TXResult", this);
			//done.
			//the channel disabled itself at this point.
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			regs->HCINTMSK = USB_CHAN_IDLE_INTMASK;
			update_data_urb_from_transfer_data_handled();
			if (data_urb_remaining == 0) {
				//done with the URB.
				state = Unused;
				completeCurrentURB(0,usb::URB::Ack);
			}
			break;
		case CtlSetupTX:
			LogEvent("USBChannel: XFRC in CtlSetupTXResult", this);
			current_urb->endpoint->dataToggleOUT = true;
			//the channel disabled itself at this point.
			if (u->buffer_len > 0) {
				if (u->setup.bmRequestType & 0x80) {
					//that's IN
					state = CtlDataRX;
					continueBulkTransfer(true, false);
				} else {
					//that's IN
					state = CtlDataTX;
					continueBulkTransfer(false, false);
				}
			} else {
				if (u->setup.bmRequestType & 0x80) {
					state = CtlStatusTX;
					if (current_urb->dataTime(0) <= usb::frameTimeRemaining())
						doOUTTransfer(transfer.data, 0, true);
				} else {
					state = CtlStatusRX;
					if (current_urb->dataTime(0) <= usb::frameTimeRemaining())
						doINTransfer(transfer.data, 0, true);
				}
			}
			break;
		case CtlDataTX:
			LogEvent("USBChannel: XFRC in CtlDataTXResult", this);
			update_data_urb_from_transfer_data_handled();
			if (data_urb_remaining == 0) {
				current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
				//the channel disabled itself at this point.
				state = CtlStatusRX;
				if (current_urb->dataTime(0) <= usb::frameTimeRemaining())
					doINTransfer(transfer.data, 0, true);
			}
			break;
		case CtlDataRX:
			LogEvent("USBChannel: XFRC in CtlDataRXWait", this);
			update_data_urb_from_transfer_data_handled();
			if (data_urb_remaining == 0) {
				//the channel disabled itself at this point.
				u->buffer_received = u->buffer_len - data_urb_remaining;
				state = CtlStatusTX;
				if (current_urb->dataTime(0) <= usb::frameTimeRemaining())
					doOUTTransfer(transfer.data, 0, true);
			}
			break;
		case CtlStatusTX:
			LogEvent("USBChannel: XFRC in CtlStatusTXResult", this);
			current_urb->endpoint->dataToggleOUT =
				((regs->HCTSIZ & 0x60000000) >> 29) != 0x2;
			//fall through
		case CtlStatusRX:
			LogEvent("USBChannel: XFRC in CtlStatusRXWait", this);
			//the channel disabled itself at this point.
			state = Unused;
			completeCurrentURB(0,usb::URB::Ack);
			break;
		case RX:
			LogEvent("USBChannel: XFRC in RXWait", this);
			update_data_urb_from_transfer_data_handled();
			//the channel disabled itself at this point.
			regs->HCINTMSK = USB_CHAN_IDLE_INTMASK;
			if (data_urb_remaining == 0) {
				u->buffer_received = u->buffer_len - data_urb_remaining;
				state = Unused;
				completeCurrentURB(0,usb::URB::Ack);
			}
			break;
		default: assert(0); break;
		}
	}
	if (hcint & OTG_HCINT_DTERR) {
		//must disable the channel when getting DTERR (stm32f4 34.17.4 Halting a channel)
		LogEvent("USBChannel: DTERR", this);
		hcint &= ~OTG_HCINT_DTERR;
		regs->HCINT = OTG_HCINT_DTERR;
		switch (state) {
		case TX:
		case RX:
		case CtlDataTX:
		case CtlDataRX:
			assert(transfer.state == Transfer::Result ||
				transfer.state == Transfer::Wait);
			update_data_urb_from_transfer_data_handled();
			break;
		default: break;
		}
		state = Disabling;
		cancelTransfer();
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
		//
		//in general, this only seems to be generated for periodic data.
		//the following is speculation:
		//periodic data does not get output during "normal" frame
		//handling, but gets queued until the next SOF. then, the USB
		//host tries to output all pending periodic packets. If it
		//cannot send all periodic data, it will emit FRMOR on the
		//respective channels.
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
		case CtlSetupTX:
			LogEvent("USBChannel: TXERR in CtlSetupTXResult", this);
			assert(transfer.state == Transfer::Result);
			break;
		case CtlDataTX:
			LogEvent("USBChannel: TXERR in CtlDataTXResult", this);
			assert(transfer.state == Transfer::Result);
			update_data_urb_from_transfer_data_handled();
			break;
		case CtlDataRX:
			LogEvent("USBChannel: TXERR in CtlDataRXWait", this);
			assert(transfer.state == Transfer::Result ||
				transfer.state == Transfer::Wait);
			update_data_urb_from_transfer_data_handled();
			break;
		case CtlStatusTX:
			LogEvent("USBChannel: TXERR in CtlStatusTXResult", this);
			assert(transfer.state == Transfer::Result);
			break;
		case CtlStatusRX:
			LogEvent("USBChannel: TXERR in CtlStatusRXWait", this);
			assert(transfer.state == Transfer::Result ||
				transfer.state == Transfer::Wait);
			break;
		case RX:
			LogEvent("USBChannel: TXERR in RXWait", this);
			assert(transfer.state == Transfer::Result ||
				transfer.state == Transfer::Wait);
			state = Disabling;
			completeCurrentURB(1,usb::URB::TXErr);
			break;
		default: assert(0); break;
		}
		cancelTransfer();
	}
	if (hcint & OTG_HCINT_STALL) {
		//must disable the channel when getting STALL (stm32f4 34.17.4 Halting a channel)
		//STALL during any part of a Control transfer means the device
		//is unhappy with it and cannot proceed.
		hcint &= ~OTG_HCINT_STALL;
		regs->HCINT = OTG_HCINT_STALL;
		switch(state) {
		case CtlSetupTX:
		case CtlDataTX:
		case CtlStatusTX:
			assert(transfer.state == Transfer::Result);
			/* fall through */
		case CtlDataRX:
		case CtlStatusRX:
		case RX:
			LogEvent("USBChannel: STALL in Ctl*RXWait/*TXResult or RXWait", this);
			assert(transfer.state == Transfer::Result ||
				transfer.state == Transfer::Wait);
			state = Disabling;
			cancelTransfer();
			update_data_urb_from_transfer_data_handled();
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
		case CtlSetupTX:
			if (current_urb->setupTime() <= usb::frameTimeRemaining())
				doSETUPTransfer();
			break;
		case CtlDataTX:
		case TX:
			continueBulkTransfer(false, false);
			break;
		case CtlDataRX:
		case RX:
			continueBulkTransfer(true, false);
			break;
		case CtlStatusTX:
			if (current_urb->dataTime(0) <= usb::frameTimeRemaining())
				doOUTTransfer(transfer.data, 0, true);
			break;
		case CtlStatusRX:
			if (current_urb->dataTime(0) <= usb::frameTimeRemaining())
				doINTransfer(transfer.data, 0, true);
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
	if(transfer.state == Transfer::Idle) {
		switch(state) {
		case CtlSetupTX:
			if(current_urb->setupTime() <= usb::frameTimeRemaining()) {
				LogEvent("USBChannel: SOF for CtlSetupTXWait", this);
				doSETUPTransfer();
			} else
				LogEvent("USBChannel: SOF frame full for CtlSetupTXWait", this);
			break;
		case CtlDataRX:
			if(continueBulkTransfer(true, false))
				LogEvent("USBChannel: SOF for CtlDataRX", this);
			else
				LogEvent("USBChannel: SOF frame full for CtlDataRX", this);
			break;
		case CtlDataTX:
			if(continueBulkTransfer(false, false))
				LogEvent("USBChannel: SOF for CtlDataTXWait", this);
			else
				LogEvent("USBChannel: SOF frame full for CtlDataTXWait", this);
			break;
		case CtlStatusTX:
			if(current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
				LogEvent("USBChannel: SOF for CtlStatusTXWait", this);
				doOUTTransfer(transfer.data, 0, true);
			} else
				LogEvent("USBChannel: SOF frame full for CtlStatusTXWait", this);
			break;
		case CtlStatusRX:
			if(current_urb->dataTime(0) <= usb::frameTimeRemaining()) {
				LogEvent("USBChannel: SOF for CtlStatusRXWait", this);
				doINTransfer(transfer.data, 0, true);
			} else
				LogEvent("USBChannel: SOF frame full for CtlStatusRXWait", this);
			break;
		case RX:
			if(continueBulkTransfer(true, false))
				LogEvent("USBChannel: SOF for RXWait", this);
			else
				LogEvent("USBChannel: SOF frame full for RXWait", this);
			break;
		case TX:
			if(continueBulkTransfer(false, false))
				LogEvent("USBChannel: SOF for TXWait", this);
			else
				LogEvent("USBChannel: SOF frame full for TXWait", this);
			break;
		default:
//			LogEvent("USBChannel: Unhandled SOF while not Unused", this);
//			assert(0);
			break;
		}
	}
}

void usb::Channel::doSETUPTransfer() {
	LogEvent("USBChannel: doSETUPTransfer", this);
	assert(transfer.state == Transfer::Idle);
	assert((regs->HCCHAR & OTG_HCCHAR_CHENA) == 0);

	transfer.data_handled = 0;
	transfer.data_remaining = 8;
	transfer.data = (uint8_t*)&current_urb->setup;

	transfer.packet_size = 0;
	setupPacket();

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

	transfer.packet_size = 0;
	setupPacket();

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

	transfer.packet_size = 0;
	setupPacket();

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
		state = CtlSetupTX;
		doSETUPTransfer();
		break;
	case usb::Endpoint::Bulk:
		if (current_urb->endpoint->direction == usb::Endpoint::DeviceToHost) {
			state = RX;
			continueBulkTransfer(true, true);
		} else {
			state = TX;
			continueBulkTransfer(false, true);
		}
		break;
	case usb::Endpoint::ISO:
	case usb::Endpoint::IRQ:
		if (u->endpoint->direction == usb::Endpoint::DeviceToHost) {
			state = RX;
			doINTransfer(data_urb, data_urb_remaining, true);
		} else {
			state = TX;
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
			if (transfer.state != Transfer::Disabling) {
				cancelTransfer();
				state = Disabling;
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

