#include "timerISR.h"     // For timer interrupt functionality
#include "helper.h"       // General helper functions (likely contains GetBit, SetBit, etc.)
#include "spiAVR.h"       // For SPI communication on AVR microcontrollers
#include "irAVR.h"        // For infrared communication on AVR (not obviously used in this snippet, but included)
//#include "LCD.h"        // LCD library (commented out, not used)
#include "periph.h"       // Peripheral control functions (likely ADC_init, ADC_read)
//#include "serialATmega.h" // Serial communication for ATmega (commented out, not used)
#include "stdlib.h"       // Standard library, needed for rand() function
#include "color.h"        // Color definitions or functions (likely contains color constants or Color565 if not defined here)

// Display dimensions: 128 pixels wide, 160 pixels tall
// LED Pin Configuration for the ST7735 TFT display:
// PIN_SCK  PORTB1  // SPI Clock
// PIN_MOSI PORTB2  // SPI Master Out Slave In
// PIN_A0   PORTH3  // Data/Command select (also called DC or RS)
// PIN_RESET PORTG5 // TFT Reset pin
// PIN_CS   PORTE5  // SPI Chip Select for TFT
// GND             // Ground connection
// VCC             // Power supply connection

// ST7735 TFT Controller Commands
#define SWRESET 0x01 //Software RESET command
#define SLPOUT  0x11 //Sleep out command (wakes up the display)
#define COLMOD  0x3A //Interface Pixel Format command (sets color depth)
#define DISPON  0x29 //Display On command
#define CASET   0x2A //Column Address Set command (defines horizontal drawing range)
#define RASET   0x2B //Row Address Set command (defines vertical drawing range)
#define RAMWR   0x2C //Memory Write command (starts pixel data transfer to TFT RAM)

//GLOBAL VARIABLES
unsigned char gameState = 0; //0: Pause game, 1: Play game. Controls overall game flow.

//Player 1 Joystick related variables
unsigned short p1Joy;      // Stores the raw ADC value from Player 1's joystick (analog input)
unsigned short p1SW;       // Stores the state of Player 1's joystick button (0 for not pressed, 1 for pressed)
unsigned char p1JoyState;  // Represents the interpreted state of Player 1's joystick: 0: IDLE, 1: RIGHT, 2:LEFT

//Player 1 Character properties
#define CHAR_WIDTH 14      // Width of the player character in pixels
#define SCREEN_WIDTH 128   // Width of the game screen in pixels
#define XPOS_MIN 0         // Minimum x-coordinate the player character can reach (left edge)
#define XPOS_MAX (SCREEN_WIDTH - CHAR_WIDTH) // 114, Maximum x-coordinate (right edge minus character width)
int xPos = (SCREEN_WIDTH - CHAR_WIDTH) / 2; // Player's current horizontal position, initialized to screen center
int p1Score = 0;           // Player 1's current score

// Performs a hardware reset of the ST7735 TFT display.
// This is typically done at the beginning of the initialization sequence.
void HardwareReset()
{
  //Reset pin is on PORTG, specifically PIN_RESET (PG5 as per comments)
  PORTG &= ~(1 << PIN_RESET); //Set PIN_RESET low
  _delay_ms(200);             //Wait for 200 milliseconds
  PORTG |= (1 << PIN_RESET);  //Set PIN_RESET high
  _delay_ms(200);             //Wait for 200 milliseconds
}

// Sends a command byte to the ST7735 TFT display.
// Commands instruct the display on operations like reset, display on/off, color mode, etc.
void Send_Command(char cmd)
{
  //PIN_A0 (Data/Command select) is on PORTH3
  PORTH &= ~(1 << PIN_A0); //Set A0 low to indicate a command is being sent
  //PIN_CS (Chip Select) is on PORTE5
  PORTE &= ~(1 << PIN_CS);  //Set CS low to select/activate the TFT display for communication
  SPI_SEND(cmd);            //Send the command byte via SPI
  PORTE |= (1 << PIN_CS);   //Set CS high to deselect/deactivate the TFT display
}

// Sends a data byte to the ST7735 TFT display.
// Data bytes are typically parameters for commands or actual pixel data.
void Send_Data(char data)
{
  //PIN_A0 (Data/Command select) is on PORTH3
  PORTH |= (1 << PIN_A0);  //Set A0 high to indicate data is being sent
  //PIN_CS (Chip Select) is on PORTE5
  PORTE &= ~(1 << PIN_CS); //Set CS low to select/activate the TFT display for communication
  SPI_SEND(data);          //Send the data byte via SPI
  PORTE |= (1 << PIN_CS);  //Set CS high to deselect/deactivate the TFT display
}

// Initializes the ST7735 TFT display.
// Sends a sequence of commands to configure the display for operation.
void ST7735_init()
{
  HardwareReset(); //Perform a hardware reset first

  Send_Command(SWRESET); //Send Software RESET command
  _delay_ms(150);        //Wait 150ms for the reset to complete

  Send_Command(SLPOUT);  //Send Sleep Out command to wake up the display
  _delay_ms(200);        //Wait 200ms for the display to wake up

  Send_Command(COLMOD);  //Send Interface Pixel Format command
  Send_Data(0x05);       //Set to 16-bit pixel format (RGB 5-6-5)
  _delay_ms(10);         //Wait 10ms

  Send_Command(DISPON);  //Send Display On command
  _delay_ms(200);        //Wait 200ms for the display to turn on
}

// Converts 8-bit Red, Green, and Blue color values to a 16-bit RGB 5-6-5 format.
// This format is commonly used by color TFT displays.
// RRRRRGGGGGGBBBBB
uint16_t Color565(uint8_t r, uint8_t g, uint8_t b) {
  // (r & 0xF8) keeps the top 5 bits of red, shifted left by 8
  // (g & 0xFC) keeps the top 6 bits of green, shifted left by 3
  // (b >> 3) keeps the top 5 bits of blue
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Fills the entire screen with a specified 16-bit color.
// Note: Screen dimensions are 128 rows (horizontal) and 160 columns (vertical) as per comments.
// However, CASET (Column Address Set) usually refers to X-axis (width)
// and RASET (Row Address Set) usually refers to Y-axis (height).
// The code seems to use 128 for columns (width) and 160 for rows (height).
void fillScreen(uint16_t color){
  Send_Command(CASET);      //Set Column Address (X-axis)
  Send_Data(0x00); Send_Data(0x00); //Start column: 0
  Send_Data(0x00); Send_Data(128-1); //End column: 127 (for 128 pixels wide) -> 0x7F. Original was 0x80 (128) which might be one pixel too many.
                                  // Using 0x80 might target 129 columns, or the controller interprets it as up to index 128.
                                  // For standard 0-indexed, 128 pixels is 0 to 127. Let's assume the controller handles it.

  Send_Command(RASET);      //Set Row Address (Y-axis)
  Send_Data(0x00); Send_Data(0x00); //Start row: 0
  Send_Data(0x00); Send_Data(160-1); //End row: 159 (for 160 pixels tall) -> 0x9F. Original was 0xA0 (160).

  Send_Command(RAMWR);      //Memory Write command, preparing to send pixel data

  // Loop through all pixels on the screen
  for(int i=0; i < 128 * 160; i++){
    Send_Data(color >> 8);   //Send the Upper (High) byte of the 16-bit color
    Send_Data(color & 0xFF); //Send the Lower (Low) byte of the 16-bit color
  }
}

// Draws a filled rectangle (sprite) on the screen with a specified color.
// XS: Start X-coordinate (column)
// XE: End X-coordinate (column)
// YS: Start Y-coordinate (row)
// YE: End Y-coordinate (row)
// color565: The 16-bit color of the rectangle
void sprite(int XS, int XE, int YS, int YE, uint16_t color565)
{
  // Ensure coordinates are within screen bounds (optional, good practice)
  // if (XS < 0) XS = 0;
  // if (XE >= SCREEN_WIDTH) XE = SCREEN_WIDTH - 1; // SCREEN_WIDTH is 128
  // if (YS < 0) YS = 0;
  // if (YE >= 160) YE = 160 - 1; // Screen height is 160

  Send_Command(CASET); //Set Column Address range (X-axis)
  Send_Data(0x00); Send_Data(XS); //Start column
  Send_Data(0x00); Send_Data(XE); //End column

  Send_Command(RASET); //Set Row Address range (Y-axis)
  Send_Data(0x00); Send_Data(YS); //Start row
  Send_Data(0x00); Send_Data(YE); //End row

  Send_Command(RAMWR); //Memory Write command, preparing to send pixel data

  // Calculate the number of pixels in the rectangle
  int numPixels = (XE - XS + 1) * (YE - YS + 1);
  for (int i = 0; i < numPixels; i++)
  {
    Send_Data(color565 >> 8);   //Send the High byte of the color
    Send_Data(color565 & 0xFF); //Send the Low byte of the color
  }
}

// Draws Player 1's character at a specified x-coordinate.
// The character is composed of several colored rectangles (sprites).
// Character is 14 pixels wide. Its y-position is fixed around 140-159.
void drawP1At(int x) {
  // Define colors using the Color565 converter
  uint16_t yellow = Color565(255, 255, 0); // Main body color
  uint16_t white  = Color565(255, 255, 255); // Detail or highlight color
  uint16_t blue   = Color565(0, 0, 255);   // Secondary body color or detail
  uint16_t gray   = Color565(160, 160, 160); // Detail or shadow color
  uint16_t black  = Color565(0, 0, 0);     // Outline or detail color

  // Draw the main body of the character (a yellow rectangle)
  // x to x+13 (14px wide), y from 140 to 153 (14px tall)
  sprite(x, x + 13, 140, 153, yellow);

  // Draw a gray detail on the character
  // x+3 to x+10 (8px wide), y from 143 to 147 (5px tall)
  sprite(x + 3, x + 10, 143, 147, gray);

  // Draw a white highlight/detail
  // x+4 to x+9 (6px wide), y from 144 to 146 (3px tall)
  sprite(x + 4, x + 9, 144, 146, white);

  // Draw another white highlight/detail, possibly overlapping
  // x+5 to x+8 (4px wide), y from 143 to 147 (5px tall)
  sprite(x + 5, x + 8, 143, 147, white);

  // Draw a black detail, perhaps an eye or feature
  // x+6 to x+7 (2px wide), y from 144 to 145 (2px tall)
  sprite(x + 6, x + 7, 144, 145, black);

  // Draw a blue part of the character, possibly feet or base
  // x to x+13 (14px wide), y from 150 to 153 (4px tall)
  sprite(x, x + 13, 150, 153, blue);

  // Draw more blue parts, extending downwards (like legs/feet)
  // x to x+2 (3px wide), y from 154 to 159 (6px tall) - left foot/side
  sprite(x, x + 2, 154, 159, blue);
  // x+11 to x+13 (3px wide), y from 154 to 159 (6px tall) - right foot/side
  sprite(x + 11, x + 13, 154, 159, blue);
}

// Draws a single digit (0-9) at a specified position (x, y) with a given color and scale.
// Digits are represented by a 5x3 bitmap.
void drawDigit(uint8_t digit, uint8_t x, uint8_t y, uint16_t color, uint8_t scale) {
  // Bitmap patterns for digits 0-9. Each row is 3 bits.
  // For example, 0b111 is a solid line, 0b101 is a line with a gap.
  static const uint8_t digits[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, //0
    {0b010, 0b110, 0b010, 0b010, 0b111}, //1
    {0b111, 0b001, 0b111, 0b100, 0b111}, //2
    {0b111, 0b001, 0b111, 0b001, 0b111}, //3
    {0b101, 0b101, 0b111, 0b001, 0b001}, //4
    {0b111, 0b100, 0b111, 0b001, 0b111}, //5
    {0b111, 0b100, 0b111, 0b101, 0b111}, //6
    {0b111, 0b001, 0b010, 0b100, 0b100}, //7
    {0b111, 0b101, 0b111, 0b101, 0b111}, //8
    {0b111, 0b101, 0b111, 0b001, 0b111}, //9
  };

  // Iterate through each row of the digit's bitmap (5 rows)
  for (uint8_t row = 0; row < 5; row++) {
    // Iterate through each column of the digit's bitmap (3 columns)
    for (uint8_t col = 0; col < 3; col++) {
      // Check if the current bit in the bitmap is set (1)
      // (1 << (2 - col)) creates a mask to check bits from left to right (e.g., 0b100, 0b010, 0b001)
      if (digits[digit][row] & (1 << (2 - col))) {
        // If the bit is set, draw a scaled pixel (rectangle) for that part of the digit
        sprite(
          x + col * scale,                // Start X of the scaled pixel
          x + (col + 1) * scale - 1,      // End X of the scaled pixel
          y + row * scale,                // Start Y of the scaled pixel
          y + (row + 1) * scale - 1,      // End Y of the scaled pixel
          color                           // Color of the pixel
        );
      }
    }
  }
}

// Draws the player's score on the screen.
// Score is a 16-bit unsigned integer, displayed as up to 3 digits.
void drawScore(uint16_t score) {
  uint8_t x = 2;     // Initial X position for the first digit
  uint8_t y = 2;     // Y position for the score display
  uint8_t scale = 2; // Scale factor for drawing digits (each pixel of digit becomes scale x scale block)
  uint16_t color = Color565(255, 255, 255); // White color for the score

  // Clear the area where the previous score was displayed to prevent ghosting
  // Area cleared is from (0,0) to (39,19), assuming max 3 digits, scaled.
  sprite(0, 40, 0, 20, 0x0000); // Clear with black color

  uint16_t temp = score;       // Temporary variable to extract digits from score
  uint8_t digits[3] = {0, 0, 0}; // Array to hold the individual digits (hundreds, tens, ones)

  // Extract digits from the score, starting from the ones place
  // e.g., if score is 123, digits will be [1, 2, 3]
  for (int i = 2; i >= 0; i--) { // Iterate for hundreds, tens, ones
    digits[i] = temp % 10;    // Get the last digit
    temp /= 10;               // Remove the last digit
    if (temp == 0 && i > 0 && score < (i==2 ? 100 : 10) ) { // Optimization: if score < 100, hundreds is 0. if score < 10, tens is 0.
        // This part of the original loop for extracting digits was slightly off.
        // The break condition `temp > 0` in the original loop handles this better.
        // Corrected logic:
        // digits[2] = score % 10;
        // digits[1] = (score / 10) % 10;
        // digits[0] = (score / 100) % 10;
    }
  }
  // Re-extracting digits for clarity and correctness
  digits[0] = (score / 100) % 10; // Hundreds digit
  digits[1] = (score / 10) % 10;  // Tens digit
  digits[2] = score % 10;         // Ones digit


  uint8_t xpos = x; // Current x-position for drawing digits, starts at initial x
  // Iterate through the extracted digits (hundreds, tens, ones)
  for (int i = 0; i < 3; i++) {
    bool shouldDraw = true; // Flag to determine if the current digit should be drawn

    // Logic to suppress leading zeros
    if (i == 0 && digits[i] == 0 && score < 100) { // If it's the hundreds digit, it's 0, and score is less than 100
      //xpos += 4 * scale; // Advance x position as if a digit (3*scale) and spacing (1*scale) were drawn.
      shouldDraw = false; // Don't draw leading zero for hundreds
    } else if (i == 1 && digits[i] == 0 && score < 10 && digits[0] == 0) { // If it's the tens digit, it's 0, score < 10, and hundreds was also 0
      //xpos += 4 * scale;
      shouldDraw = false; // Don't draw leading zero for tens if hundreds was also zero
    }

    if (shouldDraw) {
      drawDigit(digits[i], xpos, y, color, scale); // Draw the current digit
      xpos += (3 * scale) + scale; // Advance x position: 3*scale for digit width + 1*scale for spacing
    } else if ( (i==0 && score < 100) || (i==1 && score < 10 && digits[0] == 0) ) {
        // If not drawing but it was a leading zero that was skipped, still account for its space
        // This ensures digits like " 5" or "  5" are not drawn, rather "5" is drawn at the correct starting position.
        // The original code seems to want to right-align or handle spacing for fewer digits.
        // A simpler way for left-aligned score with leading zero suppression:
        // if (i == 0 && digits[0] == 0 && score < 100) continue; // Skip hundreds if 0 and score < 100
        // if (i == 1 && digits[0] == 0 && digits[1] == 0 && score < 10) continue; // Skip tens if 0 and hundreds was 0 and score < 10
        // The current logic effectively does this by advancing xpos only when drawing.
        // The `xpos += 4 * scale` within the `if (!shouldDraw)` block in the original code was removed
        // as `shouldDraw` already gates the drawing and `xpos` advancement.
        // The intention is likely: if it's a leading zero that we *don't* draw, we don't advance the cursor for it.
    }
  }
}

// Bomb dimensions
#define BOMB_WIDTH  12 // Width of the bomb sprite in pixels
#define BOMB_HEIGHT 12 // Height of the bomb sprite in pixels

// Draws a bomb at a specified (x, y) coordinate.
// The bomb is drawn as two halves: top orange, bottom red.
void drawBomb(int x, int y) {
  uint16_t red = Color565(255, 0, 0);       // Red color for the bottom half
  uint16_t orange = Color565(255, 165, 0); // Orange color for the top half

  // Draw the top half of the bomb (orange)
  // x to x+BOMB_WIDTH-1, y to y+(BOMB_HEIGHT/2)-1
  sprite(x, x + BOMB_WIDTH - 1, y, y + (BOMB_HEIGHT / 2) - 1, orange);
  // Draw the bottom half of the bomb (red)
  // x to x+BOMB_WIDTH-1, y+(BOMB_HEIGHT/2) to y+BOMB_HEIGHT-1
  sprite(x, x + BOMB_WIDTH - 1, y + (BOMB_HEIGHT / 2), y + BOMB_HEIGHT - 1, red);
}

//Task struct for concurrent synchSMs implementations
typedef struct _task{
	signed 	 char state; 		//Task's current state (specific to the task's state machine)
	unsigned long period; 		//Task period in multiples of GCD timer tick
	unsigned long elapsedTime; 	//Time elapsed since last task tick, in multiples of GCD timer tick
	int (*TickFct)(int); 		//Task tick function pointer (points to the state machine function for this task)
} task;

// Define periods for each task in milliseconds (assuming GCD is 1ms)
const unsigned long PERIOD_JOYSTICK = 50;    // Joystick reading task period
const unsigned long PERIOD_MOVE = 50;        // Player movement task period
const unsigned long PERIOD_SCORE = 1000;     // Score update task period (1 second)
const unsigned long PERIOD_GAMESTATE = 100;  // Game state update task period
const unsigned long PERIOD_BOMB = 45;        // Bomb logic task period
const unsigned long PERIOD_GCD = 1;          // Greatest Common Divisor of all periods (base timer tick, 1ms)

#define NUM_TASKS 5 // Number of concurrent tasks defined
task tasks[NUM_TASKS]; // Array to hold all the task structures

// Timer Interrupt Service Routine (ISR)
// This function is called by the hardware timer at intervals defined by PERIOD_GCD.
// It iterates through all tasks and executes their TickFct if their period has elapsed.
void TimerISR() {
	for ( unsigned int i = 0; i < NUM_TASKS; i++ ) {                   // Iterate through each task in the task array
		if ( tasks[i].elapsedTime >= tasks[i].period ) {           // Check if the task's period has been met or exceeded
			tasks[i].state = tasks[i].TickFct(tasks[i].state); // Execute the task's state machine function (TickFct) and update its state
			tasks[i].elapsedTime = 0;                          // Reset the elapsed time for this task
		}
		tasks[i].elapsedTime += PERIOD_GCD;                        // Increment the elapsed time for this task by the GCD period
	}
}

// State machine for handling joystick input
enum JOY_STATES{JOYINIT, JOYREAD}; // Defines the states for the joystick task
int tickJoy(int state)
{
  //Transitions for joystick state machine
  switch(state)
  {
    case JOYINIT: // Initial state
      state = JOYREAD; // Transition to JOYREAD state immediately
    break;

    case JOYREAD: // Reading state
      state = JOYREAD; // Remain in JOYREAD state to continuously read input
    break;

    default: // Should not happen
      state = JOYREAD; // Default to JOYREAD if an unknown state is encountered
    break;
  }

  //Actions for joystick state machine
  switch(state)
  {
    case JOYINIT:
      // No actions in the initial state
    break;

    case JOYREAD:
      p1Joy = ADC_read(0);      // Read analog value from ADC channel 0 for Player 1's joystick (measures X or Y axis)
      p1SW = !GetBit(PINF, 2);  // Read digital value from PINF bit 2 for Player 1's joystick switch.
                                // !GetBit because joystick button is likely active low (pressed = 0, released = 1).
                                // So, p1SW = 1 if pressed, 0 if not.

      // Interpret the raw joystick ADC value to determine direction
      if(p1Joy <= 600 && p1Joy >= 400){ // Joystick is in the center (dead zone)
        p1JoyState = 0; // IDLE Position
        PORTB &= ~(1 << 7); // Turn off buzzer/indicator on PORTB7
      }
      else if(p1Joy < 400){ // Joystick pushed to one side (e.g., right, depending on wiring and ADC range)
        p1JoyState = 1; // RIGHT Position (as per comment)
        PORTB |= (1 << 7); // Turn on buzzer/indicator on PORTB7
      }
      else if(p1Joy > 600){ // Joystick pushed to the other side (e.g., left)
        p1JoyState = 2; // LEFT Position (as per comment)
        PORTB |= (1 << 7); // Turn on buzzer/indicator on PORTB7
      }
    break;
  }

  return state; // Return the current (or next) state
}

// State machine for handling player character movement
enum MOVE_STATES{MOVEIDLE, MOVERIGHT, MOVELEFT}; // Defines states for player movement
int tickMove(int state)
{
  // If the game is paused (gameState == 0), the player cannot move.
  // Redraw the player at the current position and return to MOVEIDLE.
  if(gameState == 0){
    drawP1At(xPos); // Ensure player is visible even if paused mid-move in a previous cycle
    return MOVEIDLE; // Force idle state during pause
  }

  //Transitions for player movement state machine
  switch(state)
  {
    case MOVEIDLE: // Player is currently idle
      if(p1JoyState == 1){ // Joystick indicates move RIGHT
        state = MOVERIGHT;
      }
      else if (p1JoyState == 2){ // Joystick indicates move LEFT
        state = MOVELEFT;
      }
      else if  (p1JoyState == 0){ // Joystick is idle
        state = MOVEIDLE; // Remain idle
      }
    break;

    case MOVERIGHT: // Player was moving right in the last tick
      state = MOVEIDLE; // Transition back to MOVEIDLE to check joystick again in the next tick
                      // This creates a "tap to move" or "move one step per tick held" behavior
    break;

    case MOVELEFT: // Player was moving left in the last tick
      state = MOVEIDLE; // Transition back to MOVEIDLE
    break;

    default: // Should not happen
      state = MOVEIDLE; // Default to MOVEIDLE
    break;
  }

  //Actions for player movement state machine
  switch(state)
  {
    case MOVEIDLE:
      // No movement action when idle
    break;

    case MOVERIGHT:
      if(xPos < XPOS_MAX){ // Check if player is not at the rightmost screen edge
        sprite(xPos, xPos + CHAR_WIDTH -1 , 140, 159, 0x0000); //Clear previous player sprite (use CHAR_WIDTH-1 for correct width)
        xPos += 9; // Move player right by step size (9 pixels, aka speed)
        if (xPos > XPOS_MAX) xPos = XPOS_MAX; //Edge Guard: Ensure player doesn't go beyond XPOS_MAX
      }
    break;

    case MOVELEFT:
      if(xPos > XPOS_MIN){ // Check if player is not at the leftmost screen edge
        sprite(xPos, xPos + CHAR_WIDTH - 1, 140, 159, 0x0000); //Clear previous player sprite
        xPos -= 9; // Move player left by step size (9 pixels, aka speed)
        if (xPos < XPOS_MIN) xPos = XPOS_MIN; //Edge Guard: Ensure player doesn't go beyond XPOS_MIN
      }
    break;
  }
  drawP1At(xPos); // Redraw player at the new (or current) position
  return state; // Return the current (or next) state
}

// State machine for updating the score
enum SCORE_STATES{SCORERESET, SCORECOUNT}; // Defines states for score management
int tickScore(int state)
{
  // If the game is paused (gameState == 0), the score should not change.
  if(gameState == 0){
    // drawScore(p1Score); // Optionally redraw score to ensure it's visible, though it shouldn't change.
    return state; // Remain in current state without updating score
  }

  //Transitions for score state machine
  switch(state)
  {
    case SCORERESET: // Initial state or after a game reset
      state = SCORECOUNT; // Transition to SCORECOUNT to start/resume counting
      p1Score = 0;        // Reset score to 0 when entering SCORERESET from elsewhere (e.g. game over)
                          // Or, this could be the initial setup. If reset elsewhere, this line might be redundant here.
                          // Current logic implies SCORERESET is mainly an initial state.
      drawScore(p1Score); // Display the initial score (0)
    break;

    case SCORECOUNT:
      // Remain in SCORECOUNT state to continuously increment score while game is playing
      // No explicit transition out of SCORECOUNT from here other than game pause or reset handled externally.
    break;

    default: // Should not happen
      state = SCORECOUNT; // Default to SCORECOUNT
    break;
  }

  //Actions for score state machine
  switch(state)
  {
    case SCORERESET:
      // Action handled in transition (score reset and draw)
    break;

    case SCORECOUNT:
      p1Score++;          // Increment player's score
      drawScore(p1Score); // Update the score display
    break;

    default:
      // No actions for default case
    break;
  }
  return state; // Return the current (or next) state
}


// State machine for managing the game's overall state (Pause/Play)
enum GAMESTATE_STATES{GAMEINIT, PAUSE, PAUSEPRESS, PLAY, PLAYPRESS}; // Defines states for game state management
int tickGameState(int state)
{
  //Transitions for game state machine
  switch(state)
  {
    case GAMEINIT: // Initial state of the game state machine
      state = PAUSE; // Start the game in a PAUSED state
    break;

    case PAUSE: // Game is currently paused
      if(p1SW == 1){ // Player 1 presses the joystick switch
        state = PAUSEPRESS; // Transition to PAUSEPRESS to debounce or confirm
      }
      // else if(p1SW==0){ // Player 1 switch is not pressed
      //   state = PAUSE; // Remain in PAUSE state (this is redundant, as it's the default if no transition occurs)
      // }
    break;

    case PAUSEPRESS: // Joystick switch was pressed while in PAUSE state
      if(p1SW == 0){ // Player 1 releases the joystick switch
        state = PLAY; // Transition to PLAY state (resume/start game)
      }
      // else if(p1SW == 1){ // Player 1 switch is still pressed
      //   state = PAUSEPRESS; // Remain in PAUSEPRESS (wait for release)
      // }
    break;

    case PLAY: // Game is currently playing
      if(p1SW == 1){ // Player 1 presses the joystick switch
        state = PLAYPRESS; // Transition to PLAYPRESS to debounce or confirm
      }
      // else if(p1SW == 0){ // Player 1 switch is not pressed
      //   state = PLAY; // Remain in PLAY state
      // }
    break;

    case PLAYPRESS: // Joystick switch was pressed while in PLAY state
      if(p1SW==0){ // Player 1 releases the joystick switch
        state = PAUSE; // Transition to PAUSE state (pause game)
      }
      // else if(p1SW == 1){ // Player 1 switch is still pressed
      //   state = PLAYPRESS; // Remain in PLAYPRESS (wait for release)
      // }
    break;

    default: // Should not happen
      state = PAUSE; // Default to PAUSE state
    break;
  }

  //Actions for game state machine
  switch(state)
  {
    case GAMEINIT:
      // No specific actions in initial state, handled by transition to PAUSE
    break;

    case PAUSE:
      gameState = 0; // Set the global gameState variable to 0 (Paused)
    break;

    case PAUSEPRESS:
      // No action here, waiting for button release
    break;

    case PLAY:
      gameState = 1; // Set the global gameState variable to 1 (Playing)
    break;

    case PLAYPRESS:
      // No action here, waiting for button release
    break;
  }
  return state; // Return the current (or next) state
}

#define MAX_BOMBS 4     // Maximum number of bombs allowed on screen at once
#define CHAR_Y 140      // Y-coordinate of the top of the player character
#define CHAR_HEIGHT 20  // Height of the player character sprite (visual height, 159-140+1 = 20)

// Structure to define a bomb object
typedef struct {
  int x;          // Current X-coordinate of the bomb's top-left corner
  int y;          // Current Y-coordinate of the bomb's top-left corner
  uint8_t active; // Status of the bomb: 1 if active (on-screen, moving), 0 if inactive (available for reuse)
} Bomb;
Bomb bombs[MAX_BOMBS]; // Array to hold all bomb objects

// Resets the game state after a player collision with a bomb.
void resetGame() {
    int playerXAtDeath = xPos; // Store player's X position at the moment of collision

    gameState = 0; // Pause the game immediately

    // Clear the player sprite from its last position
    sprite(playerXAtDeath, playerXAtDeath + CHAR_WIDTH - 1, CHAR_Y, CHAR_Y + CHAR_HEIGHT - 1, 0x0000); // Black color to erase

    // Reset player position to the center of the screen
    xPos = (SCREEN_WIDTH - CHAR_WIDTH) / 2;

    // Reset player score
    p1Score = 0;
    drawScore(p1Score); // Update score display to show 0

    // Deactivate and clear all bombs from the screen
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (bombs[i].active) { // If the bomb was active
            // Define a slightly larger area to clear around the bomb to ensure it's fully erased,
            // especially if it was partially off-screen or moving.
            // The Y clearing is a bit generous here to catch bombs that are partially off top/bottom during update.
            int clearTop = (bombs[i].y - 6 < 0) ? 0 : bombs[i].y - 6; // Prevent negative Y for clearing
            int clearBottom = bombs[i].y + BOMB_HEIGHT + 6; // Clear a bit below the bomb too
            if (clearBottom >= 160) clearBottom = 159; // Stay within screen bounds for clearing

            sprite(bombs[i].x, bombs[i].x + BOMB_WIDTH - 1, clearTop, clearBottom, 0x0000); // Erase bomb
            bombs[i].active = 0; // Mark bomb as inactive
        }
    }

    drawP1At(xPos); // Draw the player at the reset position (center)
    // The game will remain paused (gameState = 0) until the player unpauses it via tickGameState.
}

// State machine for managing bombs (spawning, movement, collision)
enum BOMB_STATES{BOMBIDLE, BOMBSEND}; // BOMBIDLE could be initial or post-reset. BOMBSEND is active processing.
                                      // Current implementation doesn't really use these states distinctively within the tick.
int tickBomb(int state)
{
  // If the game is paused, do not update bomb logic (bombs freeze)
  if(gameState == 0){
    // Optional: redraw existing bombs if they should remain visible while paused
    // for (int i = 0; i < MAX_BOMBS; i++) {
    //   if (bombs[i].active) {
    //     drawBomb(bombs[i].x, bombs[i].y);
    //   }
    // }
    return state; // Exit without processing bombs
  }

  // Attempt to activate a new bomb if there's an inactive one available
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!bombs[i].active) { // If this bomb slot is inactive
      if ((rand() % 100) < 3) { // Small chance (e.g., 3% if rand % 100) to spawn a bomb per active bomb slot check per tickBomb call
                               // Original: (rand() % 10 < 3) is a 30% chance. This might be too high if PERIOD_BOMB is fast.
                               // Let's assume 30% is intended.
        bombs[i].x = rand() % (SCREEN_WIDTH - BOMB_WIDTH);  // Random X position within screen width, accounting for bomb width
        bombs[i].y = 0; // Start bombs from the very top of the screen
        bombs[i].active = 1; // Mark bomb as active
        drawBomb(bombs[i].x, bombs[i].y); // Draw the newly spawned bomb immediately
        break; // Spawn only one new bomb per tickBomb call to prevent sudden flood
      }
    }
  }

  // Update all active bombs (movement and collision detection)
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (bombs[i].active) { // If this bomb is active
      // Store current bomb position for collision check and clearing BEFORE moving
      int currentBombX = bombs[i].x;
      int currentBombY = bombs[i].y;

      // Clear the bomb from its current position before moving it
      // Define a slightly larger clearing area to prevent trails, especially if bomb speed > bomb dimension.
      // Y-clearing should ideally cover from current Y to current Y + speed.
      // The current clearing logic seems to be clearing around the *current* position.
      int clearTop = (currentBombY - 2 < 0) ? 0 : currentBombY - 2; // Clear a bit above
      // int clearBottom = currentBombY + BOMB_HEIGHT + 2; // Clear a bit below. Original
      int clearBottom = currentBombY + BOMB_HEIGHT + 6; // Make sure to clear where it was and where it will be (if speed is 6)
      if (clearBottom >= 160) clearBottom = 159;

      sprite(currentBombX, currentBombX + BOMB_WIDTH - 1, currentBombY, clearBottom, 0x0000); // Clear with black

      // Move bomb down
      bombs[i].y += 6; // Bomb falling speed (6 pixels per tickBomb call)

      // Define Player Hitbox (can be smaller than visual sprite for more forgiving collisions)
      int playerInsetX = 3; // Horizontal inset from player sprite edges for hitbox

      // Player's visual Y-coordinates are CHAR_Y (140) to CHAR_Y + CHAR_HEIGHT - 1 (159)
      // Defining a more specific hitbox within the player sprite
      // Let's use the middle section of the player visually for collision.
      // Original: int playerHitboxVisualTop = CHAR_Y + 3; // Top of player's "vulnerable" area
      // Original: int playerHitboxVisualBottom = CHAR_Y + 7; // Bottom of player's "vulnerable" area
      // This makes the player hitbox quite small vertically (5 pixels tall: 143-147).
      // Let's use the main body of the player for collision.
      // Player main body by drawP1At: y=140 to y=153 (yellow part)
      int playerTop = CHAR_Y;    // Top of player hitbox
      int playerBottom = CHAR_Y + CHAR_HEIGHT - 1 - 6; // Bottom of player hitbox (e.g. not the very feet)
                                                         // Making it 153 to match the yellow body.
      playerBottom = 153; // Based on yellow sprite part

      int playerLeft = xPos + playerInsetX; // Left edge of player hitbox
      int playerRight = xPos + CHAR_WIDTH - 1 - playerInsetX; // Right edge of player hitbox

      // Bomb Hitbox definition
      // Effective collision point for the bomb could be its leading edge (bottom).
      // Or, use the full bomb body. Let's use the full bomb body for now.
      int bombTop = bombs[i].y;
      int bombBottom = bombs[i].y + BOMB_HEIGHT - 1;

      // The original code had complex bomb hitbox logic, seems to be an attempt at a very specific point.
      // Let's simplify for a standard bounding box collision first.
      // int bombEffectiveCollisionY = bombs[i].y + BOMB_HEIGHT - 1;
      // ... (original complex bomb hitbox calculation) ...
      // Using simpler bomb hitbox:
      int bombLeft = bombs[i].x;
      int bombRight = bombs[i].x + BOMB_WIDTH - 1;


      // Ensure hitbox coordinates are not inverted (e.g. if inset is too large)
      if (playerRight < playerLeft) playerRight = playerLeft;
      if (playerBottom < playerTop) playerBottom = playerTop;
      // No need for bombRight < bombLeft check if bombInsetX is not used or is small.

      // Collision Check: AABB (Axis-Aligned Bounding Box)
      // Check for overlap between player hitbox and bomb hitbox
      bool collision = false;
      if (playerRight >= bombLeft &&    // Player's right edge is to the right of bomb's left edge
          playerLeft <= bombRight &&    // Player's left edge is to the left of bomb's right edge
          playerBottom >= bombTop &&    // Player's bottom edge is below bomb's top edge
          playerTop <= bombBottom) {    // Player's top edge is above bomb's bottom edge
        collision = true;
      }

      if (collision) {
        resetGame();      // Call resetGame function to handle collision (game over, reset score, etc.)
        // No need to redraw this specific bomb as resetGame clears all bombs.
        // The 'return BOMBIDLE' suggests the state machine might have more distinct roles for states,
        // but currently, it mostly acts as a continuous loop when game is active.
        return BOMBIDLE; // Exit tickBomb for this cycle as game is resetting.
      }

      // If bomb has moved off the bottom of the screen
      if (bombs[i].y >= 160) { // If top of bomb is at or below screen bottom edge
        bombs[i].active = 0; // Deactivate the bomb, making it available for reuse
        // No need to explicitly clear it here if it's fully off-screen.
        // The previous clear before moving should handle most of it.
      } else {
        // If bomb is still on screen and no collision, draw it at its new position
        drawBomb(bombs[i].x, bombs[i].y);
      }
    }
  }
  // If states were used more distinctly, 'state' might change here.
  // For now, it implies BOMBIDLE or BOMBSEND is largely the same continuous processing.
  return state; // Return current state
}


int main(void)
{
  // Configure Data Direction Registers (DDR) and initial PORT states for I/O pins
  // DDRx = 0xFF means all pins of Port x are outputs
  // DDRx = 0x00 means all pins of Port x are inputs
  // PORTx = 0x00 means all output pins of Port x are initially low (or pull-ups disabled for inputs)

  DDRB = 0xFF;  // Set PORTB as output (likely for SPI: SCK, MOSI, and PB7 for buzzer)
  PORTB = 0x00; // Initialize PORTB outputs to low

  DDRH = 0xFF;  // Set PORTH as output (likely for TFT: PIN_A0/DC)
  PORTH = 0x00; // Initialize PORTH outputs to low

  DDRG = 0xFF;  // Set PORTG as output (likely for TFT: PIN_RESET)
  PORTG = 0x00; // Initialize PORTG outputs to low

  DDRE = 0xFF;  // Set PORTE as output (likely for TFT: PIN_CS)
  PORTE = 0x00; // Initialize PORTE outputs to low

  DDRF = 0x00;  // Set PORTF as input (for ADC joystick and switch)
  DDRF &= ~(1 << PF2); // Explicitly set PF2 (joystick switch pin) as input. Redundant if DDRF=0x00.
  PORTF = 0x00; // Initialize PORTF. For inputs, this means pull-up resistors are disabled.
  PORTF |= (1 << PF2); //Enable internal pull-up resistor for PF2 (joystick switch).
                       //This means if the switch is open, PF2 reads high. If switch grounds PF2, it reads low.
                       //tickJoy uses `!GetBit(PINF, 2)`, so p1SW is 1 if PF2 is low (pressed). This is correct with pull-up.


  ADC_init();   // Initialize the Analog-to-Digital Converter
  SPI_INIT();   // Initialize SPI communication
  ST7735_init();// Initialize the ST7735 TFT display
  fillScreen(0x0000); // Clear the entire screen to black at the start

  // Initialize tasks for the scheduler
  unsigned char i = 0; // Index for tasks array

  // Task 0: Joystick Input
  tasks[i].period = PERIOD_JOYSTICK;    // Set period
  tasks[i].state = JOYINIT;             // Set initial state
  tasks[i].elapsedTime = PERIOD_JOYSTICK; // Initialize elapsedTime to period to run first time immediately
  tasks[i].TickFct = &tickJoy;          // Assign tick function
  i++; // Increment task index

  // Task 1: Player Movement
  tasks[i].period = PERIOD_MOVE;
  tasks[i].state = MOVEIDLE;
  tasks[i].elapsedTime = PERIOD_MOVE;
  tasks[i].TickFct = &tickMove;
  i++;

  // Task 2: Score Update
  tasks[i].period = PERIOD_SCORE;
  tasks[i].state = SCORERESET; // Start in SCORERESET to initialize score to 0
  tasks[i].elapsedTime = PERIOD_SCORE;
  tasks[i].TickFct = &tickScore;
  i++;

  // Task 3: Game State Management (Pause/Play)
  tasks[i].period = PERIOD_GAMESTATE;
  tasks[i].state = GAMEINIT; // Start in GAMEINIT, which will transition to PAUSE
  tasks[i].elapsedTime = PERIOD_GAMESTATE;
  tasks[i].TickFct = &tickGameState;
  i++;

  // Task 4: Bomb Management
  tasks[i].period = PERIOD_BOMB;
  tasks[i].state = BOMBIDLE; // Initial state for bomb logic
  tasks[i].elapsedTime = PERIOD_BOMB;
  tasks[i].TickFct = &tickBomb;
  // i++; // Not needed if this is the last task, but good practice if more are added

  // Initialize and start the timer for the task scheduler
  TimerSet(PERIOD_GCD); // Set the timer interrupt period to the GCD (1ms)
  TimerOn();            // Enable the timer interrupt

  // Main loop: The program will stay in this loop indefinitely.
  // All application logic is handled by the timer ISR and the scheduled tasks.
  while(1){}

  return 0; // This line will never be reached in an embedded system with an infinite loop.
}
