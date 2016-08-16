/*
 * BT_16Bit_PWM.cpp
 *
 * Created: 09.05.2016 20:40:33
 * Author : holzi
 */ 

#ifndef F_CPU
#define F_CPU 16000000UL // 16 MHz clock speed
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include "inttypes.h"

#include "USART2.h"
#include "spi.h"
#include "TLC59711.h"
#include "debounce.h"

//#include "signal.h"


#define BAUDRATE 51			//19200 -> Refer Datasheet Page 190
#define DIMM_STEP 0xFF		//Step for 1 Repeat of the debounce routine (can be set in debounce.h)
#define MAX_GS_VAL	0xFFFF	//Maximum value for Greyscale
#define NUM_OF_SETS	3	//Number of predefined Color Sets
char string[10];
uint16_t val;
int tmp;
int i;
int modus;


ISR(TIMER0_OVF_vect)		// every 10ms for debouncing the switch
{
	static uint8_t ct0, ct1, rpt;
	uint8_t i;

	TCNT0 = (uint8_t)(uint16_t)-(F_CPU / 1024 * 10e-3 + 0.5);	// preload for 10ms

	i = key_state ^ ~(KEY_PIN & 0x08);		// key changed ?
	ct0 = ~( ct0 & i );			// reset or count ct0
	ct1 = ct0 ^ (ct1 & i);		// reset or count ct1
	i &= ct0 & ct1;			// count until roll over ?
	key_state ^= i;			// then toggle debounced state
	key_press |= key_state & i;		// 0->1: key press detect
	key_release |= ~key_state & i; // 1->0: key release detect
	
	if( (key_state & REPEAT_MASK) == 0 )	// check repeat function
	rpt = REPEAT_START;		// start delay
	if( --rpt == 0 ){
		rpt = REPEAT_NEXT;			// repeat delay
		key_rpt |= key_state & REPEAT_MASK;
	}
}

void switch_CSet(uint8_t CSet)
{
	
}

int main(void)
{
	//intital Greyscale Values
	uint16_t coldW1 = 0xFF;			//coldWhite 1
	uint16_t warmW1 = 0xFF;			//warmWhite 1
	uint16_t coldW2 = 0xFF;			//coldWhite 2
	uint16_t warmW2 = 0xFF;			//warmWhite 2
	
	bool dimm_direc = true;			//true  = up
									//false = down				

	uint8_t CSet = 0;				//Color Set

	TLC59711 myChip;

	SPI_init();
	USART_Init(BAUDRATE);


	//Configuration Timer0 (debouncing)
	TCCR0B = (1<<CS02)|(1<<CS00);	// divide by 1024
	TIMSK0 |= (1 << TOIE0);			// enable timer interrupt
	
	DDRB  |= (1 << PORTB0 );		//PB0 is output (LED)
	PORTB |= (1 <<PORTB0);			//Switch LED on
		
	PORTD |= (1<<PORTD3);			//Activate Pullups for PD3 (Switch)

	
	//BTM222 Reset Configuration (not needed because of internal Pullup of the BTM222)
	/*DDRB |= (1<<PB1);
	PORTB |= (1<<PB1);		//high -> no Reset
	PORTB &= ~(1<<PB1);		//low  -> Reset   */
	

	int faktor =257;
	myChip.setGreyScale(2, coldW1, warmW1, 0x0);
	myChip.setGreyScale(3, coldW2, warmW2, 0x0);
	sei();							//Enable Global Interrups (for USART)
	
    while (1) 
    {
		//Short Press recognized
		if(get_key_short(1<<KEY0))
		{
			PORTB ^= (1<<PORTB0);
			CSet++;
			if (CSet == NUM_OF_SETS) CSet = 0;
			
			switch_CSet(CSet);
		}
		
		//long press recognized (needed for correct behaviour of get_key_rpt_l() )
		if( get_key_long_r( 1<<KEY0 ))
			asm("nop");			//do nothing

		
		//Pressed an hold -> Dimming called every 50ms
		if(get_key_rpt_l( 1<<KEY0 ))
		{	
			//LED_PORT ^= (1<<PORTB0);
			//All colors are treated the same
			if(dimm_direc == true) //dimm up
			{
				if((coldW1 <= (MAX_GS_VAL-DIMM_STEP)) && (warmW1 <= (MAX_GS_VAL-DIMM_STEP)) && 
				   (coldW2 <= (MAX_GS_VAL-DIMM_STEP)) && (warmW2 <= (MAX_GS_VAL-DIMM_STEP)))	
				{
					coldW1 += DIMM_STEP;
					warmW1 += DIMM_STEP;
					coldW2 += DIMM_STEP;
					warmW2 += DIMM_STEP;
									
					//write out to chip
					myChip.setGreyScale(3, coldW1, warmW1, 0x0);
					myChip.setGreyScale(2, coldW2, warmW2, 0x0);
				}
			}
			else
			{
				if((coldW1 >= DIMM_STEP) && (warmW1 >= DIMM_STEP) &&
				   (coldW2 >= DIMM_STEP) && (warmW2 >= DIMM_STEP))	
				{
					coldW1 -= DIMM_STEP;
					warmW1 -= DIMM_STEP;
					coldW2 -= DIMM_STEP;
					warmW2 -= DIMM_STEP;
									
					//write out to chip
					myChip.setGreyScale(3, coldW1, warmW1, 0x0);
					myChip.setGreyScale(2, coldW2, warmW2, 0x0);
				}		
			}			
		}
		
		if(get_key_release( (1<<KEY0) ))
		{
			dimm_direc = !dimm_direc;	//toggle dimming direction
		}
						
		
		if (DataInReceiveBuffer())
		{
			for (int a = 0 ; a<9 ; a++)
			{
				string[a] = 0;
			}
			
			if(USART_Receive() == 'a')
			{
				USART_Receive();	//space
				i = 0;
				tmp = 0;
				while(tmp != '\n')
				{
					tmp = USART_Receive();
					string[i] = tmp;	
					i++;
				}
				
				string[i] =  '\0';
				
				val = atoi(string);
				val = faktor*val;
				
				myChip.setGreyScale(3, val, val, val);
				myChip.setGreyScale(2, val, val, val);
			}
		}
	}
		
}

/*switch(USART_Receive())
{
	case 'a':
	PORTB |= (1 << PB0);			//Turn on
	myChip.setGlobalBrightness(3, 0x7F);
	myChip.setGreyScale(3, 0xFFFF, 0xFFFF,0xFFFF);
	myChip.setGreyScale(2, 0xFFFF, 0xFFFF,0xFFFF);
	USART_Transmit('a');
	USART_Transmit('n');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
	
	case 'b':
	PORTB &= ~(1 << PB0);			//Turn on
	myChip.setGreyScale(3 , 0x0 , 0x0, 0x0);
	myChip.setGreyScale(2 , 0x0 , 0x0, 0x0);
	USART_Transmit('a');
	USART_Transmit('u');
	USART_Transmit('s');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
	
	case '0':
	myChip.setGreyScale(3 , 0x0001 , 0x0001, 0x1);
	myChip.setGreyScale(2 , 0x0001 , 0x0001, 0x1);
	USART_Transmit('0');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
	
	case '1':
	myChip.setGreyScale(3 , 0x0002 , 0x0002, 0x2);
	myChip.setGreyScale(2 , 0x0002 , 0x0002, 0x2);
	USART_Transmit('1');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
	
	case '2':
	myChip.setGreyScale(3 , 0x000F , 0x000F, 0x000F);
	myChip.setGreyScale(2 , 0x000F , 0x000F, 0x000F);
	USART_Transmit('2');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
	
	case '3':
	myChip.setGreyScale(3 , 0x00FF , 0x00FF, 0x00FF);
	myChip.setGreyScale(2 , 0x00FF , 0x00FF, 0x00FF);
	USART_Transmit('3');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
	
	case '4':
	myChip.setGreyScale(3 , 0 , 0xFFFF, 0);
	myChip.setGreyScale(2 , 0 , 0xFFFF, 0);
	USART_Transmit('4');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
	
	case '5':
	myChip.setGreyScale(3 , 0xFFFF , 0 , 0);
	myChip.setGreyScale(2 , 0xFFFF , 0 , 0);
	USART_Transmit('5');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
	
	default:
	USART_Transmit('E');
	USART_Transmit('R');
	USART_Transmit('R');
	USART_Transmit('O');
	USART_Transmit('R');
	USART_Transmit('\n');
	USART_Transmit('\r');
	break;
}*/