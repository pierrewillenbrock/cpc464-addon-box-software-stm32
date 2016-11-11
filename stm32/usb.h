
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct URB;

void USB_Setup();
void USB_submitURB(struct URB *u);

#ifdef __cplusplus
}
#endif
