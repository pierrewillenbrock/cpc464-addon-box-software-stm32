
#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <usb/usb.hpp>

struct OTG_Host_Channel_TypeDef;

namespace usb {
	struct Channel {
	private:
		enum State { Unused,
			TXWait, TXResult, DisablingTX, TXWaitSOF,
			RXWait, RXWaitSOF,
			CtlSetupTXWait, CtlSetupTXResult, DisablingCtlSetupTX, CtlSetupTXWaitSOF,
			CtlDataTXWait, CtlDataTXResult, DisablingCtlDataTX, CtlDataTXWaitSOF,
			CtlDataRXWait, DisablingCtlDataRX, CtlDataRXWaitSOF,
			CtlStatusTXWait, CtlStatusTXResult, DisablingCtlStatusTX, CtlStatusTXWaitSOF,
			CtlStatusRXWait, DisablingCtlStatusRX, CtlStatusRXWaitSOF,
			Disabling
		};
		enum State state;
		struct Transfer {
			uint32_t *packet_data;
			size_t packet_size; ///<counted in uint32_t units
			size_t packet_remaining; ///<counted in uint8_t units
			size_t packet_handled; ///<counted in uint8_t units
			uint8_t *data;
			size_t data_remaining; ///<counted in uint8_t units
			size_t data_handled; ///<counted in uint8_t units
			size_t packet_count;
			unsigned int transfer_time; ///<in number of 48MHz phy cycles
			size_t max_packet_length; ///<counted in uint8_t units
			enum {
				Idle, Wait, Acked, Result, Disabling
			} state;
			Transfer() : state(Idle) {}
		};
		Transfer transfer;
		OTG_Host_Channel_TypeDef *regs;
		volatile uint32_t *channeldata;
		URB *current_urb;
		char *data_urb;
		size_t data_urb_remaining; //data we still need to exchange for this urb counted in uint8_t units
		unsigned const index;
		void doSETUPTransfer();
		void doINTransfer(void *data, size_t xfrsiz, bool lastTransfer);
		void doOUTTransfer(void *data, size_t xfrsiz, bool lastTransfer);
		/** \brief resumes a bulk transfer or a data phase of a control transfer
		 *
		 * \param deviceToHost  Direction of the transfer
		 * \param force  If true, forces at least one packet. In that case, it always returns true.
		 * \returns  true if at least one packet has been scheduled.
		 */
		bool continueBulkTransfer(bool deviceToHost, bool force);
		void finishReadTransfer();
		void completeCurrentURB(int resultcode, URB::USBResult usbresult);
		void cancelTransfer();
	public:
		Channel (unsigned index);
		void setupForURB( URB *u);
		void retireURB( URB *u);
		void PTXPossible();
		void NPTXPossible();
		void RXData(unsigned bcnt, unsigned dpid);
		void INT();
		void SOF();
		bool isUnused() { return state == Unused; }
		void killEndpoint( Endpoint *endpoint);
	};

}

