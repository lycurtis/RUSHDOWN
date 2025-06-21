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
  Send_Command(CASET); //Set column range
  Send_Data(0x00); Send_Data(XS);
  Send_Data(0x00); Send_Data(XE);

  Send_Command(RASET); //Set row range
  Send_Data(0x00); Send_Data(YS);
  Send_Data(0x00); Send_Data(YE);

  Send_Command(RAMWR); //Write to RAM

  int numPixels = (XE - XS + 1) * (YE - YS + 1);
  for (int i = 0; i < numPixels; i++) 
  {
    Send_Data(color565 >> 8); //High byte
    Send_Data(color565 & 0xFF); //Low byte
  }
}

void drawP1At(int x) {
  uint16_t yellow = Color565(255, 255, 0);
  uint16_t white  = Color565(255, 255, 255); 
  uint16_t blue   = Color565(0, 0, 255); 
  uint16_t gray   = Color565(160, 160, 160); 
  uint16_t black  = Color565(0, 0, 0); 

  sprite(x, x + 13, 140, 153, yellow); 
  sprite(x + 3, x + 10, 143, 147, gray); 
  sprite(x + 4, x + 9, 144, 146, white); 
  sprite(x + 5, x + 8, 143, 147, white); 
  sprite(x + 6, x + 7, 144, 145, black); 
  sprite(x, x + 13, 150, 153, blue); 
  sprite(x, x + 2, 154, 159, blue); 
  sprite(x + 11, x + 13, 154, 159, blue); 
}

void drawDigit(uint8_t digit, uint8_t x, uint8_t y, uint16_t color, uint8_t scale) {
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

  for (uint8_t row = 0; row < 5; row++) {
    for (uint8_t col = 0; col < 3; col++) {
      if (digits[digit][row] & (1 << (2 - col))) {
        //Scale the individual pixel
        sprite(
          x + col * scale,
          x + (col + 1) * scale - 1,
          y + row * scale,
          y + (row + 1) * scale - 1,
          color
        );
      }
    }
  }
}

void drawScore(uint16_t score) {
  uint8_t x = 2;
  uint8_t y = 2;
  uint8_t scale = 2;
  uint16_t color = Color565(255, 255, 255); 

  sprite(0, 40, 0, 20, 0x0000); //Clear previous area 

  uint16_t temp = score;
  uint8_t digits[3] = {0, 0, 0};

  for (int i = 2; i >= 0 && temp > 0; i--) {
    digits[i] = temp % 10;
    temp /= 10;
  }

  uint8_t xpos = x;
  for (int i = 0; i < 3; i++) {
    bool shouldDraw = 1;

    if (i == 0 && digits[i] == 0 && score < 100) {
      xpos += 4 * scale;
      shouldDraw = 0;
    } else if (i == 1 && digits[i] == 0 && score < 10) {
      xpos += 4 * scale;
      shouldDraw = 0;
    }

    if (shouldDraw) {
      drawDigit(digits[i], xpos, y, color, scale);
      xpos += 4 * scale + 1; //spacing between digits
    }
  }
}

#define BOMB_WIDTH  12
#define BOMB_HEIGHT 12
void drawBomb(int x, int y) {
  uint16_t red = Color565(255, 0, 0);
  uint16_t orange = Color565(255, 165, 0);

  sprite(x, x + BOMB_WIDTH - 1, y, y + (BOMB_HEIGHT / 2) - 1, orange); //Top half (orange)
  sprite(x, x + BOMB_WIDTH - 1, y + (BOMB_HEIGHT / 2), y + BOMB_HEIGHT - 1, red); //Bottom half (red)
}

//Task struct for concurrent synchSMs implmentations
typedef struct _task{
	signed 	 char state; 		//Task's current state
	unsigned long period; 		//Task period
	unsigned long elapsedTime; 	//Time elapsed since last task tick
	int (*TickFct)(int); 		//Task tick function
} task;

const unsigned long PERIOD_JOYSTICK = 50;
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
        if (xPos > XPOS_MAX) xPos = XPOS_MAX; //Edge Guard
      }
    break;

    case MOVELEFT:
      if(xPos > XPOS_MIN){
        sprite(xPos, xPos + 13, 140, 159, 0x0000); //Clear previous
        xPos -= 9; //Step size(Aka speed)
        if (xPos < XPOS_MIN) xPos = XPOS_MIN; //edge guard
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
#define CHAR_Y 140 
#define CHAR_HEIGHT 20 
typedef struct {
  int x;
  int y;
  uint8_t active;
} Bomb;
Bomb bombs[MAX_BOMBS]; 
void resetGame() {
    int playerXAtDeath = xPos;

    gameState = 0; //Pause the game

    sprite(playerXAtDeath, playerXAtDeath + CHAR_WIDTH - 1, CHAR_Y, CHAR_Y + CHAR_HEIGHT - 1, 0x0000); 

    //Reset player position
    xPos = (SCREEN_WIDTH - CHAR_WIDTH) / 2; 

    p1Score = 0;
    drawScore(p1Score); 

    //Deactivate and clear all bombs
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (bombs[i].active) {
            int clearTop;
            if(bombs[i].y - 6 < 0){
              clearTop = 0;
            }
            else{
              clearTop = bombs[i].y - 6;
            }
            int clearBottom = bombs[i].y + 22; 
            sprite(bombs[i].x, bombs[i].x + BOMB_WIDTH - 1, clearTop, clearBottom, 0x0000);
            bombs[i].active = 0;
        }
    }

    drawP1At(xPos);
}
enum BOMB_STATES{BOMBIDLE, BOMBSEND};
int tickBomb(int state)
{
  if(gameState == 0){ //make sure to not send bombs while paused
    return state;
  }
  
  //activate a new bomb if there's an inactive one
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (!bombs[i].active && (rand() % 10 < 3)) { //30% chance to spawn per cycle
      bombs[i].x = rand() % (SCREEN_WIDTH - BOMB_WIDTH);  
      bombs[i].y = 0; //tart bombs from the top
      bombs[i].active = 1;
      break; //Spawn one per tick max
    }
  }

  //Update all active bombs
  for (int i = 0; i < MAX_BOMBS; i++) {
    if (bombs[i].active) {
      //Store current bomb position for collision check 
      int currentBombX = bombs[i].x;
      int currentBombY = bombs[i].y;

      int clearTop; 
      if(currentBombY - 2 < 0){
        clearTop = 0;
      }
      else{
        currentBombY - 2;
      }
      int clearBottom = currentBombY + BOMB_HEIGHT + 2;
      sprite(currentBombX, currentBombX + BOMB_WIDTH - 1, clearTop, clearBottom, 0x0000);

      //Move bomb down
      bombs[i].y += 6; //speed

      //Define Player Hitbox (eye)
      int playerInsetX = 3; 

      int playerHitboxVisualTop = CHAR_Y + 3;   
      int playerHitboxVisualBottom = CHAR_Y + 7; 

      int playerLeft = xPos + playerInsetX;
      int playerRight = xPos + CHAR_WIDTH - 1 - playerInsetX;
      int playerTop = playerHitboxVisualTop;
      int playerBottom = playerHitboxVisualBottom;

      //Bomb Hitbox 
      int bombEffectiveCollisionY = bombs[i].y + BOMB_HEIGHT - 1;

      int bombVerticalHitboxSliceHeight = 2; 
      int bombTop = bombEffectiveCollisionY - 1;
      int bombBottom = bombEffectiveCollisionY;

      if (bombVerticalHitboxSliceHeight == 1) {
          bombTop = bombEffectiveCollisionY;
          bombBottom = bombEffectiveCollisionY;
      } else {
          bombTop = bombEffectiveCollisionY - (bombVerticalHitboxSliceHeight / 2);
          bombBottom = bombEffectiveCollisionY + (bombVerticalHitboxSliceHeight / 2 - (bombVerticalHitboxSliceHeight % 2 == 0 ? 1: 0) );
      }

      int bombHorizontalInset = 2; 
      int bombLeft = bombs[i].x + bombHorizontalInset;
      int bombRight = bombs[i].x + BOMB_WIDTH - 1 - bombHorizontalInset;

      if (playerRight < playerLeft){
        playerRight = playerLeft;
      } 
      if (playerBottom < playerTop){
        playerBottom = playerTop; 
      }  
      if (bombRight < bombLeft){
        bombRight = bombLeft;
      } 
      if (bombBottom < bombTop){
        bombBottom = bombTop; 
      } 

      //Collision Check
      if (bombRight >= playerLeft && bombLeft <= playerRight &&  //Horizontal overlap
          bombBottom >= playerTop && bombTop <= playerBottom) { //Vertical overlap
        
        resetGame(); 
        return BOMBIDLE; 
      }

      if ((bombs[i].y - 6) > 160) { 
        bombs[i].active = 0;
      } else {
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
  DDRF &= ~(1 << PF2); //Set PF2 as input
  PORTF = 0x00; //No pull up so tickJoy can read properly
  PORTF |= (1 << PF2); //enable pull-up resistor for tickJoy SW

  ADC_init();
  SPI_INIT();
  ST7735_init();
  fillScreen(0x0000); //Clear screen to black

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

  while(1){}

  return 0;
}
