#define F_CPU 16000000UL  // Define CPU clock speed
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>   // EEPROM library
#include "led_i2c.h"      // LCD and I2C library
#include "DHT.h"          // DHT11 sensor library
#include <stdio.h>

// Define motor control pins
#define IN1 PD2  // Motor control pin IN1 on PORTD, PIN2 (D2)
#define IN2 PD3  // Motor control pin IN2 on PORTD, PIN3 (D3)
#define ENA PD5  // Motor speed control (PWM) on PORTD, PIN5 (OC0B, D5)

volatile uint8_t screen_state = 0;  // 0 = Temp/Humidity, 1 = Date, 2 = Time
volatile uint8_t interrupt_counter = 0; // Count seconds for screen switching

// EEPROM addresses for storing date
uint8_t EEMEM ee_day = 12;
uint8_t EEMEM ee_month = 8;
uint16_t EEMEM ee_year = 2024;

// Time-keeping variables
volatile uint8_t seconds = 0;
volatile uint8_t minutes = 0;
volatile uint8_t hours = 13;
volatile uint8_t day;
volatile uint8_t month;
volatile uint16_t year;

void pwm_init(void) {
	// Set PWM for 8-bit Fast PWM mode on Timer0
	TCCR0A |= (1 << WGM00) | (1 << WGM01);  // Fast PWM mode
	TCCR0A |= (1 << COM0B1);  // Clear OC0B on Compare Match, set OC0B at BOTTOM (non-inverting mode)
	TCCR0B |= (1 << CS01);  // Set prescaler to 8 and start PWM
	DDRD |= (1 << ENA);  // Set ENA (PD5) as output for PWM signal
}

void set_motor_speed(uint8_t speed) {
	OCR0B = speed;  // Set the PWM duty cycle (0-255) to control speed
}

void motor_forward(void) {
	PORTD |= (1 << IN1);  // Set IN1 high
	PORTD &= ~(1 << IN2);  // Set IN2 low
}

void motor_stop(void) {
	PORTD &= ~(1 << IN1);  // Set IN1 low
	PORTD &= ~(1 << IN2);  // Set IN2 low
	set_motor_speed(0);  // Stop the motor
}

void timer1_init(void) {
	// Set up Timer1 to generate an interrupt every 1 second
	TCCR1B |= (1 << WGM12);  // Configure Timer1 in CTC mode
	OCR1A = 15624;           // Set the compare value for 1 second at 16MHz and prescaler of 1024
	TCCR1B |= (1 << CS12) | (1 << CS10);  // Start Timer1 with prescaler 1024
	TIMSK1 |= (1 << OCIE1A);  // Enable Timer1 compare interrupt
}

ISR(TIMER1_COMPA_vect) {
	// Increment the time-keeping variables
	interrupt_counter++;
	if (interrupt_counter >= 2) {  // 2-second interval
		interrupt_counter = 0;
		screen_state++;
		if (screen_state > 2) screen_state = 0;  // Cycle through 0, 1, 2
	}

	seconds++;
	if (seconds >= 60) {
		seconds = 0;
		minutes++;
		if (minutes >= 60) {
			minutes = 0;
			hours++;
			if (hours >= 24) {
				hours = 0;
				day++;
				if (day > 30) {
					day = 1;
					month++;
					if (month > 12) {
						month = 1;
						year++;
					}
				}

				// Save updated date to EEPROM
				eeprom_update_byte(&ee_day, day);
				eeprom_update_byte(&ee_month, month);
				eeprom_update_word(&ee_year, year);
			}
		}
	}
}

void load_date_from_eeprom(void) {
	// Load saved date from EEPROM
	day = eeprom_read_byte(&ee_day);
	month = eeprom_read_byte(&ee_month);
	year = eeprom_read_word(&ee_year);

	// Check if EEPROM values are uninitialized (255) and set defaults
	if (day == 255) day = 1;
	if (month == 255) month = 1;
	if (year == 65535) year = 2024;
}

int main(void) {
	if (eeprom_read_byte(&ee_day) == 255) {
		eeprom_update_byte(&ee_day, 12);
	}
	if (eeprom_read_byte(&ee_month) == 255) {
		eeprom_update_byte(&ee_month, 8);
	}
	if (eeprom_read_word(&ee_year) == 65535) {
		eeprom_update_word(&ee_year, 2024);
	}

	// Load date from EEPROM
	load_date_from_eeprom();
	// Initialize LCD
	lcd_initializer myLCD = {
		.device.slave_address = 0x27,  // I2C address of the LCD
		.device.baud_rate = F_100kHz,  // I2C baud rate of 100kHz
		.rows = 2,                     // 2 rows
		.columns = 16,                 // 16 columns
		.dot_size = LCD_5x8DOTS        // 5x8 dot matrix
	};
	lcd_init(myLCD);
	_delay_ms(1000);  // Wait for the LCD to initialize
	lcd_backlight();
	
	
	
	// Initialize DHT11
	Dht11 dht11 = {
		.port = 'B',
		.pin = 1
	};
	dht11_init(dht11);
	
	// Initialize PWM for fan control
	pwm_init();

	// Set motor direction forward (only one direction is needed)
	motor_forward();

	// Initialize Timer1 for software clock
	timer1_init();
	sei(); // Enable global interrupts

	// Start the fan at base speed as soon as the code begins
	uint8_t base_speed = 135;  // Base speed (min speed)
	set_motor_speed(base_speed);

	// Variables to hold sensor data
	uint8_t temperature = 0;
	uint8_t humidity = 0;
	uint8_t fan_speed = base_speed;

	// Threshold values
	uint8_t temp_threshold = 25;  // Temperature threshold
	uint8_t hum_threshold = 35;   // Humidity threshold

	while (1) {
		lcd_clear();

		if (screen_state == 0) {
			// Read temperature and humidity
			if (dht11_read(&temperature, &humidity) == 0) {
				// Calculate differences from thresholds
				int temp_diff = temperature - temp_threshold;
				int hum_diff = humidity - hum_threshold;

				// Adjust fan speed based on the difference from thresholds
				if (temp_diff > 0) {
					fan_speed = base_speed + (temp_diff * 5);
					} else {
					fan_speed = base_speed + (temp_diff * 6);  // Decrease speed
				}

				if (hum_diff > 0) {
					fan_speed += (hum_diff * 2);
					} else {
					fan_speed += (hum_diff * 3);  // Decrease speed
				}

				// Ensure fan speed stays within 0-255 range
				if (fan_speed > 255) fan_speed = 255;
				if (fan_speed < base_speed) fan_speed = base_speed;

				// Set the fan speed
				set_motor_speed(fan_speed);

				// Display temperature and humidity on LCD
				char buffer1[16];
				char buffer2[16];
				sprintf(buffer1, "Temp: %dC", temperature);
				sprintf(buffer2, "Hum: %d%%", humidity);

				lcd_setCursor(0, 0);
				lcd_print(buffer1);
				lcd_setCursor(0, 1);
				lcd_print(buffer2);
				} else {
				// Display error on LCD
				lcd_setCursor(0, 0);
				lcd_print("DHT Error");
			}
			} else if (screen_state == 1) {
			// Display current date
			char date_buffer[16];

			sprintf(date_buffer, "%02d/%02d/%04d", day, month, year);

			lcd_setCursor(0, 0);
			lcd_print("Date:");
			lcd_setCursor(0, 1);
			lcd_print(date_buffer);
			} else if (screen_state == 2) {
			// Display current time
			char time_buffer[16];

			sprintf(time_buffer, "%02d:%02d:%02d", hours, minutes, seconds);

			lcd_setCursor(0, 0);
			lcd_print("Time:");
			lcd_setCursor(0, 1);
			lcd_print(time_buffer);
		}

		_delay_ms(450);  // Small delay to allow display update
	}
}
