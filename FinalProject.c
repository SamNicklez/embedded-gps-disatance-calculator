#define BAUD_RATE 9600
#define F_CPU 16000000UL
#define USART_BAUDRATE ((F_CPU/(BAUD_RATE*16UL))-1)
#define RADIUS_EARTH 6371.0 // Earth's radius in kilometers
#define EEPROM_START_ADDR 0x00
#define EEPROM_END_ADDR 0xFF
#define MAX_VALUES 10
#define VALUE_SIZE 4
#include <avr/io.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>


#include <string.h>
#include "LCD.h"
float numLat1 = -91.321982;
float numLong1 = 41.395904;
float numLat2 = -91.6086;
float numLong2 = 41.6960;
int isFirst = 0;
uint16_t counter = 0x00;


/************************************************************************/
/*							EEPROM                                      */
/************************************************************************/
float read_float_from_eeprom(uint16_t address)
{
	float value;
	uint8_t* p_value = (uint8_t*)&value;
	for (uint8_t i = 0; i < sizeof(value); i++) {
		*(p_value + i) = eeprom_read_byte((uint8_t*)(address + i));
	}
	return value;
}

void saveFloatToEEPROM(float value)
{
	// Find the next available EEPROM address to store the value
	uint16_t addr = EEPROM_START_ADDR;
	while (addr <= EEPROM_END_ADDR) {
		eeprom_busy_wait();
		if (eeprom_read_byte((uint8_t*)addr) == 0xFF) {
			// Found an available EEPROM address
			break;
		}
		addr += VALUE_SIZE;
	}
	
	if (addr > EEPROM_END_ADDR) {
		// No available EEPROM address found
		return;
	}
	
	// Store the value at the available EEPROM address
	eeprom_write_float((float*)addr, value);

	// Check if we've stored the maximum number of values
	uint8_t num_values = 0;
	for (uint16_t i = EEPROM_START_ADDR; i <= EEPROM_END_ADDR; i += VALUE_SIZE) {
		eeprom_busy_wait();
		if (eeprom_read_byte((uint8_t*)i) != 0xFF) {
			// This address is being used
			num_values++;
		}
	}

	// If we've stored the maximum number of values, start overwriting the earliest value
	if (num_values >= MAX_VALUES) {
		uint16_t overwrite_addr = EEPROM_START_ADDR;
		eeprom_busy_wait();
		eeprom_write_float((float*)overwrite_addr, value);
	}
}

void clearEEPROM(void)
{
	// Write 0xFF to every EEPROM address to clear the memory
	for (uint16_t addr = EEPROM_START_ADDR; addr <= EEPROM_END_ADDR; addr++) {
		eeprom_busy_wait();
		eeprom_write_byte((uint8_t*)addr, 0xFF);
	}
}

/************************************************************************/
/*						HELPER FUNCTIONS			                    */
/************************************************************************/
float deg2rad(float deg) {
	return deg * (M_PI / 180.0);
}

float haversine(float lat1, float lon1, float lat2, float lon2) {
	float dlat = deg2rad(lat2 - lat1);
	float dlon = deg2rad(lon2 - lon1);
	float a = sin(dlat / 2) * sin(dlat / 2) +
	cos(deg2rad(lat1)) * cos(deg2rad(lat2)) *
	sin(dlon / 2) * sin(dlon / 2);
	float c = 2 * atan2(sqrt(a), sqrt(1 - a));
	return RADIUS_EARTH * c * 3280.84;
}

float roundTens(float var)
{
	float value = (int)(var * 100 + .5);
	return (float)value / 100;
}

/************************************************************************/
/*						   USART FUNCTIONS                              */
/************************************************************************/

//initializer for USART
void USART_Init()
{
    UBRR0H = (uint8_t)(USART_BAUDRATE >> 8); // set baud rate prescaler (high byte)
    UBRR0L = (uint8_t)USART_BAUDRATE; // set baud rate prescaler (low byte)
    UCSR0B = (1 << RXEN0); // enable receiver and receiver interrupt
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // set data frame format: 8 data bits, no parity, 1 stop bit
}

//Receives a single character from the GPS module
unsigned char USART_Receive()
{
	while(!(UCSR0A & (1<<RXC0)));
	return UDR0;
}

//Receives an entire line from the GPS modules
void USART_receive_string(char* str)
{
	int i = 0;
	unsigned char data;
	do
	{
		data = USART_Receive();
		str[i] = data;
		i++;
	}
	while (data != '\n' && data != '\0');
	str[i-1] = '\n';
	str[i] = '\0';
}

/************************************************************************/
/*							INTERRUPTS                                  */
/************************************************************************/
void setup() {
  USART_Init();
  initLCD();
  updateLCDScreen(1, "INITIALIZING", NONE, "NONE");
  // Configure pins 8 and 9 as inputs with pull-up resistors
  DDRB &= ~(1 << DDB0); // pin 8
  DDRB &= ~(1 << DDB1); // pin 9
  PORTB |= (1 << DDB0) | (1 << DDB1); // enable pull-up resistors
  
  // Enable external interrupts for pins 8 and 9
  PCICR |= (1 << PCIE0); // enable PCINT0 interrupt vector
  PCMSK0 |= (1 << PCINT0) | (1 << PCINT1); // enable interrupts for pins 8 and 9
  
    // Set A4 and A5 as input pins with pull-up resistors enabled
    DDRC &= ~(1 << DDC4) & ~(1 << DDC5);
    PORTC |= (1 << PORTC4) | (1 << PORTC5);

    // Enable interrupts on PCINT12 (A4) and PCINT13 (A5)
    PCICR |= (1 << PCIE1);
    PCMSK1 |= (1 << PCINT12) | (1 << PCINT13);
  
  sei();
}

//Handle RPG
ISR(PCINT1_vect){
	cli();
	int fullTurn = 1;
	
	//Pin 5 is low
	
	if(!(PINC & (1 << PINC5))){
		// Pin 4 is high
		if ((PINC & (1 << PINC4))){ // Clockwise Code
			// PC5 went first
			while((PINC & (1 << PINC4))){ // Waits until both are low
			}
			while(!(PINC & (1 << PINC5))){ // Waits until one goes high again
				if ((PINC & (1 << PINC4))){
					fullTurn = 0;
				}
			}
			while (!(PINC & (1 << PINC4))){ //Stays here until both are high again
				
			}
			// Do counter clockwise stuff
			if (fullTurn == 1){
				updateLCDScreen(1, "CCW", NONE, "NONE");
				if(counter == 0x00){
					float answer = read_float_from_eeprom(counter);
					if(isnan(answer)){
						updateLCDScreen(1, "No value", NONE, "NONE");
						updateLCDScreen(2, "In memory", NONE, "NONE");
						_delay_ms(1000);
					}
					else{
						char distance[100];
						sprintf(distance, "value is %.2f", answer);
						updateLCDScreen(1,"At mem loc 0x00", NONE, "NONE");
						updateLCDScreen(2, distance, NONE, "NONE");
						_delay_ms(1000);
					}
				}
				else{
					float answer = read_float_from_eeprom(counter);
					counter -= 4;
					char distance[100];
					char memVal[100];
					sprintf(distance, "value is %.2f", answer);
					sprintf(memVal, "at mem loc %d", counter);
					updateLCDScreen(1,memVal, NONE, "NONE");
					updateLCDScreen(2, distance, NONE, "NONE");
					_delay_ms(1000);
				}
				updateLCDScreen(1,"", NONE, "NONE");
				updateLCDScreen(2, "", NONE, "NONE");
			}
			
		}
		else{
			// PC4 went first
			while(!(PINC & (1 << PINC4))){ // 4 is low, wait till 5 goes low
				if ((PINC & (1 << PINC5))){
					fullTurn = 0;
				}
			}
			while (!(PINC & (1 << PINC5))){ // wait for 4 to go high
				
			}
			if (fullTurn == 1){
				if(counter == 0x40){
					float answer = read_float_from_eeprom(counter);
					if(isnan(answer)){
						updateLCDScreen(1, "No value", NONE, "NONE");
						updateLCDScreen(2, "In memory", NONE, "NONE");
						_delay_ms(1000);
					}
					else{
						char distance[100];
						sprintf(distance, "value is %.2f", answer);
						updateLCDScreen(1,"At mem loc 0x00", NONE, "NONE");
						updateLCDScreen(2, distance, NONE, "NONE");
						_delay_ms(1000);
					}
				}
				else{
					float answer = read_float_from_eeprom(counter);
					if(isnan(answer)){
						updateLCDScreen(1, "No value", NONE, "NONE");
						updateLCDScreen(2, "In memory", NONE, "NONE");
						_delay_ms(1000);
					}
					else{
						char distance[100];
						char memVal[100];
						sprintf(distance, "value is %.2f", answer);
						sprintf(memVal, "at mem loc %d", counter);
						updateLCDScreen(1,memVal, NONE, "NONE");
						updateLCDScreen(2, distance, NONE, "NONE");
						_delay_ms(1000);
						counter += 4;
					}
				}
				updateLCDScreen(1,"", NONE, "NONE");
				updateLCDScreen(2, "", NONE, "NONE");
			}
			
		}
		
	}
	
	
	sei();
}

//PUSHBUTTON HANDLERS
ISR(PCINT0_vect) {
	cli();
	if (!(PINB & (1 << PINB0))) {
		// Button on pin 8 was pressed
		// Handle the button press here
		//Press handles current data
		//wait until button is released
		while(!(PINB & (1 << PINB0))){
			
		}
		//Clear eerprom
		updateLCDScreen(1, "Points cleared!", NONE, "NONE");
		clearEEPROM();
		isFirst = 0;
		_delay_ms(2500);
	}
	else if (!(PINB & (1 << PINB1))) {
		//wait until button is released
		while(!(PINB & (1 << PINB1))){

		}
		// Button on pin 9 was pressed
		// Handle the button press here
		// If we haven't saved a value yet
		if(isFirst == 0){
			isFirst = 1;
			numLat2 = numLat1;
			numLong2 = numLong1;
			updateLCDScreen(2,"",NONE,"");
			updateLCDScreen(1,"Point 1 Saved!",NONE,"");
			_delay_ms(2500);
		}
		//If we want to calculate distance
		else{
			isFirst = 0;
			updateLCDScreen(1, "", NONE, "NONE");
			updateLCDScreen(2, "", NONE, "NONE");
			char distance[100];
			float dis = haversine(numLat1,numLong1,numLat2,numLong2);
			saveFloatToEEPROM(roundTens(dis));
			sprintf(distance, "%.2f feet", dis);
			updateLCDScreen(1, "Distance:", NONE, "NONE");
			updateLCDScreen(2, distance, NONE, "NONE");
			_delay_ms(2500);
		}
	}
	sei();
}

/************************************************************************/
/*								MAIN                                    */
/************************************************************************/
int loop(){
	while(1){
		unsigned char data = USART_Receive(); //Get GPS character, if it leads with a $ it means we have gps data
		if(data == '$'){
			char buffer[100];
			USART_receive_string(buffer);
			//Check for correct data
			if(buffer[0] == 'G' && buffer[1] == 'P' && buffer[2] == 'R' && buffer[3] == 'M' && buffer[4] == 'C'){
				// use strtok to split the string into individual fields;
				char* token;
				token = strtok(buffer, ",");
				
				// the first token should be the sentence identifier ("GPRMC")
				// For whatever reason it is not, throw an error
				if (strcmp(token, "GPRMC") != 0) {
					updateLCDScreen(1, "GPS ERROR...", NONE, "NONE");
					updateLCDScreen(2, "", NONE, "NONE");
				}
				
				token = strtok(NULL, ","); //throwaway, token is time in HHMMSS.ss format
				token = strtok(NULL, ","); //throwaway, token is status ("A" = valid data, "V" = invalid data)
				
				//if the token is not valid
				if (strcmp(token, "A") != 0) {/
					updateLCDScreen(1, "Searching...", NONE, "NONE");
					updateLCDScreen(2, "", NONE, "NONE");
				}
				//if token is valid
				else{
					char latitude[9];
					char longitude[10];
					token = strtok(NULL, ","); // the fourth token should be the latitude in DDMM.mmmm format
					strncpy(latitude, token, 9); // copy the first 9 characters into the latitude buffer
					latitude[9] = '\0'; // add null terminator
					numLat1 = atof(latitude) / 100;
					sprintf(latitude, "Lat: %f", numLat1);
					updateLCDScreen(1, latitude, NONE, "NONE"); //Update display with latitude value
					token = strtok(NULL, ","); //throwaway token, the fifth token should is the latitude direction ("N" = North, "S" = South)
					token = strtok(NULL, ","); // the sixth token is the longitude in DDDMM.mmmm format
					strncpy(longitude, token, 10); // copy the first 10 characters into the longitude buffer
					longitude[10] = '\0'; // add null terminator
					numLong1 = (atof(longitude) / 100) * -1;
					sprintf(longitude,"Long: %f", numLong1);
					updateLCDScreen(2, longitude, NONE, "NONE"); //update lower display with longitude value
					_delay_ms(500); //delay for half a second before updating
					token = strtok(NULL, ","); //throwaway token, the seventh token is the longitude direction ("E" = East, "W" = West)
				}
			}
		}
	}
	return 0;
}

//Main loop
int main()
{
	setup();
	while(1)
	{
		loop();
	}	
	return 0;
}
