#ifndef SPIAVR_H
#define SPIAVR_H
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
//B5 should always be SCK(spi clock) and B3 should always be MOSI. If you are usingan
//SPI peripheral that sends data back to the arduino, you will need to use B4 asthe MISO pin.
//The SS pin can be any digital pin on the arduino. Right before sending an 8 bitvalue with
//the SPI_SEND() funtion, you will need to set your SS pin to low. If you havemultiple SPI
//devices, they will share the SCK, MOSI and MISO pins but should have different SSpins.
//To send a value to a specific device, set it's SS pin to low and all other Spins to high.
// Outputs, pin definitions
#define PIN_SCK PORTB1//SHOULD ALWAYS BE B5 ON THE ARDUINO pin 52
#define PIN_MOSI PORTB2//SHOULD ALWAYS BE B3 ON THE ARDUINO
#define PIN_A0 PORTH3 //AKA PIN_DC; pin 6
#define PIN_RESET PORTG5
#define PIN_CS PORTE5 //AKA PIN_CS;
//If SS is on a different port, make sure to change the init to take that intoaccount.
void SPI_INIT(){
DDRB |= (1 << PIN_SCK) | (1 << PIN_MOSI);//initialize yourpins.
DDRE |= (1<< PIN_CS);
SPCR |= (1 << SPE) | (1 << MSTR); //initialize SPI coomunication
}
void SPI_SEND(char data)
{
SPDR = data;//set data that you want to transmit
while (!(SPSR & (1 << SPIF)));// wait until done transmitting
}
#endif /* SPIAVR_H */
