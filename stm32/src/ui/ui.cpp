
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
  static Control *current_inputfocus = NULL; //shared with joystick
  static Point current_inputfocus_point(0,0);
  static Control *current_mousefocus = NULL;
  static MouseState mousestate;
  static JoyState joystate;
  static uint8_t key_modifiers = 0;
  static uint8_t key_country = 0;
}

using namespace ui;

Screen ui::screen;

Screen::Screen() {
  m_options.vsync_start = 0;
  m_options.vsync_end = 4;
  m_options.vblank_start = -2;
  m_options.vblank_end = 25;
  m_options.hsync_start = -20;
  m_options.hsync_end = 40;
  m_options.hblank_start = -90;
  m_options.hblank_end = 145;
  //this calculation is mirrored in the graphics checker
  unsigned w = 1023+1;
  unsigned h = 312;
  m_rect.x = m_options.hblank_end - 15;
  m_rect.y = m_options.vblank_end * 2;
  m_rect.width = w + m_options.hblank_start - m_options.hblank_end;
  m_rect.height = (h + m_options.vblank_start - m_options.vblank_end) * 2;
}

void Screen::setRect(Rect const &rect) {
  m_rect = rect;
  m_onRectChange(m_rect);
}

void Screen::setOptions(Options const &options) {
  m_options = options;
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
  if(current_inputfocus)
    current_inputfocus->keyDown(keycode, key_modifiers, key_country);
  /**\todo sending on keycodes is easy, but we also need to decode
     to ascii, add key-repeat for the keyChar, use
     arrow keys/tab/shift+tab/backtab for navigation and enter/space/return etc.
     for triggering actions
   */
}

void UI_keyUp(uint16_t keycode) {
  if(current_inputfocus)
    current_inputfocus->keyUp(keycode, key_modifiers, key_country);
}

void UI_keyCountry(uint8_t country) {
  key_country = country;
}

void UI_joyTrgDown(ui::JoyTrg trg) {
  joystate.triggers |= (1 << (int)trg);
  if(current_inputfocus)
    current_inputfocus->joyTrgDown(trg, joystate);
}

void UI_joyTrgUp(ui::JoyTrg trg) {
  joystate.triggers &= ~(1 << (int)trg);
  if(current_inputfocus)
    current_inputfocus->joyTrgUp(trg, joystate);
}

void UI_joyAxis(int8_t x, int8_t y) {
  joystate.x = x;
  joystate.y = y;
  if(current_inputfocus)
    current_inputfocus->joyAxis(joystate);
}

void UI_setTopLevelControl(ui::Control *control) {
  toplevel = control;
  if (current_inputfocus)
    current_inputfocus->focusLeave();
  if (current_mousefocus) {
    for(unsigned i = 0; i < 8; i++) {
      if (mousestate.buttons & (1 << i))
	current_mousefocus->mouseUp(i, mousestate);
    }
    current_mousefocus->mouseLeave(mousestate);
  }
  if (control) {
    current_inputfocus_point = Point(0,0);
    current_inputfocus = control->getNextControl(Control::Direction::Tab, NULL,
					               current_inputfocus_point);
    if (!current_inputfocus)
      current_inputfocus = control;
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
    current_inputfocus->focusEnter();
  } else {
    current_inputfocus = NULL;
    current_mousefocus = NULL;
  }
}

void UI_moveFocus(Control::Direction dir) {
  if(!current_inputfocus)
    return;
  Control *nextFocus = current_inputfocus;
  nextFocus = current_inputfocus->getNextControl(dir, current_inputfocus,
              current_inputfocus_point);
  if(!nextFocus)
    return;
  if (nextFocus == current_inputfocus)
    return;
  current_inputfocus->focusLeave();
  current_inputfocus = nextFocus;
  current_inputfocus->focusEnter();
}

void UI_setFocus(ui::Control *control) {
  if (current_inputfocus == control)
    return;
  if (current_inputfocus)
    current_inputfocus->focusLeave();
  current_inputfocus = control;
  if (current_inputfocus) {
    current_inputfocus->focusEnter();
    Rect r = current_inputfocus->getGlobalRect();
    if (current_inputfocus_point.x < r.x)
      current_inputfocus_point.x = r.x;
    if (current_inputfocus_point.x >= r.x + r.width)
      current_inputfocus_point.x = r.x + r.width-1;
    if (current_inputfocus_point.y < r.y)
      current_inputfocus_point.y = r.y;
    if (current_inputfocus_point.y >= r.y + r.height)
      current_inputfocus_point.y = r.y + r.height-1;
  }
}

void Control::joyTrgDown(JoyTrg trg, JoyState /*state*/) {
  //Default for movement: move to the next control
  switch(trg) {
  case JoyTrg::Up:
    UI_moveFocus(Control::Direction::Up);
    break;
  case JoyTrg::Down:
    UI_moveFocus(Control::Direction::Down);
    break;
  case JoyTrg::Left:
    UI_moveFocus(Control::Direction::Left);
    break;
  case JoyTrg::Right:
    UI_moveFocus(Control::Direction::Right);
    break;
  case JoyTrg::Next:
    UI_moveFocus(Control::Direction::Tab);
    break;
  case JoyTrg::Previous:
    UI_moveFocus(Control::Direction::Backtab);
    break;
  case JoyTrg::Btn1:
    keySelect();
    break;
  default: break;
  }
}

// kate: indent-width 2; indent-mode cstyle;
