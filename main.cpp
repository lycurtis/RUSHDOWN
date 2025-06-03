#include "timerISR.h"
#include "helper.h"
#include "spiAVR.h"
#include "irAVR.h"
//#include "LCD.h"
#include "periph.h"
//#include "serialATmega.h"

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

unsigned short p1Joy;

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

//row-horizontal (128)
//columns-vertical (160)
void fillScreen(uint16_t topColor, uint16_t bottomColor)
{
  Send_Command(CASET); //set column address
  Send_Data(0x00); Send_Data(0x00); //start at 0
  Send_Data(0x00); Send_Data(0x7F); // end at 127

  Send_Command(RASET); //set row address
  Send_Data(0x00); Send_Data(0x00); //start at 0
  Send_Data(0x00); Send_Data(0x9F); // end at 159

  Send_Command(RAMWR); //write RAM

  for(int i=0; i<128*160; i++){
    Send_Data(color >> 8); //Upper byte
    Send_Data(color & 0xFF); //Lower byte
  }
}

//BGR color order
void black()
{
  SPI_SEND(0);
  SPI_SEND(0);
  SPI_SEND(0);
}

void white()
{
  SPI_SEND(255);
  SPI_SEND(255);
  SPI_SEND(255);
}
void red()
{
  SPI_SEND(0);
  SPI_SEND(0);
  SPI_SEND(255);
}

void green()
{
  SPI_SEND(0);
  SPI_SEND(255);
  SPI_SEND(0);
}

void blue()
{
  SPI_SEND(255);
  SPI_SEND(0);
  SPI_SEND(0);
}

void yellow()
{
  SPI_SEND(0);
  SPI_SEND(255);
  SPI_SEND(255);
}

void setColor(int B, int G, int R)
{
  SPI_SEND(B);
  SPI_SEND(G);
  SPI_SEND(R);
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

uint16_t Color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void enemySprite1 (int startX)
{
  int XS = startX;
  int XE = startX + 9;
  int YS = 0;
  int YE = 9;

  uint16_t blueColor = Color565(0, 0, 255); // bright blue

  // Draw the enemy sprite
  sprite(XS, XE, YS, YE, blueColor);
}

//Task struct for concurrent synchSMs implmentations
typedef struct _task{
	signed 	 char state; 		//Task's current state
	unsigned long period; 		//Task period
	unsigned long elapsedTime; 	//Time elapsed since last task tick
	int (*TickFct)(int); 		//Task tick function
} task;

const unsigned long JOYSTICK_PERIOD = 100;
const unsigned long DISPLAY_PERIOD = 100;
const unsigned long GCD_PERIOD = 1;

#define NUM_TASKS 2
task tasks[NUM_TASKS];

void TimerISR() {
	for ( unsigned int i = 0; i < NUM_TASKS; i++ ) {                   // Iterate through each task in the task array
		if ( tasks[i].elapsedTime == tasks[i].period ) {           // Check if the task is ready to tick
			tasks[i].state = tasks[i].TickFct(tasks[i].state); // Tick and set the next state for this task
			tasks[i].elapsedTime = 0;                          // Reset the elapsed time for the next tick
		}
		tasks[i].elapsedTime += GCD_PERIOD;                        // Increment the elapsed time by GCD_PERIOD
	}
}

enum joystick{joystickInit, joystickRead};

int joystick(int state)
{
  switch(state)
  {
    case joystickInit:
      state = joystickRead;
    break;

    case joystickRead:
      state = joystickRead;
    break;

    default:
      state = joystickRead;
    break;
  }

  switch(state)
  {
    case joystickInit:
    break;

    case joystickRead:
      p1Joy = ADC_read(0);
      Serial.println(p1Joy);
    break;
  }

  return state;
}

// Global sprite properties
unsigned char playerXS = 54;   // Start X (centered)
unsigned char playerXE = 74;   // End X (20 pixels wide)
unsigned char playerYS = 140;  // Bottom of screen
unsigned char playerYE = 159;  // 20 pixels tall

int newXS = 0;
int newXE = 0;

// Joystick X-axis value (updated in a separate task)
//unsigned short xAxis;

enum display { displayInit, displayMove };

int display(int state) {
    unsigned short threshold = 100;   // Dead zone
    unsigned char stepSize = 10;       // Speed of movement

    // State transitions
    switch (state) {
        case displayInit:
            state = displayMove;
            break;

        case displayMove:
            state = displayMove;
            break;

        default:
            state = displayInit;
            break;
    }

    // State actions
    unsigned short xAxis = 0;
    switch (state) 
    {
      case displayInit:
        // Draw sprite for the first time
        sprite(playerXS, playerXE, playerYS, playerYE, 0xF800); // red
        break;

      case displayMove:
        // Erase old sprite
        sprite(playerXS, playerXE, playerYS, playerYE, 0x0000); // black

        // Move LEFT
        if (xAxis < 512 - threshold) 
        {
          newXS = (int)playerXS - stepSize;
          newXE = (int)playerXE - stepSize;

          if (newXS >= 0) 
          {
            playerXS = (unsigned char)newXS;
            playerXE = (unsigned char)newXE;
          } 
          else 
          {
            playerXS = 0;
            playerXE = 20;
          }
        }

        // Move RIGHT
        else if (xAxis > 512 + threshold) 
        {
          newXS = (int)playerXS + stepSize;
          newXE = (int)playerXE + stepSize;

          if (newXE <= 127) 
          {
            playerXS = (unsigned char)newXS;
            playerXE = (unsigned char)newXE;
          } 
          else 
          {
            playerXE = 127;
            playerXS = 127 - 20;
          }
        }

            // Draw sprite at new position
        sprite(playerXS, playerXE, playerYS, playerYE, 0xF800); // red
      break;

      default:
      break;
    }

  return state;
}

// need to create the objects falling 
// 4 objects will fall at random and will be roughly 10*10 to test
// object 1: bullet (5*5)
// object 2: sword (7*7)
// object 3: vase (6*6)
// object 4: plane (25*25)

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

  ADC_init();
  SPI_INIT();
  ST7735_init();
  fillScreen(0x0000, 0x0000);
  enemySprite1(60);

  unsigned char i = 0;
  tasks[0].period = JOYSTICK_PERIOD;
  tasks[0].state = joystickInit;
  tasks[0].elapsedTime = JOYSTICK_PERIOD;
  tasks[0].TickFct = &joystick;
  i++;
  tasks[1].period = DISPLAY_PERIOD;
  tasks[1].state = displayInit;
  tasks[1].elapsedTime = DISPLAY_PERIOD;
  tasks[1].TickFct = &display;
  i++;

  TimerSet(GCD_PERIOD);
  TimerOn();

  while(1){
    //Serial.println(p1Joy);
  }

  return 0;
}
