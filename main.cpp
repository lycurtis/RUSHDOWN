/* Ethan Le ele038@ucr.edu
* Discussion Section: 022
* Assignment: Custom Laboratory Project Demo Video #1
* Exercise Description:  
* 
* 
* I acknowledge all content contained herein, excluding template or example code, is my own original work.
*
* Demo Link: https://www.youtube.com/watch?v=TqTUJ_ag7Lw

*
*/
#include "timerISR.h"
#include "helper.h"
#include "spiAVR.h"
#include "irAVR.h"
#include "LCD.h"
#include "periph.h"
#include "serialATmega.h"



// LED
// SCK(SCL) = PB1
// SDA(MOSI) = PB2
// AO = PH3
// Reset = PG5
// CS(SS) = PE5
// GND
// VCC
#define PIN_A0 PORTH3
#define PIN_RESET PORTG5
#define PIN_CS PORTE5

#define SWRESET 0x01
#define SLPOUT 0x11
#define COLMOD 0x3A
#define DISPON 0x29
#define CASET 0x2A
#define RASET 0x2B
#define RAMWR 0x2C





//128*160

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
  PORTH &= ~(1 << PIN_A0);
  PORTE &= ~(1 << PIN_CS);
  SPI_SEND(cmd);
  PORTE |= (1 << PIN_CS);
}

void Send_Data(char data)
{
  PORTH |= (1 << PIN_A0);
  PORTE &= ~(1 << PIN_CS);
  SPI_SEND(data);
  PORTE |= (1 << PIN_CS);
}



void ST7735_init()
{
  HardwareReset();

  Send_Command(SWRESET);
  _delay_ms(150);

  Send_Command(SLPOUT);
  _delay_ms(200);

  Send_Command(COLMOD);
  Send_Data(0x05);
  _delay_ms(10);

  Send_Command(DISPON);
  _delay_ms(200);

}


//row-horizontal (128)
//columns-vertical (160)
void blackbackGround(uint16_t color)
{
  //column
  Send_Command(CASET);
  Send_Data(0x00);
  Send_Data(0x00); //starts at 0
  Send_Data(0x00);
  Send_Data(0x7F); //ends at 127

  //row
  Send_Command(RASET);
  Send_Data(0x00);
  Send_Data(0x00); //starts at 0
  Send_Data(0x00);
  Send_Data(0x9F); //ends at 159

  Send_Command(RAMWR); //write RAM

  for (int i = 0; i < 128 * 160; ++i)
  {
    Send_Data (color >> 8);
    Send_Data (color & 0xFF);  
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




//void TimerISR() {}

void TimerISR() {
	for ( unsigned int i = 0; i < NUM_TASKS; i++ ) {                   // Iterate through each task in the task array
		if ( tasks[i].elapsedTime == tasks[i].period ) {           // Check if the task is ready to tick
			tasks[i].state = tasks[i].TickFct(tasks[i].state); // Tick and set the next state for this task
			tasks[i].elapsedTime = 0;                          // Reset the elapsed time for the next tick
		}
		tasks[i].elapsedTime += GCD_PERIOD;                        // Increment the elapsed time by GCD_PERIOD
	}
}




unsigned short xAxis;

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
      state = joystickInit;
    break;
  }

  switch(state)
  {
    case joystickInit:
      xAxis = 0;
    break;

    //A0-PF0
    case joystickRead:
      xAxis = ADC_read(0);
    break;
  }

  return state;
}



unsigned char lastspriteX = 0;
enum display{displayInit, displayUpdate};

int display(int state)
{
  static unsigned char currentX = 0;
  unsigned char newX;


  switch(state)
  {
    case displayInit:
      state = displayUpdate;
    break;

    case displayUpdate:
      state = displayUpdate;
    break;

    default:
      state = displayInit;
    break;
  }


  switch(state)
  {
    case displayInit:
      lastspriteX = 0;
    break;

    case displayUpdate:
      newX = (xAxis * (128 - 40)) / 1023;
      if (newX > 88) newX = 88;
      sprite(lastspriteX, lastspriteX + 39, 140, 159, Color565(0, 255, 0));
      sprite(newX, newX + 39, 140, 159, Color565(255, 0, 0));
      lastspriteX = newX;
    break;
  }
  return state;
}


// #define DISPLAY_WIDTH 128
// #define SPRITE_WIDTH 20

// enum displayStates { displayInit, displayLeft, displayRight };

// int display(int state)
// {
//   static unsigned char oldX = 0;
//   static unsigned char enteredLeft = 0;
//   static unsigned char prevState = displayInit;
//   unsigned char mappedX;

//   // --- Joystick Mapping (manual, inverted, rounded) ---
//   mappedX = ((1023 - xAxis) * (DISPLAY_WIDTH - SPRITE_WIDTH) + 511) / 1023;

//   // Clamp
//   if (mappedX > DISPLAY_WIDTH - SPRITE_WIDTH)
//     mappedX = DISPLAY_WIDTH - SPRITE_WIDTH;

//   // Optional edge fix: force right edge if joystick is near full up
//   if (xAxis < 50)
//     mappedX = DISPLAY_WIDTH - SPRITE_WIDTH;

//   // --- State Transitions ---
//   switch (state)
//   {
//     case displayInit:
//       oldX = mappedX;
//       state = displayRight;
//       break;

//     case displayLeft:
//     case displayRight:
//       if (xAxis < 400) {
//         if (state != displayRight) {
//           // (Optional) logic entering displayRight
//         }
//         prevState = state;
//         state = displayRight;
//       }
//       else if (xAxis > 600) {
//         if (state != displayLeft) {
//           enteredLeft = 1; // One-time effect flag
//         }
//         prevState = state;
//         state = displayLeft;
//       }
//       break;

//     default:
//       state = displayInit;
//       break;
//   }

//   // --- State Actions ---
//   switch (state)
//   {
//     case displayInit:
//       sprite(mappedX, mappedX + SPRITE_WIDTH - 1, 140, 159, Color565(255, 0, 0));
//       oldX = mappedX;
//       break;

//     case displayLeft:
//       if (mappedX != oldX)
//       {
//         // Erase previous sprite
//         sprite(oldX, oldX + SPRITE_WIDTH - 1, 140, 159, Color565(0, 255, 0));

//         // Draw red sprite
//         sprite(mappedX, mappedX + SPRITE_WIDTH - 1, 140, 159, Color565(255, 0, 0));

//         oldX = mappedX;
//       }
//       break;

//     case displayRight:
//       if (mappedX != oldX)
//       {
//         // Erase previous sprite
//         sprite(oldX, oldX + SPRITE_WIDTH - 1, 140, 159, Color565(0, 255, 0));

//         // Draw red sprite
//         sprite(mappedX, mappedX + SPRITE_WIDTH - 1, 140, 159, Color565(255, 0, 0));

//         oldX = mappedX;
//       }
//       break;
//   }

//   return state;
// }




int main(void)
{

  // //declare ports
  // DDRF = 0x00;
  // PORTF = 0x01;

  // DDRB = 0x06;
  // PORTB = 0x00;

  // DDRH = 0x08;
  // PORTH = 0x00;

  // DDRG = 0x20;
  // PORTG = 0x00;

  // DDRE = 0x20;
  // PORTE = 0x00;

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
  //_delay_ms(100);
  blackbackGround(0x07E0);
  //sprite(20, 59, 30, 69, Color565(255, 0, 0));  // red
  
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

  while(1){}

  return 0;
}
