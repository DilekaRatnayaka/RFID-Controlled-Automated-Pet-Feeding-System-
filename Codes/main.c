#define F_CPU 8000000UL
#include <avr/io.h>
#include <stdio.h>			//Include std. library file 
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>

#define  Trigger_pin	PA0	//Trigger pin for the ultrasonic sensor

#define DISTANCE_THRESHOLD_MIN 16.0
#define DISTANCE_THRESHOLD_MAX 17.0 // Define your threshold distance in centimeters


#define LCD_Data_Dir DDRC				// Define LCD data port direction 
#define LCD_Command_Dir DDRB			// Define LCD command port direction register 
#define LCD_Data_Port PORTC				// Define LCD data port 
#define LCD_Command_Port PORTB			// Define LCD data port 
#define RS PB0							// Define Register Select (data reg./command reg.) signal pin 
#define RW PB1							// Define Read/Write signal pin 
#define EN PB2							// Define Enable signal pin 

#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1) // GSM 

#include "header.h"

#define MAX_PETS 1

uint8_t pet_UID[MAX_PETS][4] = {
    {0x23, 0xF0, 0x89, 0xA6}
};

// Initialize UART with the specified baud rate
void UART_init(long USART_BAUDRATE){
    UCSRB |= (1 << RXEN) | (1 << TXEN); 	// Turn on transmission and reception
    UCSRC |= (1 << URSEL) | (1 << UCSZ0) | (1 << UCSZ1); // Use 8-bit character sizes
    UBRRL = BAUD_PRESCALE; 			// Load lower 8-bits of the baud rate value
    UBRRH = (BAUD_PRESCALE >> 8); 		// Load upper 8-bits
}

// Receive a character from UART
unsigned char UART_RxChar(void){
    while ((UCSRA & (1 << RXC)) == 0); 		// Wait till data is received
    return(UDR); 				// Return the received byte
}

// Transmit a character through UART
void UART_TxChar(char ch){
    while (!(UCSRA & (1<<UDRE))); 		// Wait for empty transmit buffer
    UDR = ch;
}

// Send a string through UART
void UART_SendString(char *str){
    unsigned char j=0;
    while (str[j] != 0){ 			// Send string till null terminator
        UART_TxChar(str[j]);
        j++;
    }
}


void LCD_Command (char cmd)							//LCD command write function 
{
	LCD_Data_Port = cmd;							//Write command data to LCD data port 
	LCD_Command_Port &= ~((1<<RS)|(1<<RW));			// Make RS LOW (command reg.), RW LOW (Write) 
	LCD_Command_Port |= (1<<EN);					// High to Low transition on EN (Enable) 
	_delay_us(1);
	LCD_Command_Port &= ~(1<<EN);
	_delay_ms(3);									// Wait little bit 
}

void LCD_Char (char char_data)						// LCD data write function 
{
	LCD_Data_Port = char_data;						// Write data to LCD data port 
	LCD_Command_Port &= ~(1<<RW);					// Make RW LOW (Write) 
	LCD_Command_Port |= (1<<EN)|(1<<RS);			// Make RS HIGH (data reg.) and High to Low transition on EN (Enable) 
	_delay_us(1);
	LCD_Command_Port &= ~(1<<EN);
	_delay_ms(1);									// Wait little bit 
}

void LCD_Init (void)								/// LCD Initialize function 
{
	LCD_Command_Dir |= (1<<RS)|(1<<RW)|(1<<EN);		// Make LCD command port direction as o/p
	LCD_Data_Dir = 0xFF;							// Make LCD data port direction as o/p 
	
	_delay_ms(20);									// LCD power up time to get things ready, it should always >15ms 
	LCD_Command (0x38);								// Initialize 16X2 LCD in 8bit mode 
	LCD_Command (0x0C);								/// Display ON, Cursor OFF command 
	LCD_Command (0x06);								// Auto Increment cursor 
	LCD_Command (0x01);								// Clear LCD command 
	LCD_Command (0x80);								// 8 is for first line and 0 is for 0th position 
}

void LCD_String (char *str)							// Send string to LCD function 
{
	int i;
	for(i=0;str[i]!=0;i++)							// Send each char of string till the NULL 
	{
		LCD_Char (str[i]);							// Call LCD data write 
	}
}

void LCD_String_xy (char row, char pos, char *str)	// Send string to LCD function 
{
	if (row == 1)
		LCD_Command((pos & 0x0F)|0x80);				// Command of first row and required position<16 
	else if (row == 2)
		LCD_Command((pos & 0x0F)|0xC0);				// Command of Second row and required position<16 
	LCD_String(str);								// Call LCD string function 
}

void LCD_Clear (void)								// LCD clear function 
{
	LCD_Command (0x01);								// Clear LCD command 
	LCD_Command (0x80);								// 8 is for first line and 0 is for 0th position 
}

void displayFoodLevel(double distance) {
    char foodLevel[10];
    char foodStatus[20];

    if (distance >= 0 && distance <= 5) {
        //strcpy(foodLevel, "High");
        strcpy(foodStatus, "Enough food");
    } else if (distance > 5 && distance <= 10) {
        //strcpy(foodLevel, "Medium");
        strcpy(foodStatus, "Moderate food");
    } else if (distance > 10 && distance <= 15) {
        //strcpy(foodLevel, "Low");
        strcpy(foodStatus, "Low food");
    } else if (distance > 15 && distance <= 20) {
        //strcpy(foodLevel, "Very Low");
        strcpy(foodStatus, "Very low food");
    } else if (distance > 20 && distance <= 30) {
        //strcpy(foodLevel, "Empty");
        strcpy(foodStatus, "Out of food");
    }

    LCD_Clear();
    LCD_String_xy(1, 0, "Food Level:");
    //LCD_String_xy(2, 0, foodLevel);
    LCD_String_xy(2, 0, foodStatus);
}

int TimerOverflow = 0;
int notificationSent = 0; // Flag to track if the notification has been sent

ISR(TIMER1_OVF_vect)
{
	TimerOverflow++;	// Increment Timer Overflow count 
}

int main(void)
{	
    // Initialize necessary hardware and settings here

    // SPI initialization
    spi_init();
    _delay_ms(1000);

    // Initialize RFID reader
    mfrc522_init();
	
	char string[10];
	long count;
	double distance;
	
	DDRA = 0x01;		// Make trigger pin as output 
	PORTD = 0xFF;		// Turn on Pull-up 
	
	LCD_Init();
	LCD_String_xy(1, 0, "Cleo's Food Bowl");
	
	sei();			// Enable global interrupt */
	TIMSK = (1 << TOIE1);	// Enable Timer1 overflow interrupts 
	TCCR1A = 0;		// Set all bit to zero Normal operation 

	while(1)
	{
		// Give 10us trigger pulse on trig. pin to HC-SR04 
		PORTA |= (1 << Trigger_pin);
		_delay_us(10);
		PORTA &= (~(1 << Trigger_pin));
		
		TCNT1 = 0;	// Clear Timer counter 
		TCCR1B = 0x41;	// Capture on rising edge, No prescaler
		TIFR = 1<<ICF1;	// Clear ICP flag (Input Capture flag) 
		TIFR = 1<<TOV1;	// Clear Timer Overflow flag 

		//Calculate width of Echo by Input Capture (ICP) 
		
		while ((TIFR & (1 << ICF1)) == 0);// Wait for rising edge 
		TCNT1 = 0;	// Clear Timer counter 
		TCCR1B = 0x01;	// Capture on falling edge, No prescaler 
		TIFR = 1<<ICF1;	// Clear ICP flag (Input Capture flag) 
		TIFR = 1<<TOV1;	// Clear Timer Overflow flag 
		TimerOverflow = 0;// Clear Timer overflow count 

		while ((TIFR & (1 << ICF1)) == 0);// Wait for falling edge 
		count = ICR1 + (65535 * TimerOverflow);	// Take count 
		// 8MHz Timer freq, sound speed =343 m/s 
		distance = (double)count / 466.47;

		dtostrf(distance, 2, 2, string);// distance to string 
		strcat(string, " cm   ");	// Concat unit i.e.cm 
		LCD_String_xy(2, 0, "Dist = ");
		LCD_String_xy(2, 7, string);	// Print distance 
		displayFoodLevel(distance);
		_delay_ms(200);
		
		        if (distance >= DISTANCE_THRESHOLD_MIN && distance <= DISTANCE_THRESHOLD_MAX)
        {
            // Distance is between 16 and 17 centimeters, send the message.
            if (!notificationSent)
            {
                // Only send the message if it hasn't been sent before.
                UART_init(9600);		// Initialize UART communication with a baud rate of 9600
                UART_SendString("AT\r\n");	// Send AT command to the GSM module
                _delay_ms(200);
                UART_SendString("ATE0\r\n");	// Disable command echo
                _delay_ms(200);
                UART_SendString("AT+CMGF=1\r\n");// Set SMS text mode
                _delay_ms(200);
                UART_SendString("AT+CMGS=\"+94772455735\"\r\n");// Set recipient phone number
                _delay_ms(200);
                UART_SendString("Refill the Dog's Food Bowl");// Send SMS text
                UART_TxChar(26);		// Send Ctrl+Z to indicate end of SMS
				_delay_ms(10000); 		// Delay before sending another SMS
                notificationSent = 1; // Set the notification flag to indicate that it has been sent
            }
        }
        else
        {
            // Reset the notification flag if the distance is outside the specified range.
            notificationSent = 0;
        }
		uint8_t status;
        uint8_t UID[4];
		
        status = mfrc522_request(PICC_REQALL, UID);
        if (status == CARD_FOUND) {
            status = mfrc522_get_card_serial(UID);
            if (status == CARD_FOUND) {
                int detected_pet = -1;
                for (int i = 0; i < MAX_PETS; i++) {
                    int match = 1;
                    for (uint8_t byte = 0; byte < 4; byte++) {
                        if (UID[byte] != pet_UID[i][byte]) {
                            match = 0;
                            break;
                        }
                    }
                    if (match) {
                        detected_pet = i;
                        break;
                    }
                }
				
                if (detected_pet == -1) {
                   // LCDWriteStringXY(0, 0, "Access denied!");
                } 
				
				else {
					
					DDRD |= (1<<PD5);	// Make OC1A pin as output  
					TCNT1 = 0;			// Set timer1 count zero 
					ICR1 = 2499;		// Set TOP count for timer1 in ICR1 register 
						
					// Set Fast PWM, TOP in ICR1, Clear OC1A on compare match, clk/64 
					TCCR1A = (1<<WGM11)|(1<<COM1A1);
					TCCR1B = (1<<WGM12)|(1<<WGM13)|(1<<CS10)|(1<<CS11);
					OCR1A = 300;	// Set servo at +90  position 
					_delay_ms(2000);
					OCR1A = 175;	// Set servo shaft at 0  position 
					_delay_ms(200);
					OCR1A = 65;		// Set servo shaft at -90� position 
					_delay_ms(200);
                    //LCDWriteStringXY(0, 0, "Hi Cleo!");
                    // Execute actions for the detected pet.
                }
                _delay_ms(2000);
            }
            _delay_ms(200);
        }
	}	
}

