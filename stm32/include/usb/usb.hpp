
#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <refcounted.hpp>

class USBDevice;
struct USBEndpoint;

struct URB {
	RefPtr<USBEndpoint> endpoint;
	struct {  // for Control transactions
		uint8_t bmRequestType;
		uint8_t bRequest;
		uint16_t wValue;
		uint16_t wIndex;
		uint16_t wLength;
	} setup;
	unsigned pollingInterval;//for IRQ and ISO transactions
	void *buffer;   //for all transactions
	size_t buffer_len;
	size_t buffer_received;
	enum { Ack, Nak, Stall, Nyet, TXErr, DTErr } result; //for Bulk, Control and IRQ transactions
	void *userpriv;
	void (*completion)(int result, URB *u);
	/** \brief Time for first packet if Bulk or Control, for all packets otherwise
	 *
	 * Calculate the time required for processing the packets that need to happen in this frame
	 * \returns Number of 48MHz PHY Cycles required to process the first packet of this URB
	 */
	unsigned int thisFrameTime() const;
	/** \brief Calculate the time required for processing this URB
	 * \returns Number of 48MHz PHY Cycles required to process this URB
	 */
	unsigned int allPacketTime() const;
	/** \brief Calculate the time for a SETUP packet
	 * \returns Number of Bits required
	 */
	unsigned int setupBits() const;
	/** \brief Calculate the time for a STATUS packet
	 * \returns Number of Bits required
	 */
	unsigned int statusBits() const;
	/** \brief Calculate the time for a DATA packet
	 * \param length Number of bytes in the packet
	 * \returns Number of 48MHz PHY Cycles required
	 */
	unsigned int dataBits(unsigned int length) const;
	/** \brief Calculate the time for a SETUP packet
	 * \returns Number of Bits required
	 */
	unsigned int setupTime() const;
	/** \brief Calculate the time for a STATUS packet
	 * \returns Number of 48MHz PHY Cycles required
	 */
	unsigned int statusTime() const;
	/** \brief Calculate the time for a DATA packet
	 * \param length Number of bytes in the packet
	 * \returns Number of 48MHz PHY Cycles required
	 */
	unsigned int dataTime(unsigned int length) const;
};

class USBDriver {
public:
	virtual bool probe(RefPtr<USBDevice> device) = 0;
};

class USBDriverDevice {
public:
	//called once the interface is ready for use
	virtual void interfaceClaimed(uint8_t /*interfaceNumber*/, uint8_t /*alternateSetting*/) {};
	virtual void deviceClaimed() {};
	//called only for the driver that claimed it. the driver is supposed to
	//drop all references to the usbdevice.
	virtual void disconnected(RefPtr<USBDevice> device) = 0;
};

void USB_Setup();
void USB_submitURB(struct URB *u);
void USB_retireURB(struct URB *u);
void USB_registerDriver(USBDriver *driver);
