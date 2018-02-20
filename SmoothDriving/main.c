/*
 * GPS.c
 *
 * Created: 11/7/2017 10:56:00 PM
 * Author : matth
 */ 

#define F_CPU 16000000UL

#define MAXIDLETIME 60*5 // 5 minutes

#define DEBUG 1


#include <avr/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include "GPSFunc/GPSFunctions.h"
#include "USART/USARTFunc.h"
#include "GPSDecoder/GPSDecoder.h"
#include "Display/16x2_Display.h"
#include "RecordLoader/RecordLoader.h"
#include "PowerManager/PowerManager.h"

volatile float syncTime = 0;
volatile float lastIntersectionChangeTime = 0;
void (*SendString)(char*) = &SendUARTString;
void PrintIntersectionUART(intersection intSec);
/**
    The WaitForGPSFix() function blocks until a gps fix is found.
 */
void WaitForGPSFix()
{
	while(!IsGPSFixed())
	{
		while(!IsNewGPSData());
	}
}

/**
    The WaitForSDMount() function blocks until the SD card is mounted
 */
void WaitForSDMount()
{
	bool mounted = InitializeStorageDevice();
	while(!mounted)
	{
		_delay_ms(250);
		mounted = MountStorageDevice();
	}
}
/**
    The TimeSinceLastMovement() function is unimplemented.
 */
float TimeSinceLastMovement()
{
	return 0;
}

/**
    The CurrentlyMoving() function returns true if speed > ~10 mph, and false otherwise.
 */
bool CurrentlyMoving()
{
	float speed = GetSpeed(locationBuffer.prevLocations[0], locationBuffer.prevLocations[1]);
	return (speed > 4.8);
}
void printgpspoint(gpsPoint point)
{
	char temp[15];
	dtostrf(point.lat, -8,6, temp);
	SendUARTString("(");
	SendUARTString(temp);
	SendUARTString(",");
	dtostrf(point.lng, -8,6, temp);
	SendUARTString(temp);
	SendUARTString(",");
	dtostrf(point.locatedTime, -8,1, temp);
	SendUARTString(temp);
	SendUARTString(")");
}
/**
    The WaitForIntersectionSync() function waits for the user to stop at an intersection, then sets the sync time.
 */
void WaitForIntersectionSync()
{
	bool intersectionSynced = false;
	#if DEBUG
	char temp[15];
	#endif
	while(!intersectionSynced)
	{
		gpsPoint currentLocation = locationBuffer.prevLocations[0];
		
		intersection nearestIntersection = LoadNearestIntersection(currentLocation.lat, currentLocation.lng);
		gpsPoint intersectionLocation = { .lat = nearestIntersection.lat, .lng = nearestIntersection.lng };

		#if DEBUG
			SendUARTString("Current loc: ");
			dtostrf(currentLocation.lat, -8,6, temp);
			SendUARTString(temp);
			SendUARTString(",");
			dtostrf(currentLocation.lng, -8,6, temp);
			SendUARTString(temp);

			//float speed=DistanceBetweenPoints(locationBuffer.prevLocations[0], locationBuffer.prevLocations[1]);
			float speed = GetSpeed(locationBuffer.prevLocations[0], locationBuffer.prevLocations[1]);

			SendUARTString(", SPEED:");
			dtostrf(speed, -8,6, temp);
			SendUARTString(temp);

			SendUARTString(",");
			printgpspoint(locationBuffer.prevLocations[0]);
			printgpspoint(locationBuffer.prevLocations[1]);
			SendUARTString("\r\n");

			PrintIntersectionUART(nearestIntersection);

			if(CurrentlyMoving())
				SendUARTString("MOVING\n\r");
			else
				SendUARTString("NOTMOVING\n\r");
		#endif


		if(75 > DistanceBetweenPoints(currentLocation, intersectionLocation) &&
			!CurrentlyMoving())
		{
			DeleteIntersection(currentIntersection);
			currentIntersection = nearestIntersection;
			LoadLinkedIntersections((intersection *)&currentIntersection);
			syncTime = currentTime;
			lastIntersectionChangeTime = currentTime;
			intersectionSynced = true;
		}
		else
			DeleteIntersection(nearestIntersection);
		_delay_ms(4000);
	}
}

void PrintIntersectionUART(intersection intSec)
{
	char temp[15];
	itoa(intSec.id, temp, 10);
	SendUARTString("ID: ");
	SendUARTString(temp);
	SendUARTString(", Name: ");
	SendUARTString(intSec.name);
	SendUARTString("Lat:");
	dtostrf(intSec.lat, -8,6, temp);
	SendUARTString(temp);
	SendUARTString(", Lng: ");
	dtostrf(intSec.lng, -8,6, temp);
	SendUARTString(temp);
	SendUARTString(", NearbyCount: ");
	itoa(intSec.nearbyIntersectionCount, temp, 10);
	SendUARTString(temp);
	SendUARTString(", time:");
	itoa(intSec.time, temp, 10);
	SendUARTString(temp);
	SendUARTString("\r\n");
}
/**
    The SendStringToDispla() function sends the string through UART
 */
void SendStringToDisplay(char* str)
{
	SendUARTString(str);
}

int main(void)
{
	InitUART();
	InitGPS();
	InitIntersectionManager();
	sei();
	SendStringToDisplay("Waiting for GPS fix\r\n");
	WaitForGPSFix();
	SendStringToDisplay("Waiting for SD card init...\n\r");
	WaitForSDMount();
	SendStringToDisplay("Waiting for Intersection Sync..\n\r");
	WaitForIntersectionSync();
	SendStringToDisplay("Started.");
	
	char speedStr[15];
	while(1)
	{
		WaitForGPSFix();
		if(!CurrentlyMoving() && (currentTime - lastIntersectionChangeTime) > 300)
		{
			float idleTime = TimeSinceLastMovement();
			if(idleTime > MAXIDLETIME)
			{
				Sleep();
				continue;
			}
		}
		else if((currentTime - lastIntersectionChangeTime) > 300)
		{
			WaitForIntersectionSync();
		}
		
		intersection nextInt = GetNextIntersection(locationBuffer.prevLocations[0], locationBuffer.velocity[0]);
		gpsPoint nextIntersectionLocation = { .lat = nextInt.lat, .lng = nextInt.lng };
		gpsPoint currentIntersectionLocation = { .lat = currentIntersection.lat, .lng = currentIntersection.lng };
			
		float distanceBetweenIntersections = DistanceBetweenPoints(nextIntersectionLocation, currentIntersectionLocation);
		
		#if DEBUG
			dtostrf(distanceBetweenIntersections, -8,2, speedStr);
			SendUARTString("DistBetween Int: ");
			SendUARTString(speedStr);
			SendUARTString(",");
			dtostrf(currentTime, -8,2, speedStr);
			SendUARTString("Curtime: ");
			SendUARTString(speedStr);
			SendUARTString(",");
			dtostrf(syncTime, -8,2, speedStr);
			SendUARTString("Synctime: ");
			SendUARTString(speedStr);
			SendUARTString("\n\r");
			float speed = GetSpeed(locationBuffer.prevLocations[0], locationBuffer.prevLocations[1]);

			SendUARTString(", SPEED:");
			dtostrf(speed, -8,2, speedStr);
			SendUARTString(speedStr);

			SendUARTString(",");
			printgpspoint(locationBuffer.prevLocations[0]);
			printgpspoint(locationBuffer.prevLocations[1]);
			SendUARTString("\r\n");
		#endif

		float distanceFromNextIntersection = DistanceBetweenPoints(locationBuffer.prevLocations[0], nextIntersectionLocation);
		float distanceFromCurrentIntersection = DistanceBetweenPoints(locationBuffer.prevLocations[0], currentIntersectionLocation);
		short necessarySpeed = (short)ConvertSpeedToMPH((distanceFromNextIntersection/(syncTime + nextInt.time - currentTime)));
		itoa(necessarySpeed, speedStr, 10);
		SendStringToDisplay("Maintain at least ");
		SendStringToDisplay(speedStr);
		SendStringToDisplay("mph.\n\r");

		//float distanceFromNextIntersection = DistanceBetweenPoints(locationBuffer.prevLocations[0], nextIntersectionLocation);
		
		#if DEBUG
			PrintIntersectionUART(nextInt);
		#endif

		if(distanceFromNextIntersection < distanceFromCurrentIntersection)
		{
			#if DEBUG
			SendUARTString("Reached intersection\n\r");
			#endif
			DeleteIntersection(currentIntersection);
			currentIntersection = LoadRecordByID(nextInt.id);
			LoadLinkedIntersections((intersection *)&currentIntersection);
			lastIntersectionChangeTime = currentTime;
		}
		else if (distanceFromCurrentIntersection < 50) //50 metres
		{
			syncTime = currentTime;
			_delay_ms(500);
		}


		#if DEBUG
		SendString("------------\r\n");
		_delay_ms(4000);
		#endif
	}
}

/**
    10ms period FatFs timer function.
 */
ISR(TIMER1_COMPA_vect)
{
	#if DEBUG == 0
	currentTime += 0.011; // add 11ms to current time.
	#endif
	disk_timerproc();
}





