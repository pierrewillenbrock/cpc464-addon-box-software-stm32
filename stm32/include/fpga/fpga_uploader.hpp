
#pragma once

#include <fpga/fpga_comm.hpp>
#include <irq.h>
#include <assert.h>
#include <bits.h>

class FPGA_Uploader {
private:
	uint32_t dest;
	void const *src;
	size_t size;
	enum { Clean, Transfer, Dirty } state;
	struct Cmd {
		FPGA_Uploader *_this;
		FPGAComm_Command cmd;
	} cmd;
	//not copyable.
	FPGA_Uploader(FPGA_Uploader const &) {}
	FPGA_Uploader &operator=(FPGA_Uploader const &) {return *this;}
	void cmpl(int /*result*/) {
		ISR_Guard g;
		switch(state) {
		case Clean: assert(0); break;
		case Transfer: state = Clean; break;
		case Dirty: start(); break;
		}
	}
	void start() {
		state = Transfer;
		cmd.cmd.address = dest;
		cmd.cmd.length = size;
		cmd.cmd.write_data = src;
		FPGAComm_ReadWriteCommand(&cmd.cmd);
	}
public:
	FPGA_Uploader() : state(Clean) {
		cmd._this = this;
		cmd.cmd.read_data = NULL;
		cmd.cmd.slot = sigc::mem_fun(this, &FPGA_Uploader::cmpl);
	}
	FPGA_Uploader(uint32_t dest, void const *src, size_t size)
		: dest(dest), src(src), size(size), state(Clean) {
		cmd._this = this;
		cmd.cmd.read_data = NULL;
		cmd.cmd.slot = sigc::mem_fun(this, &FPGA_Uploader::cmpl);
	}
	~FPGA_Uploader() {
		assert(state == Clean);
	}
	void setDest(uint32_t dest) { this->dest = dest; }
	void setSrc(void const *src) { this->src = src; }
	void setSize(size_t size) { this->size = size; }
	void triggerUpload() {
		ISR_Guard g;
		switch(state) {
		case Clean: start(); break;
		case Transfer: state = Dirty; break;
		case Dirty: break;
		}
	}
};

