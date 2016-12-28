#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void FDC_Setup();
void FDC_InsertDisk(int drive, char const *filename);
void FDC_EjectDisk(int drive);

//functions to be provided externally
void FDC_MotorOn();
void FDC_MotorOff();
void FDC_Activity(int drive, int activity);

#ifdef __cplusplus
}
#endif
