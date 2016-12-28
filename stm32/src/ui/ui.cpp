
/** \todo move this to a suitable place

  so, what do we want to show?
  > constraints: 4 sprites, 1 used for mouse.
  > fdc has states: motor on(joint), r/w activity (seperate)
  > configuration(colors, position, h/v blank)
  > switch mouse to lpen or cpc mouse and back
  > select disk image
  > create a new disk image
  > select cpc joysticks
  > usable by keyboard, mouse or joystick
  > map keyboard to cpc joystick
  > simulating the cpc keyboard is _not_ possible using only the joyport.
    (missing one return line)

  keyboard operated:
  > probably just arrow keys, (shift)tab and input boxes.

  mouse operated:
  > probably need an on-screen keyboard (to enter names of new disks)

  joystick operated:
  > probably need an on-screen keyboard (to enter names of new disks)



  random: cpc mice:
  use joystick port as usual. direction lines are kept at a given direction
  until the joystick select line goes inactive. i.E., if during a given polling
  interval the mouse has been moved, the corresponding direction pins will
  go active once the select line goes active, then the direction pins go
  inactive again and stay inactive unless the corresponding movement happens.







  ui plan:
  > action/status buttons in the border area bottom right. menus pop up
    from there.
  > action/status:
    > 4 drive buttons
      > status displayed:
        > motor on
        > disk inserted
        > access(r/w or just general tbd)
      > mouse over:
        > disk image(s) or "(not inserted)"
      > menu:
        > last 4 inserted disks  (if none inserted)
        > insert disk (if none inserted)
        > new disk (if none inserted)
        > eject disk (if inserted)
    > lpen
      > status displayed:
        > active or not
      > mouse over:
        > name of the assigned device
      > menu:
        > select input (one of the mice, joystick or even keyboard emulation)
        > configure(axis/button assign for joystick input, keys for keyboard,
                    revert method)
    > cpc mouse
      > status displayed:
        > active or not
      > mouse over:
        > name of the assigned device
      > menu:
        > select input (one of the mice, joystick or even keyboard emulation)
        > configure(axis/button assign for joystick input, keys for keyboard,
                    revert method)(probably uses long left+right to revert
                                    back. we cannot pass left+right on to the
                                    cpc in that case. also need timeout on left
                                    and right for left+right detection, then.)
    > 2 cpc joysticks
      > status displayed:
        > connected/disconnected
      > mouse over:
        > name of the assigned joystick
      > menu:
        > select input
        > reassign buttons/axis
    > settings
      > status displayed:
        > none
      > mouse over:
        > none
      > menu:
        > video settings (h/v blank etc)
        > overlay settings (colors, positions, maybe hiding icons)
 */

#include <ui/ui.hpp>

namespace ui {
  static Control *toplevel = NULL;
  static Control *current_keyfocus = NULL;
  static Control *current_mousefocus = NULL;
  static MouseState mousestate;
  static uint8_t key_modifiers = 0;
}

using namespace ui;

Rect const &ui::screenRect() {
  static Rect r;
  r.x = 130;
  r.y = 50;
  r.width = 788;
  r.height = 570;
  return r;
}

void UI_mouseMove(uint16_t x, uint16_t y, int16_t dx, int16_t dy) {
  mousestate.x = x;
  mousestate.y = y;
  if (!mousestate.buttons) {
    if (toplevel) {
      Rect r = toplevel->getGlobalRect();
      Control *c = toplevel->getChildAt(Point(mousestate.x-r.x,
					      mousestate.y-r.y));
      if (!c)
	c = toplevel;
      if (c != current_mousefocus) {
	current_mousefocus->mouseLeave(mousestate);
	current_mousefocus = c;
	current_mousefocus->mouseEnter(mousestate);
      }
    }
  }
  if (current_mousefocus)
    current_mousefocus->mouseMove(dx, dy, mousestate);
}

void UI_mouseDown(uint8_t button) {
  mousestate.buttons |= (1 << button);
  if (current_mousefocus)
    current_mousefocus->mouseDown(button, mousestate);
}

void UI_mouseUp(uint8_t button) {
  mousestate.buttons &= ~(1 << button);
  if (current_mousefocus)
    current_mousefocus->mouseUp(button, mousestate);
}

void UI_mouseWheel(int8_t dz) {
  if (current_mousefocus)
    current_mousefocus->mouseWheel(dz, mousestate);
}

void UI_keyDown(uint16_t keycode) {
  /**\todo sending on keycodes is easy, but we also need to decode
     to ascii, add key-repeat for the keyChar, use
     arrow keys/tab/shift+tab/backtab for navigation and enter/space/return etc.
     for triggering actions
   */
}

void UI_keyUp(uint16_t keycode) {
}

void UI_setTopLevelControl(ui::Control *control) {
  toplevel = control;
  if (current_keyfocus)
    current_keyfocus->keyLeave();
  if (current_mousefocus) {
    for(unsigned i = 0; i < 8; i++) {
      if (mousestate.buttons & (1 << i))
	current_mousefocus->mouseUp(i, mousestate);
    }
    current_mousefocus->mouseLeave(mousestate);
  }
  if (control) {
    current_keyfocus = control->getNextControl(Control::Direction::First, Point(0,0));
    if (!current_keyfocus)
      current_keyfocus = control;
    Rect r = control->getGlobalRect();
    current_mousefocus = control->getChildAt(Point(mousestate.x-r.x,
						   mousestate.y-r.y));
    if (!current_mousefocus)
      current_mousefocus = control;
    current_mousefocus->mouseEnter(mousestate);
    r = current_mousefocus->getGlobalRect();
    current_mousefocus->mouseMove(0, 0, mousestate);
    for(unsigned i = 0; i < 8; i++) {
      if (mousestate.buttons & (1 << i))
	current_mousefocus->mouseDown(i, mousestate);
    }
    current_keyfocus->keyEnter();
  } else {
    current_keyfocus = NULL;
    current_mousefocus = NULL;
  }
}

