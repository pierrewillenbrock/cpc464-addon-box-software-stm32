
#pragma once

#include <stdint.h>
#include <stdlib.h>

namespace ui {
  struct Point {
    uint16_t x;
    uint16_t y;
    Point(uint16_t x,uint16_t y) : x(x),y(y) {}
    Point() {}
  };
  struct Rect {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
  };
  struct MouseState {
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
  };
  class Control {
  public:
    enum class Direction {
      First, Tab, Backtab, Up, Down, Left, Right
    };
    virtual ~Control() {}
    virtual Rect getRect() = 0;
    virtual Rect getGlobalRect() = 0;
    virtual void mouseDown(uint8_t button, MouseState mousestate) {}
    virtual void mouseUp(uint8_t button, MouseState mousestate) {}
    virtual void mouseWheel(int8_t dz, MouseState mousestate) {}
    virtual void mouseMove(int16_t dx, int16_t dy, MouseState mousestate) {}
    /** mouse focus enters. mouse focus stays on the given control even if
	the mouse cursor leaves the rectangle when any button is pressed.
	leave is only called once all buttons are released and the cursor
	is outside of the rect.
     */
    virtual void mouseEnter(MouseState mousestate) {}
    virtual void mouseLeave(MouseState mousestate) {}
    /** returns the child at positon p in the control, or NULL if there is
	only itself.
     */
    virtual Control *getChildAt(Point p) { return NULL; }

    virtual void keyDown(uint16_t keycode, uint8_t modifiers) {}
    virtual void keyUp(uint16_t keycode, uint8_t modifiers) {}
    /** keyChar delivers a decoded character instead of a raw keycode.
     */
    virtual void keyChar(char chr) {}
    /** keySelect is higher level than keyDown/Up in that it is called whenever
     * an selection event happens(like enter, spacebar, joystick buttons)
     */
    virtual void keySelect() {}
    /** keyboard focus enters
     */
    virtual void keyEnter() {}
    /** keyboard focus leaves
     */
    virtual void keyLeave() {}
    virtual Control *getNextControl(Direction dir, Point reference) { return NULL; }
  };
  Rect const &screenRect();
};

void UI_mouseMove(uint16_t x, uint16_t y, int16_t dx, int16_t dy);
void UI_mouseDown(uint8_t button);
void UI_mouseUp(uint8_t button);
void UI_mouseWheel(int8_t dz);
void UI_keyDown(uint16_t keycode);
void UI_keyUp(uint16_t keycode);

void UI_setTopLevelControl(ui::Control *control);

