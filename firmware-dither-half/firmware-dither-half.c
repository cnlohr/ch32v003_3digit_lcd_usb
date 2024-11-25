#include "ch32v003fun.h"
#include <stdio.h>
#include <string.h>
#include "rv003usb.h"

// Allow reading and writing to the scratchpad via HID control messages.
uint8_t scratch[63];
volatile uint8_t start_leds = 0;
uint8_t ledat;

#define LCDCOMBUF GPIOD

#define LCDCOM1 PD0
#define LCDCOM2 PD7
#define LCDCOM3 PD6
#define LCDCOM4 PD2
#define LCDCOMMASK1 (1<<0)
#define LCDCOMMASK2 (1<<7)
#define LCDCOMMASK3 (1<<6)
#define LCDCOMMASK4 (1<<2)
#define ALLCOMMASK (LCDCOMMASK1|LCDCOMMASK2|LCDCOMMASK3|LCDCOMMASK4)

#define LCDSEGBUF GPIOC

#define LCDSEG0 PC0
#define LCDSEG1 PC1
#define LCDSEG2 PC2
#define LCDSEG3 PC3
#define LCDSEG4 PC4
#define LCDSEG5 PC5

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
	uint8_t pinset[4] = { LCDCOM1, LCDCOM2, LCDCOM3, LCDCOM4 };
	uint8_t commasks[4] = { LCDCOMMASK1, LCDCOMMASK2, LCDCOMMASK3, LCDCOMMASK4 };

	#define drivepr GPIO_CFGLR_IN_PUPD

	for( group = 0; group < 4; group++ )
		funPinMode( pinset[group], drivepr );
	funPinMode( LCDSEG0, drivepr );
	funPinMode( LCDSEG1, drivepr );
	funPinMode( LCDSEG2, drivepr );
	funPinMode( LCDSEG3, drivepr );
	funPinMode( LCDSEG4, drivepr );
	funPinMode( LCDSEG5, drivepr );
	LCDSEGBUF->BSHR = 0x3f<<16;
	const int dithers = 2000;

	int tm = ~mask;
	for( group = 0; group < 4; group++ )
	{
		int d;
		int ditherA = ALLCOMMASK;
		int commasknthis = commasks[group] ^ ALLCOMMASK;
		int ditherB = (commasknthis<<16);

		LCDSEGBUF->BSHR = tm&0x3f;
		for( d = 0; d < dithers; d++ )
		{
			LCDCOMBUF->BSHR = ditherA;
			LCDCOMBUF->BSHR = ditherA;
			LCDCOMBUF->BSHR = ditherA;
			LCDCOMBUF->BSHR = ditherA;
			LCDCOMBUF->BSHR = ditherB;
		}
		LCDSEGBUF->BSHR = 0x3f<<16;
		tm>>=6;
	}
	// section 1 zero
	LCDCOMBUF->BSHR = ALLCOMMASK<<16;
	Delay_Us(0);
	tm = ~mask;
	LCDSEGBUF->BSHR = 0x3f;
	for( group = 0; group < 4; group++ )
	{
		int d;
		int ditherA = ALLCOMMASK<<16;
		int commasknthis = commasks[group] ^ ALLCOMMASK;
		int ditherB = (commasknthis);

		LCDSEGBUF->BSHR = tm<<16;
		for( d = 0; d < dithers; d++ )
		{
			LCDCOMBUF->BSHR = ditherA;
			LCDCOMBUF->BSHR = ditherA;
			LCDCOMBUF->BSHR = ditherA;
			LCDCOMBUF->BSHR = ditherA;
			LCDCOMBUF->BSHR = ditherB;
		}
		LCDSEGBUF->BSHR = 0x3f;
		tm>>=6;
	}
	LCDCOMBUF->BSHR = ALLCOMMASK;
	Delay_Us(1000);

}

int main()
{
	Delay_Ms(3); // Ensures USB re-enumeration after bootloader or reset; Spec demand >2.5µs ( TDDIS )
	usb_setup();
	SystemInit();

	funGpioInitAll();

	int id = 0;
	int didaffect = 0;
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


