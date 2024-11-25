#include "ch32v003fun.h"
#include <stdio.h>
#include <string.h>
#include "rv003usb.h"


#include "ch32v003_touch.h"

// Allow reading and writing to the scratchpad via HID control messages.
uint8_t scratch[63];
uint8_t scratchi[63];
volatile uint8_t start_out = 0;
volatile uint8_t start_in = 0;
uint8_t ledat;

#define LCDCOM1 PD0
#define LCDCOM2 PD7
#define LCDCOM3 PD6
#define LCDCOM4 PD2

#define LCDCOMBUF GPIOD
#define LCDCOMMASK ((1<<0) | (1<<7) | (1<<6) | (1<<2))
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
static uint32_t touchval;

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
	for( group = 0; group < 4; group++ )
		funPinMode( pinset[group], drivepr );
	funPinMode( LEDSEG0, drivepr );
	funPinMode( LEDSEG1, drivepr );
	funPinMode( LEDSEG2, drivepr );
	funPinMode( LEDSEG3, drivepr );
	funPinMode( LEDSEG4, drivepr );
	funPinMode( LEDSEG5, drivepr );



#if 0
		// kinda works don't recommend
		LCDSEGBUF->BSHR = 0x3f<<16;
		for( group = 0; group < 4; group++ )
			funPinMode( pinset[group], GPIO_CFGLR_IN_FLOAT );
		LCDCOMBUF->BSHR = LCDCOMMASK<<16;
		for( group = 0; group < 4; group++ )
		{
			LCDSEGBUF->BSHR = lmask&0x3f;
			//funDigitalWrite( pinset[group], 0 );
			funPinMode( pinset[group], GPIO_CFGLR_IN_PUPD );
			Delay_Us(120); // Set higher to increase constrast
			LCDSEGBUF->BSHR = 0x3f<<16;
			Delay_Us(10); // Set higher to reduce contrast
			funDigitalWrite( pinset[group], 1 );
			Delay_Us(1); // Set higher to reduce contrast
			funPinMode( pinset[group], GPIO_CFGLR_IN_FLOAT );
			lmask>>=6;
		}
#endif

	// Regular, not floating way.

	LCDSEGBUF->BSHR = 0x3f<<16;
	LCDCOMBUF->BSHR = LCDCOMMASK;
	int lmask = mask;
	for( group = 0; group < 4; group++ )
	{
		LCDSEGBUF->BSHR = lmask&0x3f;
		funDigitalWrite( pinset[group], 0 );
		Delay_Us(280); // Set higher to increase constrast
		funDigitalWrite( pinset[group], 1 );
		LCDSEGBUF->BSHR = 0x3f<<16;
		Delay_Us(10); // Set higher to reduce contrast -- but also may brighten up some of the digit.
		lmask>>=6;
	}
	// Set all pins high preparing for touch.
	for( group = 0; group < 4; group++ )
		funPinMode( pinset[group], GPIO_CFGLR_IN_PUPD );
	funPinMode( LEDSEG0, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG1, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG2, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG3, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG4, GPIO_CFGLR_IN_FLOAT );
	funPinMode( LEDSEG5, GPIO_CFGLR_IN_FLOAT );

	for( group = 0; group < 4; group++ )
		funDigitalWrite( pinset[group], 1 );
	LCDSEGBUF->BSHR = 0x3f;

	// Configure touch!
	// Touch pin = 3 or 6 
	// I tried seeing if you could get data by measuring one or the other, and the answer was basically no.
	// I couldn't get any extra data.
	int reads = 0;
	const int adcno = 3;
	const int oversample = 150; // Has significant impact over contrast.

#if 1
#define FORCEALIGN \
	asm volatile( \
		"\n\
		.balign 4\n\
		andi a2, %[cyccnt], 3\n\
		c.slli a2, 1\n\
		c.addi a2, 12\n\
		auipc a1, 0\n\
		c.add  a2, a1\n\
		jalr a2, 1\n\
		.long 0x00010001\n\
		.long 0x00010001\n\
		"\
		:: [cyccnt]"r"(SysTick->CNT) : "a1", "a2"\
	);
#else
#define FORCEALIGN \
	asm volatile( ".balign 8");
#endif

	ADC1->RSQR3 = adcno;
	#define SPECIAL_TOUCH_ADC_SAMPLE_TIME 2
	ADC1->SAMPTR2 = SPECIAL_TOUCH_ADC_SAMPLE_TIME<<(3*adcno);
	int ttv = 0;
	//FORCEALIGN
	for( reads = 0; reads < oversample; reads++ )
	{
		__disable_irq();
		FORCEALIGN
		ADC1->CTLR2 = ADC_SWSTART | ADC_ADON | ADC_EXTSEL;
		LCDSEGBUF->BSHR = 0x3f<<16;
		LCDCOMBUF->BSHR = LCDCOMMASK<<16;
		__enable_irq();
		while(!(ADC1->STATR & ADC_EOC));
		int tvu = ADC1->RDATAR;

		__disable_irq();
		FORCEALIGN
		ADC1->CTLR2 = ADC_SWSTART | ADC_ADON | ADC_EXTSEL;
		LCDSEGBUF->BSHR = 0x3f;
		LCDCOMBUF->BSHR = LCDCOMMASK;
		__enable_irq();
		while(!(ADC1->STATR & ADC_EOC));
		int tvd = ADC1->RDATAR;
		ttv += tvd - tvu;
	}
	touchval = ttv;
	LCDSEGBUF->BSHR = 0x3f<<16;

	// For ??? Reason, if we wait longer here,
	// it causes the display to uniformly fade.
	// This is actually good to prevent oversaturation
	// but it's still weird.
	//
	// We can add delay time by just increasing oversampling.
	// Delay_Us(100);

	funPinMode( LEDSEG0, GPIO_CFGLR_IN_PUPD );
	funPinMode( LEDSEG1, GPIO_CFGLR_IN_PUPD );
	funPinMode( LEDSEG2, GPIO_CFGLR_IN_PUPD );
	funPinMode( LEDSEG3, GPIO_CFGLR_IN_PUPD );
	funPinMode( LEDSEG4, GPIO_CFGLR_IN_PUPD );
	funPinMode( LEDSEG5, GPIO_CFGLR_IN_PUPD ); // GPIO_CFGLR_IN_PUPD

	// This slightly uniformyl darkens everything.s
	Delay_Us(280);
}

int main()
{
	SystemInit();
	Delay_Ms(3); // Ensures USB re-enumeration after bootloader or reset; Spec demand >2.5µs ( TDDIS )
	usb_setup();

	funGpioInitAll();
	RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
	InitTouchADC();

	//printf( "Hello!\n" );

	int id = 0;
	int didaffect = 0;

	int calstage = 20;
	int cal = 0;
	int press = 0;
	int touchstate = 0;
	int touchsum = 0;

	int dv = 0;
	while(1)
	{
		id++;
		int group;
		if( !didaffect )
		{
			UpdateLCD( ComputeLCDMaskWithNumber( dv ) );
		}
		else
		{
			UpdateLCD( lastmask );
		}
		touchsum += touchval;

		if( touchstate++ > 20 )
		{
			//printf( "%d\n", touchsum );
			if( calstage > 0 )
			{
				calstage--;
				if( calstage < 16 ) cal += touchsum;
			}
			else
			{
				int event = 0;
				int rezero = (cal>>4) - touchsum;
				if( rezero > 1000 ) cal-=10000;
				if( rezero < -2000 ) cal+=20000;
				if( rezero > 60000 && press == 0 ) { press = 1; event = 1; }
				if( rezero < 20000 && press == 1) { press = 0; event = 1; }

				if( event && press )
					dv++;
			}
			memcpy( scratchi + 1, &touchsum, 4 );
			scratchi[0] = 0xaa;
			scratchi[5] = dv;
			touchsum = touchstate = 0;
		}
		//Delay_Ms(1);

		//printf( "%lu %lu %lu %08lx\n", rv003usb_internal_data.delta_se0_cyccount, rv003usb_internal_data.last_se0_cyccount, rv003usb_internal_data.se0_windup, RCC->CTLR );
#if RV003USB_EVENT_DEBUGGING
		uint32_t * ue = GetUEvent();
		if( ue )
		{
			printf( "%lu %lx %lx %lx\n", ue[0], ue[1], ue[2], ue[3] );
		}
#endif
		if( start_out )
		{
			memcpy( &lastmask, scratch+1, 4 );
			didaffect = 1;
			start_out = 0;
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
			start_out = e->max_len;
		}
	}
}

void usb_handle_hid_get_report_start( struct usb_endpoint * e, int reqLen, uint32_t lValueLSBIndexMSB )
{
	if( reqLen > sizeof( scratchi ) ) reqLen = sizeof( scratchi );

	// You can check the lValueLSBIndexMSB word to decide what you want to do here
	// But, whatever you point this at will be returned back to the host PC where
	// it calls hid_get_feature_report. 
	//
	// Please note, that on some systems, for this to work, your return length must
	// match the length defined in HID_REPORT_COUNT, in your HID report, in usb_config.h

	e->opaque = scratchi;
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


