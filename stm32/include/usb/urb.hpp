
#pragma once

#include <usb/usbdevice.hpp>
#include <usb/usbendpoint.hpp>

//token packets: SYNC(with SOP)(8) PID(4+4) address(8) endpoint-address(4)  CRC(5) 175ns EOP (2.1 bit times)
//data packets: SYNC(with SOP)(8) PID(4+4) data(*)  CRC(16) 175ns EOP (2.1 bit times)
//handshake packets:  SYNC(with SOP)(8) PID(4+4) 175ns EOP (2.1 bit times)
//ctl in transaction: token(SETUP) DATA(8) handshake(ACK)
//                    token(IN) DATA handshake(ACK)    for as many as needed by maxpacketsize and data size
//                    token(OUT) DATA(0) handshake(ACK)
//ctl out transaction: token(SETUP) DATA(8) handshake(ACK)
//                     token(OUT) DATA handshake(ACK)    for as many as needed by maxpacketsize and data size
//                     token(IN) DATA(0) handshake(ACK)
//bulk in transaction: token(IN) DATA handshake(ACK)    for as many as needed by maxpacketsize and data size
//bulk out transaction: token(OUT) DATA handshake(ACK)    for as many as needed by maxpacketsize and data size
//irq in transaction: token(IN) DATA handshake(ACK)    for as many as needed by maxpacketsize and data size
//irq out transaction: token(OUT) DATA handshake(ACK)    for as many as needed by maxpacketsize and data size
//iso in transaction: token(IN) DATA    for as many as needed by maxpacketsize and data size
//iso out transaction: token(OUT) DATA    for as many as needed by maxpacketsize and data size
//bit stuffing leads to 7/6 time use, but we can assume it only ever hits addresses, data and crcs.

//at this point, i'd assume c-code is out.
unsigned int usb::URB::thisFrameTime() const {
	unsigned int mult = endpoint->device.speed == usb::Speed::Full?4:32;
	unsigned int time_required = 0;
	unsigned int len = buffer_len;
	if (endpoint->type == usb::Endpoint::Control) {
		//apart from the data phase, Control transfers also have SETUP and STATUS.
		time_required = setupBits() * mult;
	} else if (endpoint->type == usb::Endpoint::Bulk) {
		if (len > endpoint->max_packet_length)
			len = endpoint->max_packet_length;
		time_required = dataBits(len) * mult;
	} else {
		time_required = allPacketTime();
	}
	return time_required;
}

unsigned int usb::URB::allPacketTime() const {
	unsigned int mult = endpoint->device.speed == usb::Speed::Full?4:32;
	unsigned int time_required = 0;
	unsigned int packet_count = (buffer_len + endpoint->max_packet_length-1) / endpoint->max_packet_length;
	//everyone does at least TOKEN DATA, for as many packet as needed.
	if (packet_count > 0)
		time_required += dataBits(endpoint->max_packet_length)*(packet_count-1) +
		dataBits(buffer_len - endpoint->max_packet_length * (packet_count-1));
	if (endpoint->type == usb::Endpoint::Control) {
		//apart from the data phase, Control transfers also have SETUP and STATUS.
		time_required += setupBits() + statusBits();
	}
	return time_required * mult;
}

unsigned int usb::URB::setupBits() const {
	//like dataBits(8), but we don't check endpoint->type
	return (8+8+(8+4+5)*7/6+2)+(8+8+(8*8+16)*7/6+2)+(8+8+2);
}

unsigned int usb::URB::statusBits() const {
	//like dataBits(0), but we don't check endpoint->type
	return (8+8+(8+4+5)*7/6+2)+(8+8+16*7/6+2)+(8+8+2);
}

unsigned int usb::URB::dataBits(unsigned int length) const {
	//everyone does at least TOKEN DATA, for as many packet as needed.
	unsigned int time_required = 0;
	time_required += (8+8+(8+4+5)*7/6+2)+(8+8+16*7/6+2) + ((8*length)*7/6);
	if (endpoint->type != usb::Endpoint::ISO) {
		//everyone except ISO also does handshakes.
		time_required += 8+8+2;
	}
	return time_required;
}

unsigned int usb::URB::setupTime() const {
	unsigned int mult = endpoint->device.speed == usb::Speed::Full?4:32;
	return setupBits()*mult;
}
unsigned int usb::URB::statusTime() const {
	unsigned int mult = endpoint->device.speed == usb::Speed::Full?4:32;
	return statusBits()*mult;
}

unsigned int usb::URB::dataTime(unsigned int length) const {
	unsigned int mult = endpoint->device.speed == usb::Speed::Full?4:32;
	return dataBits(length)*mult;
}
