#include <SPI.h>
#include <RF24.h>
#include <RF24BLE.h>
#include <printf.h>

#include "irk.h"




#define IRK_LIST_NUMBER 1
char * IrkListName[IRK_LIST_NUMBER] = {"A"};
uint8_t irk[IRK_LIST_NUMBER][ESP_BT_OCTET16_LEN]= 
{
	//IRK of VAL
	{0x2C,0xD1,0xC5,0xBB,0xDF,0xAB,0x8C,0xE2,0x55,0x01,0x73,0xDB,0x88,0x3B,0xF9,0xC2}
};



#define MAC_LEN 6
#define RECV_PAYLOAD_SIZE 28

/* Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 */
RF24 radio(7, 8);
RF24BLE BLE(radio);
/**********************************************************/

void BleDataCheckTask();
unsigned char input[32]={0};
//there are 3 channels at which BLE broadcasts occur
//hence channel can be 0,1,2 
byte channel =0; //using single channel to receive

//counters so we can see the radio is alive even when nothing resolves yet
unsigned long lastStatsPrint = 0;
unsigned long timeoutCount = 0;
unsigned long corruptCount = 0;
unsigned long validCount = 0;

void setup()
{
    Serial.begin(115200);
    Serial.println(F("RF24_BLE_address"));
    printf_begin();

	BLE.recvBegin(RECV_PAYLOAD_SIZE,channel);

	if(!radio.isChipConnected())
	{
		Serial.println(F("ERROR: nRF24L01 not detected, check CE/CSN/SPI wiring!"));
	}
	radio.printDetails();
}




void loop()
{
	BleDataCheckTask();
} // Loop





void BleDataCheckTask()
{
	byte status=BLE.recvPacket((uint8_t*)input,RECV_PAYLOAD_SIZE,channel);

	if(status==RF24BLE_TIMEOUT){ timeoutCount++; }
	else if(status==RF24BLE_CORRUPT){ corruptCount++; }
	else if(status==RF24BLE_VALID){ validCount++; }

	//heartbeat every 2s so we know the loop/radio is alive with no visible activity
	if(millis()-lastStatsPrint>2000)
	{
		lastStatsPrint=millis();
		printf("stats: valid=%lu corrupt=%lu timeout=%lu\r\n",validCount,corruptCount,timeoutCount);
	}

	if(status==RF24BLE_TIMEOUT){ return; } //nothing received on this channel in time

	//dump the raw packet: the PDU header byte varies a lot between phones/PDU types,
	//so print it instead of silently dropping anything that isn't exactly 0x40
	printf("%s pduType=%02X raw=",status==RF24BLE_VALID?"VALID  ":"CORRUPT",input[0]);
	for (byte i = 0; i < RECV_PAYLOAD_SIZE; i++){ printf("%02X ",input[i]); }
	printf("\r\n");

	//AdvA (the MAC) sits right after the header+length bytes for every ADV PDU type
	//(ADV_IND, ADV_NONCONN_IND, ADV_SCAN_IND, SCAN_RSP, ...), so resolve it regardless
	//of the header byte value - the whitening of these first bytes doesn't depend on
	//the assumed total packet length, so it's valid even when the CRC check is CORRUPT.
	unsigned char AdMac[MAC_LEN];
	for (byte i = 0; i < MAC_LEN; i++)
	{
		AdMac[MAC_LEN-1-i] = input[i+2];
	}

	for (byte i = 0; i < IRK_LIST_NUMBER; i++)
	{
		//Check with all IRK we got one by one.
		if(btm_ble_addr_resolvable(AdMac,irk[i]))
		{
			printf("MacAdd= %02X %02X %02X %02X %02X %02X Belongs to:%s\r\n"
				,AdMac[0],AdMac[1],AdMac[2],AdMac[3],AdMac[4],AdMac[5]
			,IrkListName[i]);
		}
	}
}


