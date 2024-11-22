#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// We borrow the combined hidapi.c from minichlink.
//
// This is for total perf testing.

#include "hidapi.c"

#include "os_generic.h"


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
int main()
{
	hid_device * hd = hid_open( 0x1209, 0xd003, L"TINYLCD7SEG"); // third parameter is "serial"
	if( !hd )
	{
		fprintf( stderr, "Error: Failed to open device.\n" );
		return -4;
	}

	// Size of buffers must match the report descriptor size in the special_hid_desc
	//  NOTE: You are permitted to have multiple entries.
	uint8_t buffer0[7] = { 0 }; // NOTE: This must be ONE MORE THAN what is in the hid descriptor.
	uint8_t buffer1[7] = { 0 };
	int r;
	int i;
	int j;
	int retries = 0;
	double dStart = OGGetAbsoluteTime();
	double dSecond = dStart;
	double dStartSend = 0.0;
	double dSendTotal = 0;
	double dRecvTotal = 0;
	for( j = 0; ; j++ )
	{
		buffer0[0] = 0xaa; // First byte must match the ID.

		uint32_t mask = ComputeLCDMaskWithNumber( j );
		memcpy( buffer0 + 1, &mask, 4 );

		retrysend:
		
		dStartSend = OGGetAbsoluteTime();
		r = hid_send_feature_report( hd, buffer0, sizeof(buffer0) );
		dSendTotal += OGGetAbsoluteTime() - dStartSend;
		if( r != sizeof(buffer0) )
		{
			fprintf( stderr, "Warning: HID Send fault (%d) Retrying\n", r );
			retries++;
			if( retries > 10 ) break;
			goto retrysend;
		}

		retries = 0;
		printf( "<" ); // Print this out meaning we sent the data.

		memset( buffer1, 0xff, sizeof( buffer1 ) );
		buffer1[0] = 0xaa; // First byte must be ID.

		double dStartRecv = OGGetAbsoluteTime();
		r = hid_get_feature_report( hd, buffer1, sizeof(buffer1) );
		dRecvTotal += OGGetAbsoluteTime() - dStartRecv;

		printf( ">" ); fflush( stdout);

		if( r != sizeof( buffer1 ) && r != sizeof( buffer1 ) + 1) { printf( "Got %d\n", r ); break; }

		// Validate the scratches matched.
		if( memcmp( buffer0, buffer1, sizeof( buffer0 ) ) != 0 ) 
		{
			printf( "%d: ", r );
			for( i = 0; i < r; i++ )
				printf( "[%d] %02x>%02x %s", i, buffer0[i], buffer1[i], (buffer1[i] != buffer0[i])?"MISMATCH ":""  );
			printf( "\n" );
			break;
		}
		
		if( dStartRecv - dSecond > 1.0 )
		{
			printf( "\n%2.3f xa/s\n", j / dRecvTotal );
			dSecond++;
		}
	}

	hid_close( hd );
}

