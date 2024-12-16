#include "ch32v003fun.h"
#include <stdio.h>
#include <string.h>
#include "rv003usb.h"

//#include "ch32v003_touch.h"

// Allow reading and writing to the scratchpad via HID control messages.
uint8_t scratch[63];
uint8_t scratchi[63];
volatile uint8_t start_out = 0;
volatile uint8_t start_in = 0;
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
#define ALLSEGMASK (0x3f)
static const uint8_t pinset[4] = { LCDCOM1, LCDCOM2, LCDCOM3, LCDCOM4 };


// from host
static uint32_t lastmask;
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

void RunTouch()
{
	// Touch runs for about 1ms.
	// Configure touch!
	// Touch pin = 3 or 6 
	// I tried seeing if you could get data by measuring one or the other, and the answer was basically no.
	// I couldn't get any extra data.
	int reads = 0;
	int group;
	const int oversample = 300; // Has significant impact over contrast.

#define FORCEALIGNADC \
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

	int ttv = 0;

	const int adcno = 3;
	#define SPECIAL_TOUCH_ADC_SAMPLE_TIME 1

	__disable_irq();
	FORCEALIGNADC
	ADC1->RSQR3 = adcno;
	ADC1->SAMPTR2 = SPECIAL_TOUCH_ADC_SAMPLE_TIME<<(3*adcno);
	__enable_irq();

	Delay_Us(1);
	for( reads = 0; reads < oversample; reads++ )
	{

#if LOCKIN_AMP_MODE
		// Lock-in amplifier mode only useful for when we can control timing of inner loop
		// I.e. if using DMA to do touch reads.  Don't do this 

		__disable_irq();
		FORCEALIGNADC
		ADC1->CTLR2 = ADC_SWSTART | ADC_ADON | ADC_EXTSEL;
		LCDSEGBUF->BSHR = 0x3f<<16;
		LCDCOMBUF->BSHR = ALLCOMMASK<<16;
		__enable_irq();
		while(!(ADC1->STATR & ADC_EOC));
		int tvu = ADC1->RDATAR;
#else
		// Non-lock-in mode (You should use this)
		LCDSEGBUF->BSHR = 0x3f<<16;
		LCDCOMBUF->BSHR = ALLCOMMASK<<16;
		for( group = 0; group < 4; group++ )
			funPinMode( pinset[group], GPIO_CFGLR_OUT_2Mhz_PP );
		funPinMode( LCDSEG0, GPIO_CFGLR_OUT_2Mhz_PP );
		funPinMode( LCDSEG1, GPIO_CFGLR_OUT_2Mhz_PP );
		funPinMode( LCDSEG2, GPIO_CFGLR_OUT_2Mhz_PP );
		funPinMode( LCDSEG3, GPIO_CFGLR_OUT_2Mhz_PP );
		funPinMode( LCDSEG4, GPIO_CFGLR_OUT_2Mhz_PP );
		funPinMode( LCDSEG5, GPIO_CFGLR_OUT_2Mhz_PP );

		for( group = 0; group < 4; group++ )
			funPinMode( pinset[group], GPIO_CFGLR_IN_PUPD );
		funPinMode( LCDSEG0, GPIO_CFGLR_IN_PUPD );
		funPinMode( LCDSEG1, GPIO_CFGLR_IN_PUPD );
		funPinMode( LCDSEG2, GPIO_CFGLR_IN_PUPD );
		funPinMode( LCDSEG3, GPIO_CFGLR_IN_PUPD );
		funPinMode( LCDSEG4, GPIO_CFGLR_IN_PUPD );
		funPinMode( LCDSEG5, GPIO_CFGLR_IN_PUPD );
#endif

		__disable_irq();
		FORCEALIGNADC
		ADC1->CTLR2 = ADC_SWSTART | ADC_ADON | ADC_EXTSEL;
		LCDSEGBUF->BSHR = 0x3f;
		LCDCOMBUF->BSHR = ALLCOMMASK;
		__enable_irq();
		while(!(ADC1->STATR & ADC_EOC));
		int tvd = ADC1->RDATAR;
#if LOCKIN_AMP_MODE
		ttv += tvu - tvd;
#else
		ttv += tvd;
#endif
	}
	touchval += ttv;
}

// Background for below code:
// The bottom 16 bits of BSHRC says to set these IO pins high.
// The top 16 bits of BSHRC says to turn off the corresponding IO pins.

void DitherIO( volatile uint32_t * bshr, uint32_t da, uint32_t db, int iterations ) __attribute__((noinline));
void DitherIO( volatile uint32_t * bshr, uint32_t da, uint32_t db, int iterations )
{
	// This is not written this way because it needs to be fast, but that I want to make sure
	// that it takes as many cycles on the high-side as it does on the low-side when dithering.
	// Loops are realy slow on the 003, and no reason not to make it bigger.
	//
	if( iterations == 0 ) return;

	asm volatile(
		".balign 4\n"
		"1:\n"
		"	c.sw %[db], 0(%[bshr])\n"
		"	c.nop\n"
		"	c.nop\n"
		"	c.addi %[iterations], -1\n"
		"	c.beqz %[iterations], 2f\n"
		"	c.sw %[da], 0(%[bshr])\n"
		"	c.j 1b\n"
		"2:"
		: [iterations]"+r"(iterations)
		: [bshr]"r"(bshr),
		  [da]"r"(da),
		  [db]"r"(db)
		:  );	
}

void UpdateLCD( uint32_t mask )
{
	int group;
	const int dithers = 8000;

	#define drivepr GPIO_CFGLR_IN_PUPD
	for( group = 0; group < 4; group++ )
		funPinMode( pinset[group], drivepr );
	funPinMode( LCDSEG0, drivepr );
	funPinMode( LCDSEG1, drivepr );
	funPinMode( LCDSEG2, drivepr );
	funPinMode( LCDSEG3, drivepr );
	funPinMode( LCDSEG4, drivepr );
	funPinMode( LCDSEG5, drivepr );

	static const uint8_t commasks[4] = { LCDCOMMASK1, LCDCOMMASK2, LCDCOMMASK3, LCDCOMMASK4 };

	int tm = ~mask;
	// Reset SEG mask for this run.
	LCDSEGBUF->BSHR = ALLSEGMASK<<16;
	for( group = 0; group < 4; group++ )
	{
		int commasknthis = commasks[group] ^ ALLCOMMASK;

		LCDSEGBUF->BSHR = tm&ALLSEGMASK;
		DitherIO( &LCDCOMBUF->BSHR, (commasknthis<<16), ALLCOMMASK, dithers );
		LCDSEGBUF->BSHR = ALLSEGMASK<<16;
		tm>>=6;
	}

	// Turn off all SEGs (all COMs are off here too)
	LCDCOMBUF->BSHR = ALLCOMMASK<<16;

	// Delay some time - this is the "clear" time.
	// This should be balanced with the bottom (Theoretically)
	RunTouch();

	// Now, we invert our mask (what segments we are trying to command on/off)
	tm = ~mask;

	// Set seg mask for this run.
	LCDSEGBUF->BSHR = ALLSEGMASK;
	for( group = 0; group < 4; group++ )
	{
		int commasknthis = (commasks[group] ^ ALLCOMMASK);

		LCDSEGBUF->BSHR = tm<<16;
		DitherIO( &LCDCOMBUF->BSHR, commasknthis, ALLCOMMASK<<16, dithers );
		LCDSEGBUF->BSHR = ALLSEGMASK;
		tm>>=6;
	}
	// Turn on SEGs off (All COMs are on here too)
	LCDCOMBUF->BSHR = ALLCOMMASK;

	RunTouch();

	// Not sure why this is needed?
	Delay_Us(1); 
}

int main()
{
	SystemInit();
	Delay_Ms(3); // Ensures USB re-enumeration after bootloader or reset; Spec demand >2.5µs ( TDDIS )
	usb_setup();

	funGpioInitAll();
	RCC->APB2PCENR |= RCC_APB2Periph_ADC1;

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
		if( !didaffect )
		{
			UpdateLCD( ComputeLCDMaskWithNumber( dv ) );
		}
		else
		{
			UpdateLCD( lastmask );
		}

		touchsum += touchval;
		touchval = 0;

		if( ++touchstate >= 1 )
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
				if( rezero > 100 ) cal-=1000;
				if( rezero < -100 ) cal+=10000;
				if( rezero > 4000 && press == 0 ) { press = 1; event = 1; }
				if( rezero < 2000 && press == 1) { press = 0; event = 1; }
				if( event && press )
					dv++;
			}
			memcpy( scratchi + 1, &touchsum, 4 );
			scratchi[0] = 0xaa;
			scratchi[5] = dv;
			touchsum = touchstate = 0;
		}

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


