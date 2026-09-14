#include <RF24BLE.h>
#include <SPI.h>
#include <RF24.h>
#include <printf.h>

#include "irk.h"




#define IRK_LIST_NUMBER 1
char * IrkListName[IRK_LIST_NUMBER] = {"VAL"};
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
//BLE emits each advertising event on ALL 3 primary channels (37,38,39 = index 0,1,2)
//within a few ms, so a single fixed channel already catches every event with zero
//gaps. We camp on channel 38 (index 1, 2426 MHz) since it sits between WiFi
//channels and is usually the cleanest.
byte channel =1;

//counters so we can see the radio is alive even when nothing resolves yet
unsigned long lastStatsPrint = 0;
unsigned long timeoutCount = 0;
unsigned long corruptCount = 0;
unsigned long validCount = 0;

//presence tracking: a phone is considered gone if not resolved again within this window
//kept generous since a locked/idle iPhone advertises far less often than when active
#define PRESENCE_TIMEOUT_MS 15000
bool present[IRK_LIST_NUMBER] = {false};
unsigned long lastSeen[IRK_LIST_NUMBER] = {0};

void checkPresenceTimeouts();

void setup()
{
    Serial.begin(115200);
    Serial.println(F("RF24_BLE_address"));
    printf_begin();

	BLE.recvBegin(RECV_PAYLOAD_SIZE,channel);

	//don't run the main loop against a dead radio - it just produces meaningless noise
	while(!radio.isChipConnected())
	{
		Serial.println(F("ERROR: nRF24L01 not detected, check VCC=3.3V/CE/CSN/SPI wiring!"));
		delay(1000);
	}
	Serial.println(F("nRF24L01 detected."));
	radio.printDetails();
}




void loop()
{
	BleDataCheckTask();
	checkPresenceTimeouts();
} // Loop

void checkPresenceTimeouts()
{
	for (byte i = 0; i < IRK_LIST_NUMBER; i++)
	{
		if(present[i] && millis()-lastSeen[i]>PRESENCE_TIMEOUT_MS)
		{
			present[i]=false;
			printf("OUT: %s\r\n",IrkListName[i]);
		}
	}
}





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

	if(status!=RF24BLE_VALID){ return; } //corrupt/timeout: only counted in the heartbeat above

	//dump the raw packet: the PDU header byte varies a lot between phones/PDU types,
	//so print it instead of silently dropping anything that isn't exactly 0x40
	
	/*
	printf("VALID pduType=%02X raw=",input[0]);
	for (byte i = 0; i < RECV_PAYLOAD_SIZE; i++){ printf("%02X ",input[i]); }
	printf("\r\n");
	*/

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

			lastSeen[i]=millis();
			if(!present[i])
			{
				present[i]=true;
				printf("NEAR: %s\r\n",IrkListName[i]);
			}
		}
	}
}


