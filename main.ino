/* Ethan Le ele038@ucr.edu
* Discussion Section: 022
* Assignment: Custom Laboratory Project Demo Video #1
* Exercise Description:  
* 
* 
* I acknowledge all content contained herein, excluding template or example code, is my own original work.
*
* Demo Link: https://www.youtube.com/watch?v=5my8fUszLII
*
*/
#include "timerISR.h"
#include "helper.h"
#include "spiAVR.h"
#include "irAVR.h"
//#include "LCD.h"
#include "periph.h"
//#include "serialATmega.h"

#define SWRESET 0x01 //Software RESET
#define SLPOUT 0x11 //Sleep out
#define COLMOD 0x3A //Interface Pixel Format
#define DISPON 0x29 //Display On
#define CASET 0x2A //Column Address Set
#define RASET 0x2B //Row Address Set
#define RAMWR 0x2C //Memory Write

void HardwareReset() 
{
  //Reset: 
  PORTG &= ~(1 << PIN_RESET); //Low
  _delay_ms(200);
  PORTG |= (1 << PIN_RESET); //High
  _delay_ms(200);
}

void Send_Command(char cmd){
  PORTH &= ~(1 << PIN_A0); //A0 for low command
  PORTE &= ~(1 << PIN_CS); //CS low to select TFT
  SPI_SEND(cmd);
  PORTE |= (1 << PIN_CS); //CS high to deselect
}

void Send_Data(char data){
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


void fillScreen(uint16_t color){
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


//Task struct for concurrent synchSMs implmentations
typedef struct _task{
	signed 	 char state; 		//Task's current state
	unsigned long period; 		//Task period
	unsigned long elapsedTime; 	//Time elapsed since last task tick
	int (*TickFct)(int); 		//Task tick function
} task;


const unsigned long DISPLAYSPRITE_PERIOD = 100;
const unsigned long GCD_PERIOD = 1;


#define NUM_TASKS 1

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

uint16_t Color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}


int main(void)
{

  //declare ports
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
  fillScreen(0x07E0);
  sprite(20, 59, 30, 69, Color565(255, 0, 0));  // red


  //unsigned char i = 0;
  //tasks[i].period = JOYSTICK1_PERIOD;
  //tasks[i].state = joystick1Wait;
  //tasks[i].elapsedTime = tasks[i].period;
  //tasks[i].TickFct = &joystick1;

  TimerSet(GCD_PERIOD);
  TimerOn();

  while(1){}

  return 0;
}