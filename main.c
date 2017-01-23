//////////////////////////////////////////////////////////////////////////
// @name:	3_3V_Power_Supply_2
// @func:	Sits in power save mode until watchdog wakes up uC.
//			May also wake up from timer 0 or ADC interrupts.
//			Uses watchdog and sleep modes for low power 
//				consumption.
//////////////////////////////////////////////////////////////////////////

#define F_CPU 1000000UL	// speed of clock after prescaler (8MHz/8)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

// Pins
#define LED_RED	PB0		// LOW enables Red LED
#define OUT_ENA	PB1		// HIGH enables 3.3V output
#define LED_GRN PB2		// LOW enables Green LED
#define CHR_STA	PB3		// LOW input means Li-Ion is charging
#define USB_STA	PB4		// HIGH input means USB connected

// ADC Conversion Variables
const unsigned char START_ADC_1S_WATCHDOG = 4;
const unsigned char START_ADC_0_5S_WATCHDOG = 8;
unsigned char watchdogCount = 0; // number of watchdog calls before new adc sample
unsigned short adcVal;
unsigned int sumVolt;
unsigned short voltage = 4000;
unsigned char numSamples = 0;	 // number of adc samples

// PWM Variables
const unsigned char PWM_RAMP_SPEED = 3;
const unsigned char PWM_MAX = 253;
const unsigned char PWM_MIN = 0;
unsigned char countPWM = 0;
char indexDirPWM = 1;
unsigned char indexPWM = 70;

// Status Variables
const unsigned short BATTERY_GOOD = 3419; // min good voltage = 3.5V
const unsigned short BATTERY_LOW = 3304; // min low voltage = 3.4V
const unsigned short BATTERY_CRITICAL = 3209; // min critical voltage = 3.3V
unsigned char mStatus;			// 1-9 status indicator
unsigned char pStatus = 0;		// 1-9 previous status indicator
unsigned char grn_glw = 0;		// 1 = green led glows
unsigned char red_glw = 0;		// 1 = red led glows
unsigned char requestStatus = 1;// 1 calls getStatus

char getStatus(void);
void setMode(void);
void setup(void);

//////////////////////////////////////////////////////////////////////////
// @name:	WDT_vect
// @func:	handles watchdog timer interrupt, to wake from sleep
//////////////////////////////////////////////////////////////////////////
ISR(WDT_vect) {

	sleep_disable();	
	requestStatus = 1; /* Used to call getStatus() in main(). Prefered
						  over calling getStatus() in interrupt to
						  shorten interrupt handler length */
	
}


//////////////////////////////////////////////////////////////////////////
// @name:	ADC Interrupt
// @func:	samples ADC val to build an average value
//////////////////////////////////////////////////////////////////////////
ISR(ADC_vect) {

	adcVal = ADCL;
	adcVal |= ADCH<<8; // get ADC values
	if (numSamples < 10) {
		numSamples++;
		sumVolt += (1126400 / adcVal );
		ADCSRA |= (1 << ADSC); // start another adc cycle
	}
	else {
		voltage = sumVolt / 10;
		numSamples = 0;
		sumVolt = 0;
		MCUCR |= (1 << SM1); // power down - prepare for sleep
		ADCSRA &= ~(1 << ADEN); // shut off ADC
	}
	
}


///////////////////////////////////////////////////////////////////////////////////////
//	@name:		Timer 0 Overflow Interrupt
//	@call:		When Timer 0 overflows
//	@note:		Used for 10-bit fast PWM output
///////////////////////////////////////////////////////////////////////////////////////
ISR(TIM0_OVF_vect) {
	sleep_disable();
	// Red Off
	PORTB |= (grn_glw << LED_GRN) | (red_glw << LED_RED);
}


///////////////////////////////////////////////////////////////////////////////////////
//	@name:		Timer 0 Compare A Interrupt
//	@call:		When OCR1A is equal to compare value
//	@note:		Used for 10-bit fast PWM output
///////////////////////////////////////////////////////////////////////////////////////
ISR(TIM0_COMPA_vect){
	
	sleep_disable();
	countPWM++;
	
	if (countPWM >= PWM_RAMP_SPEED){	// PWM ramp speed
		countPWM = 0;				// e.g. change duty cycle every 3rd overflow
		indexPWM += indexDirPWM;	// adjust duty cycle
		if (indexPWM >= PWM_MAX)		// if duty cycle = 100%,
			indexDirPWM = -1;		// ramp down
		else if (indexPWM <= PWM_MIN)		// else if duty cycle = 0%
			indexDirPWM = 1;		// ramp up
		OCR0A = indexPWM;
		
		PORTB &= ~(grn_glw << LED_GRN) & ~(red_glw << LED_RED);

	}
}


//////////////////////////////////////////////////////////////////////////
// @name:	findMode
// @func:	determine status of switch, USB, and Li-Ion IC
// @rtrn:	1-9 number designating mode
//////////////////////////////////////////////////////////////////////////
char getStatus(void) {
	
	if ( mStatus == 7 ) {
		DDRB &= ~(1 << OUT_ENA); // disable 3.3V
		_delay_us(5);	// wait for pin to settle before reading
	}
	
	if ( !( PINB & (1 << OUT_ENA) ) ) {
		if ( !( PINB & (1 << USB_STA) ) ) {
			/*	Mode 1 */
			return 1; 
		}
		else if ( !( PINB & (1 << CHR_STA) ) ) {
			/*	Mode 2 */
			return 2; 
		}
		else {
			/*	Mode 3 */
			return 3; 
		} 
	}
	else {
		if ( !( PINB & (1 << USB_STA) ) ) {
			if ( voltage > BATTERY_GOOD ) {
				/*	Mode 4 */
				if ( watchdogCount % START_ADC_1S_WATCHDOG == 0 ) {
					watchdogCount = 0;
					MCUCR &= ~(1 << SM1);  // go to idle mode
					ADCSRA |= (1 << ADEN) | (1 << ADSC); // start adc cycles
				}
				watchdogCount++;
				return 4; 
			}
			else if ( voltage > BATTERY_LOW )	{
				/*	Mode 5 */
				PORTB ^= (1 << LED_RED); // battery low warning light
				watchdogCount++;
				if ( watchdogCount % START_ADC_1S_WATCHDOG == 0 ) {
					watchdogCount = 0;
					MCUCR &= ~(1 << SM1);  // go to idle mode
					ADCSRA |= (1 << ADEN) | (1 << ADSC); // start adc cycles
				}
				return 5; 
			}
			else if ( voltage > BATTERY_CRITICAL ) {
				/*	Mode 6 */
				PORTB ^= (1 << LED_RED); // battery critical warning light
				watchdogCount++;
				if ( watchdogCount % START_ADC_0_5S_WATCHDOG == 0 ) {
					watchdogCount = 0;
					MCUCR &= ~(1 << SM1);  // go to idle mode
					ADCSRA |= (1 << ADEN) | (1 << ADSC); // start adc cycles
				}
				return 6; 
			}
			else {
				/*	Mode 7 */
				watchdogCount++;
				if ( watchdogCount % START_ADC_1S_WATCHDOG == 0 ) {
					watchdogCount = 0;
					MCUCR &= ~(1 << SM1);  // go to idle mode (wake up, bitch!)
					ADCSRA |= (1 << ADEN) | (1 << ADSC); // start adc cycles
				}
				DDRB |= (1 << OUT_ENA); // re-enable 3.3V
				return 7; 
			}
		}
		else if ( !( PINB & (1 << CHR_STA) ) ) {
			/*	Mode 8 */
			return 8; 
		}
		else {
			/*	Mode 9 */
			return 9;
		}
	}
	return 1;
}


//////////////////////////////////////////////////////////////////////////
// @name:	setMode
// @func:	controls green led, red led, and 3.3V based on mode
//////////////////////////////////////////////////////////////////////////
void setMode(void) {
	switch(mStatus) {
		
		
		/*	Off with no USB 
			deep sleep */
		case 1 :
		default	:
		
			// I/O
			DDRB &= ~(1 << LED_GRN) & ~(1 << LED_RED) & ~(1 << OUT_ENA); // disable green, red, 3.3V
			PORTB &= ~(1 << LED_GRN) & ~(1 << LED_RED) & ~(1 << OUT_ENA);	// green, red led off
			
			// Power
			MCUCR |= (1 << SM1); // power down
			TIMSK &= ~(1 << OCIE0A) & ~(1 << TOIE0); // Disable Timer 0 Interrupts
			TCCR0B &= ~(1 << CS01); // Timer 0 Clock = 0
			WDTCR |= (1 << WDP1); // 1 second watchdog
			WDTCR &= ~(1 << WDP0);
			ADCSRA &= ~(1 << ADEN); // shut off ADC
			break;
			
			
		/*	Off, charging
			no power savings, glow red */
		case 2 :
		
			// I/O
			red_glw = 1;
			grn_glw = 0;
			// @TODO glow red
			DDRB |= (1 << LED_RED);		// enable red
			DDRB &= ~(1 << LED_GRN) & ~(1 << OUT_ENA);	// disable green, 3.3V
			PORTB &= ~(1 << LED_GRN);	// green off, red off
			PORTB |= (1 << LED_RED);	// red off
			
			// Power
			MCUCR &= ~(1 << SM1);	// idle
			TIMSK |= (1 << OCIE0A) | (1 << TOIE0);	// Enable Timer 0 Interrupts
			TCCR0B |= (1 << CS01);	// Clock = prescaler/256
			WDTCR |= (1 << WDP1);	// 1 second watchdog
			WDTCR &= ~(1 << WDP0);
			ADCSRA &= ~(1 << ADEN); // shut off ADC
			break;
			
			
		/*	Off, fully charged
			deep sleep, red LED on */
		case 3 :
		
			// I/O
			DDRB |= (1 << LED_RED);		// enable red
			DDRB &= ~(1 << LED_GRN) & ~(1 << OUT_ENA); // disable 3.3V, green
			PORTB &= ~(1 << LED_GRN) & ~(1 << LED_RED); // green off, red on
			
			// Power
			MCUCR |= (1 << SM1); // power down
			TIMSK &= ~(1 << OCIE0A) & ~(1 << TOIE0); // Disable Timer 0 Interrupts
			TCCR0B &= ~(1 << CS01); // Timer 0 Clock = 0
			WDTCR |= (1 << WDP1); // 1 second watchdog
			WDTCR &= ~(1 << WDP0);
			ADCSRA &= ~(1 << ADEN); // shut off ADC
			break;
		
		
		/*	On, no USB, V > 3.5V
			deep sleep, green on, voltage check */
		case 4 :
		
			// I/O
			DDRB |= (1 << LED_GRN); // enable green
			DDRB &= ~(1 << LED_RED) & ~(1 << OUT_ENA); // disable red, 3.3V
			PORTB &= ~(1 << LED_GRN) & ~(1 << LED_RED); // green on, red off
			
			// Power
			MCUCR |= (1 << SM1); // power down
			TIMSK &= ~(1 << OCIE0A) & ~(1 << TOIE0); // Disable Timer 0 Interrupts
			TCCR0B &= ~(1 << CS01); // Timer 0 Clock = 0
			WDTCR |= (1 << WDP1); // 1 second watchdog
			WDTCR &= ~(1 << WDP0);
			break;
			
		/*	On, no USB, 3.4V < V < 3.5V
			deep sleep, green on, red 1s blink, voltage check */
		case 5 :
		
			// I/O
			DDRB |= (1 << LED_GRN) | (1 << LED_RED); // enable green, red
			DDRB &= ~(1 << OUT_ENA); // disable 3.3V
			PORTB &= ~(1 << LED_GRN) & ~(1 << LED_RED); // green on, red on
			// @TODO red slow blinking
			
			// Power
			MCUCR |= (1 << SM1); // power down
			TIMSK &= ~(1 << OCIE0A) & ~(1 << TOIE0); // Disable Timer 0 Interrupts
			TCCR0B &= ~(1 << CS01); // Timer 0 Clock = 0
			WDTCR |= (1 << WDP1); // 1 second watchdog
			WDTCR &= ~(1 << WDP0);
			break;
			
			
		/*	On, no USB, 3.3V < V < 3.4V
			deep sleep, green on, red 1/2s blink, voltage check */
		case 6 :
			
			// I/O
			DDRB |= (1 << LED_GRN) | (1 << LED_RED); // enable green, red
			DDRB &= ~(1 << OUT_ENA); // disable 3.3V
			PORTB &= ~(1 << LED_GRN) & ~(1 << LED_RED); // green on, red on
			// @TODO red fast blinking
			
			// Power
			MCUCR |= (1 << SM1); // power down
			TIMSK &= ~(1 << OCIE0A) & ~(1 << TOIE0); // Disable Timer 0 Interrupts
			TCCR0B &= ~(1 << CS01); // Timer 0 Clock = 0
			WDTCR |= (1 << WDP0); // 1/2 second watchdog
			WDTCR &= ~(1 << WDP1);
			break;
			
			
		/*	On, no USB, V < 3.3V
			deep sleep, leds off, output off */
		case 7 :
		
			// I/O
			DDRB &= ~(1 << LED_GRN) & ~(1 << LED_RED);  // disable green, red led
			DDRB |= (1 << OUT_ENA); // enable 3.3V
			PORTB &= ~(1 << LED_GRN) & ~(1 << LED_RED); // green off, red off
			
			// Power
			MCUCR |= (1 << SM1); // power down
			TIMSK &= ~(1 << OCIE0A) & ~(1 << TOIE0); // Disable Timer 0 Interrupts
			TCCR0B &= ~(1 << CS01); // Timer 0 Clock = 0
			WDTCR |= (1 << WDP1); // 1 second watchdog
			WDTCR &= ~(1 << WDP0);
			break;
			
			
		/*	On, charging
			no power savings, glow green */
		case 8 :
		
			// I/O
			red_glw = 0; // red off
			grn_glw = 1; // glowing green
			DDRB |= (1 << LED_GRN);	// enable green
			DDRB &= ~(1 << LED_RED) & ~(1 << OUT_ENA); // disable red, 3.3V
			PORTB &= ~(1 << LED_GRN) & ~(1 << LED_RED); // green on, red off

			// Power
			MCUCR &= ~(1 << SM1); // idle
			TIMSK |= (1 << OCIE0A) | (1 << TOIE0);	// Enable Timer 0 Interrupts
			TCCR0B	|=	(1 << CS01);	// Timer 0 Clock = prescaler/256
			WDTCR |= (1 << WDP1); // 1 second watchdog
			WDTCR &= ~(1 << WDP0);
			ADCSRA &= ~(1 << ADEN); // shut off ADC
			break;
			
			
		/*	On, fully charged
			deep sleep, solid green */
		case 9 :
		
			// I/O
			DDRB |= (1 << LED_GRN);	// enable green
			DDRB &= ~(1 << LED_RED) & ~(1 << OUT_ENA); // disable red, 3.3V
			PORTB &= ~(1 << LED_GRN) & ~(1 << LED_RED); // green on, red off

			// Power
			MCUCR |= (1 << SM1); // power down
			TIMSK &= ~(1 << OCIE0A) & ~(1 << TOIE0); // Disable Timer 0 Interrupts
			TCCR0B &= ~(1 << CS01); // Timer 0 Clock = 0
			WDTCR |= (1 << WDP1); // 1 second watchdog
			WDTCR &= ~(1 << WDP0);
			ADCSRA &= ~(1 << ADEN); // shut off ADC
			break;
			
	}
	
}


//////////////////////////////////////////////////////////////////////////
// @name:	setup
// @func:	set up registers and initial configuration
//////////////////////////////////////////////////////////////////////////
void setup (void) {
	
	// Configure Timer 0
	TCCR0A	|=	(1 << WGM01) | (1 << WGM00);	// Fast PWM, no OC0A connection
	OCR0A	=	0x00;							// Initial duty cycle
	
	// Configure ADC
	ADMUX |= (1 << MUX3) | (1 << MUX2); // 1.1V, Vbg as input voltage and Vcc as reference
	ADCSRA |= (1 << ADPS2) | (1 << ADPS1); // Prescale 8MHz by 64 = 125kHz
	ADCSRA |= (1 << ADIE);  // enable ADC interrupts
		
	// Configure sleep mode
	PRR |= (1 << PRTIM1) | (1 << PRUSI); // turn off timer 1, USI
	MCUCR |= (1 << SM1); // power down mode
	
	// Configure Watchdog Timer 
	WDTCR |= (1 << WDIE) | (1 << WDP2) | (0 << WDP1); // 1 second watchdog
}


int main(void){
	
	setup();
	
	sei();
	
	// disable ADC
	// disable analog comparator
	// make outputs inputs
	
	while(1){
		
		// runs only from watchdog interrupts
		if ( requestStatus == 1) {
			
			mStatus = getStatus();

			if ( mStatus != pStatus )
				setMode();
			pStatus = mStatus;
			requestStatus = 0;
			
		}
	
		sleep_mode();
		//_delay_ms(10);
		
	}
	
}