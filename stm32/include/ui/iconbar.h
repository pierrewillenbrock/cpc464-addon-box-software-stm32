
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void IconBar_disk_assigned(unsigned no, char const *displayname);
void IconBar_disk_unassigned(unsigned no);
void IconBar_disk_activity(unsigned no, bool activity);
void IconBar_disk_motor_on();
void IconBar_disk_motor_off();
void IconBar_joystick_assigned(unsigned no, char const *displayname);
void IconBar_joystick_unassigned(unsigned no);
void IconBar_mouse_assigned(char const *displayname);
void IconBar_mouse_unassigned();
void IconBar_lpen_assigned(char const *displayname);
void IconBar_lpen_unassigned();
void IconBar_Setup();

#ifdef __cplusplus
}
#endif
