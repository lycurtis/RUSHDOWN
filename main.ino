#include "timerISR.h"
#include "helper.h"
#include "spiAVR.h"
#include "irAVR.h"
//#include "LCD.h"
#include "periph.h"
//#include "serialATmega.h"
#include "stdlib.h" //needed for rand() function
#include "color.h"

//128*160
// LED
// PIN_SCK PORTB1
// PIN_MOSI PORTB2
// PIN_A0 PORTH3
// PIN_RESET PORTG5
// PIN_CS PORTE5
// GND
// VCC
#define SWRESET 0x01 //Software RESET
#define SLPOUT 0x11 //Sleep out
#define COLMOD 0x3A //Interface Pixel Format
#define DISPON 0x29 //Display On
#define CASET 0x2A //Column Address Set
#define RASET 0x2B //Row Address Set
#define RAMWR 0x2C //Memory Write

//GLOBAL VARIABLES
unsigned char gameState = 0; //0: Pause, 1: Play
//Player 1 Joystick
unsigned short p1Joy;
unsigned short p1SW;
unsigned char p1JoyState; //0: IDLE, 1: RIGHT, 2:LEFT

//Player 1 Character
#define CHAR_WIDTH 14
#define SCREEN_WIDTH 128
#define XPOS_MIN 0
#define XPOS_MAX (SCREEN_WIDTH - CHAR_WIDTH) // 114
int xPos = (SCREEN_WIDTH - CHAR_WIDTH) / 2; // start centered
int p1Score = 0;



void HardwareReset()
{
  //Reset: 4-D4-PG5
  PORTG &= ~(1 << PIN_RESET); //low
  _delay_ms(200);
  PORTG |= (1 << PIN_RESET); //High
  _delay_ms(200);
}

void Send_Command(char cmd)
{
  PORTH &= ~(1 << PIN_A0); //A0 for low command
  PORTE &= ~(1 << PIN_CS); //CS low to select TFT
  SPI_SEND(cmd);
  PORTE |= (1 << PIN_CS); //CS high to deselect
}

void Send_Data(char data)
{
  PORTH |= (1 << PIN_A0); //A0 high for data
  PORTE &= ~(1 << PIN_CS); //CS low to select TFT
  SPI_SEND(data);
  PORTE |= (1 << PIN_CS); //CS high to deselct 
}

void ST7735_init()
{
  HardwareReset();
  
  Send_Command(SWRESET);
  _delay_ms(150);

  Send_Command(SLPOUT);
  _delay_ms(200);

  Send_Command(COLMOD);
  Send_Data(0x05); //set 16 bit color mode
  _delay_ms(10);

  Send_Command(DISPON);
  _delay_ms(200);
}

uint16_t Color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

//row-horizontal (128)
//columns-vertical (160)
void fillScreen(uint16_t color){
  Send_Command(CASET); //set column address
  Send_Data(0x00); Send_Data(0x00); //start at 0
  Send_Data(0x00); Send_Data(0x80); // end at 128

  Send_Command(RASET); //set row address
  Send_Data(0x00); Send_Data(0x00); //start at 0
  Send_Data(0x00); Send_Data(0xA0); // end at 160

  Send_Command(RAMWR); //write RAM

  for(int i=0; i<128*160; i++){
    Send_Data(color >> 8); //Upper byte
    Send_Data(color & 0xFF); //Lower byte
  }
}

void sprite(int XS, int XE, int YS, int YE, uint16_t color565)
{
  Send_Command(CASET); // Set column range
  Send_Data(0x00); Send_Data(XS);
  Send_Data(0x00); Send_Data(XE);

  Send_Command(RASET); // Set row range
  Send_Data(0x00); Send_Data(YS);
  Send_Data(0x00); Send_Data(YE);

  Send_Command(RAMWR); // Write to RAM

  int numPixels = (XE - XS + 1) * (YE - YS + 1);
  for (int i = 0; i < numPixels; i++) 
  {
    Send_Data(color565 >> 8);     // High byte
    Send_Data(color565 & 0xFF);   // Low byte
  }
}

void drawP1At(int x) {
  uint16_t yellow = Color565(255, 255, 0); // Body
  uint16_t white  = Color565(255, 255, 255); // Eye
  uint16_t blue   = Color565(0, 0, 255); // Overalls
  uint16_t gray   = Color565(160, 160, 160); // Goggle
  uint16_t black  = Color565(0, 0, 0); // Pupil

  sprite(x, x + 13, 140, 153, yellow); // Body
  sprite(x + 3, x + 10, 143, 147, gray); // Goggle
  sprite(x + 4, x + 9, 144, 146, white); // Eye white
  sprite(x + 5, x + 8, 143, 147, white); // Eye padding
  sprite(x + 6, x + 7, 144, 145, black); // Pupil
  sprite(x, x + 13, 150, 153, blue); // Overalls top
  sprite(x, x + 2, 154, 159, blue); // Left leg
  sprite(x + 11, x + 13, 154, 159, blue); // Right leg
}

void drawDigit(uint8_t digit, uint8_t x, uint8_t y, uint16_t color) {
  //3x5 pixel digit patterns 
  static const uint8_t digits[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
    {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
    {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
    {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
    {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
    {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
    {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
    {0b111, 0b001, 0b010, 0b100, 0b100}, // 7
    {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
    {0b111, 0b101, 0b111, 0b001, 0b111}, // 9
  };

  for (uint8_t row = 0; row < 5; row++) {
    for (uint8_t col = 0; col < 3; col++) {
      if (digits[digit][row] & (1 << (2 - col))) {
        sprite(x + col, x + col, y + row, y + row, color);
      }
    }
  }
}

void drawScore(uint16_t score) {
  uint8_t x = 2;
  uint8_t y = 2;
  uint16_t color = Color565(255, 255, 255); // White

  // Clear area (assumes max 3 digits)
  sprite(0, 30, 0, 10, 0x0000); // Clear previous

  // Draw digits right to left
  uint16_t temp = score;
  uint8_t digits[3] = {0, 0, 0};
  int i = 2;
  do {
    digits[i--] = temp % 10;
    temp /= 10;
  } while (temp > 0 && i >= 0);

  uint8_t xpos = x;
  for (i = 0; i < 3; i++) {
    if (i == 0 && digits[i] == 0 && score < 100) {
      xpos += 4; // Skip leading zero
      continue;
    }
    if (i == 1 && digits[i] == 0 && score < 10) {
      xpos += 4;
      continue;
    }
    drawDigit(digits[i], xpos, y, color);
    xpos += 5;
  }
}

void drawBomb(int x, int y) {
  uint16_t black = Color565(0, 0, 0);
  uint16_t red   = Color565(255, 0, 0);
  uint16_t orange = Color565(255, 165, 0);

  // Main body: 24x24 rounded shape
  sprite(x + 6,  x + 17, y,     y,     black);  // top
  sprite(x + 4,  x + 19, y + 1, y + 1, black);
  sprite(x + 2,  x + 21, y + 2, y + 20, black); // middle bulk
  sprite(x + 4,  x + 19, y + 21, y + 21, black);
  sprite(x + 6,  x + 17, y + 22, y + 22, black); // bottom

  // Fuse (larger and offset upward)
  sprite(x + 10, x + 12, y - 6, y - 6, red);     // fuse tip
  sprite(x + 10, x + 12, y - 5, y - 5, orange);  // fuse base
}





//Task struct for concurrent synchSMs implmentations
typedef struct _task{
	signed 	 char state; 		//Task's current state
	unsigned long period; 		//Task period
	unsigned long elapsedTime; 	//Time elapsed since last task tick
	int (*TickFct)(int); 		//Task tick function
} task;

const unsigned long PERIOD_JOYSTICK = 55;
const unsigned long PERIOD_MOVE = 50;
const unsigned long PERIOD_SCORE = 1000;
const unsigned long PERIOD_GAMESTATE = 100;
const unsigned long PERIOD_BOMB = 45;
const unsigned long PERIOD_GCD = 1;

#define NUM_TASKS 5
task tasks[NUM_TASKS];

void TimerISR() {
	for ( unsigned int i = 0; i < NUM_TASKS; i++ ) {                   // Iterate through each task in the task array
		if ( tasks[i].elapsedTime == tasks[i].period ) {           // Check if the task is ready to tick
			tasks[i].state = tasks[i].TickFct(tasks[i].state); // Tick and set the next state for this task
			tasks[i].elapsedTime = 0;                          // Reset the elapsed time for the next tick
		}
		tasks[i].elapsedTime += PERIOD_GCD;                        // Increment the elapsed time by PERIOD_GCD
	}
}

enum JOY_STATES{JOYINIT, JOYREAD};
int tickJoy(int state)
{
  //Transitions
  switch(state)
  {
    case JOYINIT:
      state = JOYREAD;
    break;

    case JOYREAD:
      state = JOYREAD;
    break;

    default:
      state = JOYREAD;
    break;
  }

  //Actions
  switch(state)
  {
    case JOYINIT:
    break;

    case JOYREAD:
      p1Joy = ADC_read(0); //Player 1 tickJoy Read
      p1SW = !GetBit(PINF, 2); //By default joystick is high, so we want the reverse
      
      if(p1Joy<=600 && p1Joy>=400){
        p1JoyState = 0; //IDLE Position
        PORTB &= ~(1 << 7);
      }
      else if(p1Joy<400){
        p1JoyState = 1; //RIGHT Position
        PORTB |= (1 << 7); //BUZZER SOUND
      }
      else if(p1Joy>600){
        p1JoyState = 2; //LEFT Position
        PORTB |= (1 << 7); //BUZZER SOUND
      }
    break;
  }

  return state;
}

enum MOVE_STATES{MOVEIDLE, MOVERIGHT, MOVELEFT};
int tickMove(int state) 
{
  if(gameState == 0){ //player cannot move during puase time
    drawP1At(xPos);
    return MOVEIDLE;
  }

  //Transitions
  switch(state)
  {
    case MOVEIDLE:
      if(p1JoyState == 1){
        state = MOVERIGHT;
      }
      else if (p1JoyState == 2){
        state = MOVELEFT;
      }
      else if  (p1JoyState == 0){
        state = MOVEIDLE;
      }
    break;

    case MOVERIGHT:
      state = MOVEIDLE;
    break;

    case MOVELEFT:
      state = MOVEIDLE;
    break;

    default:
      state = MOVEIDLE;
    break;
  }

  //Actions
  switch(state)
  {
    case MOVEIDLE:
    break;

    case MOVERIGHT:
      if(xPos < XPOS_MAX){
        sprite(xPos, xPos + 13, 140, 159, 0x0000); //Clear previous
        xPos += 9; //Step size (Aka speed)
        if (xPos > XPOS_MAX) xPos = XPOS_MAX; // Clamp
      }
    break;

    case MOVELEFT:
      if(xPos > XPOS_MIN){
        sprite(xPos, xPos + 13, 140, 159, 0x0000); //Clear previous
        xPos -= 9; //Step size(Aka speed)
        if (xPos < XPOS_MIN) xPos = XPOS_MIN; // Clamp
      }
    break;
  }
  drawP1At(xPos);
  return state;
}

enum SCORE_STATES{SCORERESET, SCORECOUNT};
int tickScore(int state)
{
  if(gameState == 0){ //Score is still during pause time
    return state;
  }

  //Transitions
  switch(state)
  {
    case SCORERESET:
      state = SCORECOUNT;
    break;

    case SCORECOUNT:
      
    break;

    default:
      state = SCORECOUNT;
    break;
  }

  //Actions
  switch(state)
  {
    case SCORERESET:
      
    break;

    case SCORECOUNT:
      p1Score++;
      drawScore(p1Score);
    break;

    default:
      state = SCORECOUNT;
    break;
  }
  return state;
}

enum GAMESTATE_STATES{GAMEINIT, PAUSE, PAUSEPRESS, PLAY, PLAYPRESS};
int tickGameState(int state)
{
  //Transitions
  switch(state)
  {
    case GAMEINIT:
      state = PAUSE;
    break;

    case PAUSE:
      if(p1SW == 1){
        state = PAUSEPRESS;
      }
      else if(p1SW==0){
        state = PAUSE;
      }
    break;

    case PAUSEPRESS:
      if(p1SW == 0){
        state = PLAY;
      }
      else if(p1SW == 1){
        state = PAUSEPRESS;
      }
    break;

    case PLAY:
      if(p1SW == 1){
        state = PLAYPRESS;
      }
      else if(p1SW == 0){
        state = PLAY;
      }
    break;

    case PLAYPRESS:
      if(p1SW==0){
        state = PAUSE;
      }
      else if(p1SW == 1){
        state = PLAYPRESS;
      }
    break;

    default:
      state = PAUSE;
    break;
  }

  //Actions
  switch(state)
  {
    case GAMEINIT:
    break;

    case PAUSE:
      gameState = 0;
    break;

    case PAUSEPRESS:
    break;

    case PLAY:
      gameState = 1;
    break;

    case PLAYPRESS:
    break;
  }
  return state;
}

#define MAX_BOMBS 4
#define CHAR_Y 140 // Player's visual top Y coordinate
#define CHAR_HEIGHT 20 // Player's visual height (140 to 159)
typedef struct {
  int x;
  int y;
  uint8_t active;
} Bomb;
Bomb bombs[MAX_BOMBS]; 
#define BOMB_WIDTH  22
#define BOMB_HEIGHT 29
void resetGame() {
    // Store the player's X position at the moment of death
    int playerXAtDeath = xPos;

    gameState = 0; // Pause the game

    // ---- NEW: Clear the player's sprite from its last known position ----
    // The player's visual extent is from Y=CHAR_Y to Y=(CHAR_Y + CHAR_HEIGHT - 1),
    // and width is CHAR_WIDTH.
    // We use the same coordinates that tickMove uses for clearing.
    sprite(playerXAtDeath, playerXAtDeath + CHAR_WIDTH - 1, CHAR_Y, CHAR_Y + CHAR_HEIGHT - 1, 0x0000); // 0x0000 is black (background)

    // Reset player position
    xPos = (SCREEN_WIDTH - CHAR_WIDTH) / 2; // Center the player

    // Reset player score
    p1Score = 0;
    drawScore(p1Score); // Redraw score as 0

    // Deactivate and clear all bombs
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (bombs[i].active) {
            // Clear the bomb's last visual position
            int clearTop = (bombs[i].y - 6 < 0) ? 0 : bombs[i].y - 6; // Fuse is at y-6
            int clearBottom = bombs[i].y + 22; // Bomb body extends to y+22
            // Use BOMB_WIDTH for clearing the bomb sprite
            sprite(bombs[i].x, bombs[i].x + BOMB_WIDTH - 1, clearTop, clearBottom, 0x0000);
            bombs[i].active = 0;
        }
    }

    // Redraw the player at the new reset position
    // This ensures the player appears immediately at the start.
    drawP1At(xPos);

    // Note: Your tickMove function, when gameState is 0, will also call drawP1At(xPos).
    // This is slightly redundant but harmless and ensures the player stays drawn while paused.
}
enum BOMB_STATES{BOMBIDLE, BOMBSEND};
// enum BOMB_STATES{BOMBIDLE, BOMBSEND}; // Already defined
int tickBomb(int state)
{
  if(gameState == 0){ //make sure to not send bombs while paused
    return state;
  }
  
  // Try to activate a new bomb if there's an inactive one
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!bombs[i].active && (rand() % 10 < 3)) { // 30% chance to spawn per cycle
      bombs[i].x = rand() % (SCREEN_WIDTH - BOMB_WIDTH);  // Ensure bomb fits on screen
      bombs[i].y = 0; // Start bombs from the top
      bombs[i].active = 1;
      break; // Spawn one per tick max
    }
  }

  // Update all active bombs
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (bombs[i].active) {
      // Store current bomb position for collision check BEFORE moving
      int currentBombX = bombs[i].x;
      int currentBombY = bombs[i].y;

      // Clear previous visual position of the bomb
      // Fuse is drawn at y-6, main body up to y+22
      int clearTop = (currentBombY - 6 < 0) ? 0 : currentBombY - 6;
      int clearBottom = currentBombY + 22; // Bottom of the bomb's visual body
      sprite(currentBombX, currentBombX + BOMB_WIDTH -1 , clearTop, clearBottom, 0x0000); // Clear based on BOMB_WIDTH

      // Move bomb down
      bombs[i].y += 6; // Adjust speed as desired

      // --- START OF COLLISION DETECTION LOGIC ---

      // Define Player Hitbox (smaller than visual)
      // Player visual X: xPos to xPos + CHAR_WIDTH - 1
      // Player visual Y: CHAR_Y to CHAR_Y + CHAR_HEIGHT - 1
      int playerInsetX = 2;     // Inset from left/right edges (e.g., 2 pixels each side)
      int playerInsetTop = 4;   // Inset from visual top (e.g., 4 pixels)
      int playerInsetBottom = 2;// Inset from visual bottom (e.g., 2 pixels)

      int playerLeft   = xPos + playerInsetX;
      int playerRight  = xPos + CHAR_WIDTH - 1 - playerInsetX;
      int playerTop    = CHAR_Y + playerInsetTop;
      int playerBottom = CHAR_Y + CHAR_HEIGHT - 1 - playerInsetBottom;

      // Define Bomb Hitbox (smaller than visual, focus on main body)
      // Bomb visual X (main body): currentBombX to currentBombX + BOMB_WIDTH - 1
      // Bomb visual Y (main body): currentBombY to currentBombY + 22 (approx.)
      // Fuse is at currentBombY - 6. BOMB_HEIGHT (29) includes fuse.
      int bombInsetX = 5;        // Inset from bomb's left/right edges
      int bombInsetBodyTop = 4;  // Inset from the top of the bomb's *main body* (currentBombY)
      int bombVisualBodyHeight = 23; // Approx. height of the black part of the bomb
      int bombInsetBodyBottom = 4; // Inset from the bottom of the bomb's *main body*

      int bombLeft   = currentBombX + bombInsetX;
      int bombRight  = currentBombX + BOMB_WIDTH - 1 - bombInsetX;
      int bombTop    = currentBombY + bombInsetBodyTop; // Hitbox starts below fuse, into the body
      int bombBottom = currentBombY + bombVisualBodyHeight - 1 - bombInsetBodyBottom;

      // Ensure hitboxes are valid (optional, but good practice if insets are large)
      if (playerRight < playerLeft) playerRight = playerLeft;
      if (playerBottom < playerTop) playerBottom = playerTop;
      if (bombRight < bombLeft) bombRight = bombLeft;
      if (bombBottom < bombTop) bombBottom = bombTop;
      
      // AABB (Axis-Aligned Bounding Box) Collision Check
      // Check against the bomb's position *before* it was redrawn at the new spot,
      // or its new spot bombs[i].y if you prefer collision at the final drawn spot.
      // Using currentBombY (position before this tick's move, but after last tick's draw)
      // or bombs[i].y (position after this tick's move, where it will be drawn this tick)
      // Let's use bombs[i].y for where it *will* be drawn.
      // So re-calculate bomb hitbox with bombs[i].x and bombs[i].y
      
      bombLeft   = bombs[i].x + bombInsetX;
      bombRight  = bombs[i].x + BOMB_WIDTH - 1 - bombInsetX;
      bombTop    = bombs[i].y + bombInsetBodyTop;
      bombBottom = bombs[i].y + bombVisualBodyHeight - 1 - bombInsetBodyBottom;
      
      if (bombRight >= playerLeft && bombLeft <= playerRight &&
          bombBottom >= playerTop && bombTop <= playerBottom) {
        
        resetGame(); // Call the reset game function
        // No need to draw this bomb, game is resetting
        // We might want to ensure the BOMB task goes to a safe state,
        // but resetGame handles deactivating bombs.
        return BOMBIDLE; // Or 'state' if BOMBIDLE is the only active play state
      }
      // --- END OF COLLISION DETECTION LOGIC ---

      // Deactivate if bomb is off screen (using its new Y position)
      // Check if the very top of the bomb (fuse tip) is below the screen
      if ((bombs[i].y - 6) > 160) { 
        bombs[i].active = 0;
        // No need to clear here, it was cleared before move, and it's off-screen now
      } else {
        // If no collision and not off-screen, draw the bomb at its new position
        drawBomb(bombs[i].x, bombs[i].y);
      }
    }
  }
  return state;
}

int main(void)
{
  DDRB = 0xFF;
  PORTB = 0x00;

  DDRH = 0xFF;
  PORTH = 0x00;

  DDRG = 0xFF;
  PORTG = 0x00;

  DDRE = 0xFF;
  PORTE = 0x00;

  DDRF = 0x00;
  DDRF &= ~(1 << PF2); // Set PF2 as input
  PORTF = 0x00; //No pull up so tickJoy can read properly
  PORTF |= (1 << PF2); //enable pull-up resistor for tickJoy SW

  ADC_init();
  SPI_INIT();
  ST7735_init();
  fillScreen(0x0000); // Clear screen to black

  Serial.begin(9600);

  unsigned char i = 0;
  tasks[i].period = PERIOD_JOYSTICK;
  tasks[i].state = JOYINIT;
  tasks[i].elapsedTime = PERIOD_JOYSTICK;
  tasks[i].TickFct = &tickJoy;
  i++;
  tasks[i].period = PERIOD_MOVE;
  tasks[i].state = MOVEIDLE;
  tasks[i].elapsedTime = PERIOD_MOVE;
  tasks[i].TickFct = &tickMove;
  i++;
  tasks[i].period = PERIOD_SCORE;
  tasks[i].state = SCORERESET;
  tasks[i].elapsedTime = PERIOD_SCORE;
  tasks[i].TickFct = &tickScore;
  i++;
  tasks[i].period = PERIOD_GAMESTATE;
  tasks[i].state = GAMEINIT;
  tasks[i].elapsedTime = PERIOD_GAMESTATE;
  tasks[i].TickFct = &tickGameState;
  i++;
  tasks[i].period = PERIOD_BOMB;
  tasks[i].state = BOMBIDLE;
  tasks[i].elapsedTime = PERIOD_BOMB;
  tasks[i].TickFct = &tickBomb;
  
  TimerSet(PERIOD_GCD);
  TimerOn();

  while(1){
    //Serial.println(gameState);
  }

  return 0;
}
