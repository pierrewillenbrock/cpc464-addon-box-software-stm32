#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void FDC_Setup();
void FDC_InsertDisk(int drive, char const *filename);
void FDC_EjectDisk(int drive);

#ifdef __cplusplus
}
#endif
