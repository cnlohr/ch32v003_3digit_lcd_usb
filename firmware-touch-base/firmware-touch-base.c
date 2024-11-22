#include "ch32v003fun.h"
#include <stdio.h>
#include <string.h>
#include "rv003usb.h"


#include "ch32v003_touch.h"

// Allow reading and writing to the scratchpad via HID control messages.
uint8_t scratch[63];
volatile uint8_t start_leds = 0;
uint8_t ledat;

#define LCDCOM1 PD0
#define LCDCOM2 PD7
#define LCDCOM3 PD6
#define LCDCOM4 PD2

#define LCDSEGBUF GPIOC

#define LEDSEG0 PC0
#define LEDSEG1 PC1
#define LEDSEG2 PC2
#define LEDSEG3 PC3
#define LEDSEG4 PC4
#define LEDSEG5 PC5
static const uint8_t pinset[4] = { LCDCOM1, LCDCOM2, LCDCOM3, LCDCOM4 };

// from host
static uint32_t lastmask;


static uint8_t frame;

// LCD Is 10PIN TN Positive 3-Digits 7 Segment LCD Panel 3.0V

//  bbb
//  a d
//  ccc
//  e f
//  ggg
// ab0000cd0000ef0000g0

const uint32_t digits[16] = {
	0b11000001000011000010, // 0
	0b00000001000001000000, // 1
	0b01000011000010000010, // 2
	0b01000011000001000010, // 3
	0b10000011000001000000, // 4
	0b11000010000001000010, // 5
	0b11000010000011000010, // 6
	0b11000001000001000000, // 7
	0b11000011000011000010, // 8
	0b11000011000001000010, // 9
	0b11000011000011000000, // A
	0b10000010000011000010, // b
	0b00000010000010000010, // c
	0b00000011000011000010, // d
	0b11000010000010000010, // e
	0b11000010000010000000, // f
};

uint32_t ComputeLCDMaskWithNumber( uint32_t val )
{
	int i;
	uint32_t mask = 0;
	for( i = 0; i < 3; i++ )
	{
		uint32_t vv = (val >> 8);
		mask <<= 2;
		if( vv > 0 || i == 2 )
		{
			mask |= digits[vv & 0xf];
		}
		val <<= 4;
	}
	return mask;
}

void UpdateLCD( uint32_t mask )
{
	int group;

	#define drivepr GPIO_CFGLR_IN_PUPD

	LCDSEGBUF->BSHR = 0x3f<<16;

	for( group = 0; group < 4; group++ )
		funPinMode( pinset[group], drivepr );

	funPinMode( LEDSEG0, drivepr );
	funPinMode( LEDSEG1, drivepr );
	funPinMode( LEDSEG2, drivepr );
	funPinMode( LEDSEG3, drivepr );
	funPinMode( LEDSEG4, drivepr );
	funPinMode( LEDSEG5, drivepr );

	for( group = 0; group < 4; group++ )
	{
		LCDSEGBUF->BSHR = mask&0x3f;
		funDigitalWrite( pinset[group], 0 );
		Delay_Us(300);
		LCDSEGBUF->BSHR = 0x3f<<16;
		funDigitalWrite( pinset[group], 1 );
		Delay_Us(10);
		mask>>=6;
	}

	Delay_Us(3000);
}

static uint32_t ReadTouchPinSpecial( GPIO_TypeDef * io, int portpin, int adcno, int iterations ) __attribute__((noinline, section(".srodata")));
uint32_t ReadTouchPinSpecial( GPIO_TypeDef * io, int portpin, int adcno, int iterations )
{
	int group;
	uint32_t ret = 0;

	ADC1->RSQR3 = adcno;
	ADC1->SAMPTR2 = TOUCH_ADC_SAMPLE_TIME<<(3*adcno);

	// TODO: Control all IO's in unison.

	uint32_t CFGBASE = io->CFGLR & (~(0xf<<(4*portpin)));
	uint32_t CFGFLOAT = ((GPIO_CFGLR_IN_PUPD)<<(4*portpin)) | CFGBASE;
	uint32_t CFGDRIVE = (GPIO_CFGLR_OUT_2Mhz_PP)<<(4*portpin) | CFGBASE;

	for( group = 0; group < 4; group++ )
		funPinMode( pinset[group], GPIO_CFGLR_IN_FLOAT );

	funPinMode( LEDSEG0, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG1, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG2, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG3, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG4, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG5, GPIO_CFGLR_IN_FLOAT );
		io->CFGLR = CFGDRIVE;                                                   \
		io->BSHR = 1<<(portpin+(16*(1-TOUCH_SLOPE)));                          \


#if 0
#if TOUCH_FLAT == 1
#define RELEASEIO io->BSHR = 1<<(portpin+16*TOUCH_SLOPE); io->CFGLR = CFGFLOAT;
#else
#define RELEASEIO io->CFGLR = CFGFLOAT; io->BSHR = 1<<(portpin+16*TOUCH_SLOPE);
#endif

#define INNER_LOOP( n ) \
	{ \
		/* Only lock IRQ for a very narrow window. */                           \
		__disable_irq();                                                        \
                                                                                \
		/* Tricky - we start the ADC BEFORE we transition the pin.  By doing    \
			this We are catching it onthe slope much more effectively.  */      \
		ADC1->CTLR2 = ADC_SWSTART | ADC_ADON | ADC_EXTSEL;                      \
                                                                                \
		ADD_N_NOPS( n )                                                         \
                                                                                \
	funDigitalWrite( pinset[0], 0 );											\
	funDigitalWrite( pinset[1], 0 );											\
	funDigitalWrite( pinset[2], 0 );											\
	funDigitalWrite( pinset[3], 0 );											\
																			    \
		/* Sampling actually starts here, somewhere, so we can let other        \
			interrupts run */                                                   \
		__enable_irq();                                                         \
		while(!(ADC1->STATR & ADC_EOC));                                        \
	funDigitalWrite( pinset[0], 1 );											\
	funDigitalWrite( pinset[1], 1 );											\
	funDigitalWrite( pinset[2], 1 );											\
	funDigitalWrite( pinset[3], 1 );											\
		ret += ADC1->RDATAR;                                                    \
	}
#endif



#if TOUCH_FLAT == 1
#define RELEASEIO io->BSHR = 1<<(portpin+16*TOUCH_SLOPE); io->CFGLR = CFGFLOAT;
#else
#define RELEASEIO io->CFGLR = CFGFLOAT; io->BSHR = 1<<(portpin+16*TOUCH_SLOPE);
#endif

#define INNER_LOOP( n ) \
	{ \
		/* Only lock IRQ for a very narrow window. */                           \
		__disable_irq();                                                        \
                                                                                \
		/* Tricky - we start the ADC BEFORE we transition the pin.  By doing    \
			this We are catching it onthe slope much more effectively.  */      \
		ADC1->CTLR2 = ADC_SWSTART | ADC_ADON | ADC_EXTSEL;                      \
                                                                                \
		ADD_N_NOPS( n )                                                         \
                                                                                \
		RELEASEIO                                                               \
																			    \
		/* Sampling actually starts here, somewhere, so we can let other        \
			interrupts run */                                                   \
		__enable_irq();                                                         \
		while(!(ADC1->STATR & ADC_EOC));                                        \
		io->CFGLR = CFGDRIVE;                                                   \
		io->BSHR = 1<<(portpin+(16*(1-TOUCH_SLOPE)));                          \
		ret += ADC1->RDATAR;                                                    \
	}


	int i;
	for( i = 0; i < iterations; i++ )
	{
		// Wait a variable amount of time based on loop iteration, in order
		// to get a variety of RC points and minimize DNL.

		INNER_LOOP( 0 );
		//INNER_LOOP( 2 );
		//INNER_LOOP( 4 );
	}

	for( group = 0; group < 4; group++ )
		funPinMode( pinset[group], drivepr );

	funPinMode( LEDSEG0, drivepr );
	funPinMode( LEDSEG1, drivepr );
	funPinMode( LEDSEG2, drivepr );
	funPinMode( LEDSEG3, drivepr );
	funPinMode( LEDSEG4, drivepr );
	funPinMode( LEDSEG5, drivepr );
	return ret;
}
int ReadTouch() __attribute__((noinline)) __attribute((section(".srodata")));
int ReadTouch()
{
	return ReadTouchPinSpecial( GPIOD, 6, 6, 2 ) + ReadTouchPinSpecial( GPIOD, 2, 3, 2 );
		// ReadTouchPinSpecial( GPIOC, 4, 2, 2 ) +
}

int main()
{
	SystemInit();
	Delay_Ms(3); // Ensures USB re-enumeration after bootloader or reset; Spec demand >2.5µs ( TDDIS )
	usb_setup();

	funGpioInitAll();
	RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
	InitTouchADC();

	printf( "Hello!\n" );

	int id = 0;
	int didaffect = 0;

	int calstage = 32;
	int cal = 0;
	int press = 0;
	uint32_t sum = 0;
	int dv = 0;
	while(1)
	{
		id++;
		int group;

		#define drivepr GPIO_CFGLR_IN_PUPD

		sum += ReadTouch();

		if( (id & 7) == 7 )
		{
			if( calstage > 0 )
			{
				calstage--;
				if( calstage < 16 ) cal += sum;
			}
			else
			{
				int event = 0;
				int rezero = sum-(cal>>4);
				if( rezero < -5 ) cal-=10;
				if( rezero > 1 ) cal++;
				if( rezero > 2000 && press == 0 ) { press = 1; event = 1; }
				if( rezero < 200 && press == 1) { press = 0; event = 1; }

				//printf( "%d\n", rezero );
				//if( rezero > 0 ) dv = rezero;
				if( event && press )
					dv++;
				//	printf( "*** %d\n", press );
			}
			sum = 0;
		}

		UpdateLCD( ComputeLCDMaskWithNumber( dv ) );
		Delay_Ms(1);
	}
	while(1)
	{
		if( !didaffect )
		{
			id++;
			UpdateLCD( ComputeLCDMaskWithNumber( id >> 4 ) );
		}
		else
		{
			UpdateLCD( lastmask );
		}
		//printf( "%lu %lu %lu %08lx\n", rv003usb_internal_data.delta_se0_cyccount, rv003usb_internal_data.last_se0_cyccount, rv003usb_internal_data.se0_windup, RCC->CTLR );
#if RV003USB_EVENT_DEBUGGING
		uint32_t * ue = GetUEvent();
		if( ue )
		{
			printf( "%lu %lx %lx %lx\n", ue[0], ue[1], ue[2], ue[3] );
		}
#endif
		if( start_leds )
		{
			memcpy( &lastmask, scratch+1, 4 );
			didaffect = 1;
			start_leds = 0;
			frame++;
		}
	}
}

void usb_handle_user_in_request( struct usb_endpoint * e, uint8_t * scratchpad, int endp, uint32_t sendtok, struct rv003usb_internal * ist )
{
	// Make sure we only deal with control messages.  Like get/set feature reports.
	if( endp )
	{
		usb_send_empty( sendtok );
	}
}

void usb_handle_user_data( struct usb_endpoint * e, int current_endpoint, uint8_t * data, int len, struct rv003usb_internal * ist )
{
	//LogUEvent( SysTick->CNT, current_endpoint, e->count, 0xaaaaaaaa );
	int offset = e->count<<3;
	int torx = e->max_len - offset;
	if( torx > len ) torx = len;
	if( torx > 0 )
	{
		memcpy( scratch + offset, data, torx );
		e->count++;
		if( ( e->count << 3 ) >= e->max_len )
		{
			start_leds = e->max_len;
		}
	}
}

void usb_handle_hid_get_report_start( struct usb_endpoint * e, int reqLen, uint32_t lValueLSBIndexMSB )
{
	if( reqLen > sizeof( scratch ) ) reqLen = sizeof( scratch );

	// You can check the lValueLSBIndexMSB word to decide what you want to do here
	// But, whatever you point this at will be returned back to the host PC where
	// it calls hid_get_feature_report. 
	//
	// Please note, that on some systems, for this to work, your return length must
	// match the length defined in HID_REPORT_COUNT, in your HID report, in usb_config.h

	e->opaque = scratch;
	e->max_len = reqLen;
}

void usb_handle_hid_set_report_start( struct usb_endpoint * e, int reqLen, uint32_t lValueLSBIndexMSB )
{
	// Here is where you get an alert when the host PC calls hid_send_feature_report.
	//
	// You can handle the appropriate message here.  Please note that in this
	// example, the data is chunked into groups-of-8-bytes.
	//
	// Note that you may need to make this match HID_REPORT_COUNT, in your HID
	// report, in usb_config.h

	if( reqLen > sizeof( scratch ) ) reqLen = sizeof( scratch );
	e->max_len = reqLen;
}


void usb_handle_other_control_message( struct usb_endpoint * e, struct usb_urb * s, struct rv003usb_internal * ist )
{
	LogUEvent( SysTick->CNT, s->wRequestTypeLSBRequestMSB, s->lValueLSBIndexMSB, s->wLength );
}


