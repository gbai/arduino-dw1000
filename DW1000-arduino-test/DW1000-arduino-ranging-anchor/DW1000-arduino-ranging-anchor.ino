/*
 * Copyright (c) 2015 by Thomas Trojer <thomas@trojer.net>
 * Decawave DW1000 library for arduino.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Use this to test two-way ranging functionality with two
 * DW1000. This is the anchor component's code which computes range after
 * exchanging some messages. Addressing and frame filtering is currently done 
 * in a custom way, as no MAC features are implemented yet.
 *
 * Complements the "DW1000-arduino-ranging-tag" sketch. 
 */

#include <SPI.h>
#include <DW1000.h>

// messages used in the ranging protocol
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define RANGE_FAILED 255
volatile byte expectedMsgId = POLL;
volatile boolean sentAck = false;
volatile boolean receivedAck = false;
boolean protocolFailed = false;
// timestamps to remember
DW1000Time timePollSent;
DW1000Time timePollReceived;
DW1000Time timePollAckSent;
DW1000Time timePollAckReceived;
DW1000Time timeRangeSent;
DW1000Time timeRangeReceived;
// data buffer
#define LEN_DATA 16
byte data[LEN_DATA];
// reset line to the chip
int RST = 9;

void setup() {
  // DEBUG monitoring
  Serial.begin(9600);
  Serial.println("### DW1000-arduino-ranging-anchor ###");
  // initialize the driver
  DW1000.begin(0, RST);
  DW1000.select(SS);
  Serial.println("DW1000 initialized ...");
  // general configuration
  DW1000.newConfiguration();
  DW1000.setDefaults();
  DW1000.setDeviceAddress(1);
  DW1000.setNetworkId(10);
  DW1000.setFrameFilter(false);
  DW1000.commitConfiguration();
  Serial.println("Committed configuration ...");
  // DEBUG chip info and registers pretty printed
  Serial.print("Device ID: "); Serial.println(DW1000.getPrintableDeviceIdentifier());
  Serial.print("Unique ID: "); Serial.println(DW1000.getPrintableExtendedUniqueIdentifier());
  Serial.print("Network ID & Device Address: "); Serial.println(DW1000.getPrintableNetworkIdAndShortAddress());
  // attach callback for (successfully) sent and received messages
  DW1000.attachSentHandler(handleSent);
  DW1000.attachReceivedHandler(handleReceived);
  // anchor starts in receiving mode, awaiting a ranging poll message
  receiver();
}

void handleSent() {
  // status change on sent success
  sentAck = true;
}

void handleReceived() {
  // status change on received success
  receivedAck = true;
}

void transmitPollAck() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  data[0] = POLL_ACK;
  DW1000.setData(data, LEN_DATA);
  DW1000.startTransmit();
  receiver();
}

void transmitRangeReport(float curRange) {
  DW1000.newTransmit();
  DW1000.setDefaults();
  data[0] = RANGE_REPORT;
  // write final ranging result
  memcpy(data+1, &curRange, 4);
  DW1000.setData(data, LEN_DATA);
  DW1000.startTransmit();
  receiver();
}

void transmitRangeFailed() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  data[0] = RANGE_FAILED;
  DW1000.setData(data, LEN_DATA);
  DW1000.startTransmit();
  receiver();
}

void receiver() {
  DW1000.newReceive();
  DW1000.setDefaults();
  // so we don't need to restart the receiver manually
  DW1000.receivePermanently(true);
  DW1000.startReceive();
}

float getRange() {
  // correct timestamps (in case system time counter wrap-arounds occured)
  // TODO
  // two roundtrip times - each minus message preparation times / 4
  Serial.println("START DATA");
  //Serial.print("POLL RECEIVED ");
  Serial.println(timePollReceived.getAsFloat());
  //Serial.print("POLL ACK SENT ");
  Serial.println(timePollAckSent.getAsFloat());
  //Serial.print("RANGE RECEIVED ");
  Serial.println(timeRangeReceived.getAsFloat());
  //Serial.print("POLL SENT ");
  Serial.println(timePollSent.getAsFloat());
  //Serial.print("POLL ACK RECEIVED ");
  Serial.println(timePollAckReceived.getAsFloat());
  //Serial.print("RANGE SENT ");
  Serial.println(timeRangeSent.getAsFloat());
  
  DW1000Time timeOfFlight = ((timePollAckReceived-timePollSent)-(timePollAckSent-timePollReceived) +
      (timeRangeReceived-timePollAckSent)-(timeRangeSent-timePollAckReceived));// / 4;
  
  return timeOfFlight.getAsFloat();
}

void loop() {
  if(!sentAck && !receivedAck) {
    return;
  }
  // continue on any success confirmation
  if(sentAck) {
    sentAck = false;
    byte msgId = data[0];
    if(msgId == POLL_ACK) {
      DW1000.getTransmitTimestamp(timePollAckSent);
      Serial.print("Sent POLL ACK @ "); Serial.println(timePollAckSent.getAsFloat());
    }
  } else if(receivedAck) {
    receivedAck = false;
    // get message and parse
    DW1000.getData(data, LEN_DATA);
    byte msgId = data[0];
    if(msgId != expectedMsgId) {
      // unexpected message, start over again
      Serial.print("Received wrong message # "); Serial.println(msgId);
      protocolFailed = true;
    }
    if(msgId == POLL) {
      protocolFailed = false;
      DW1000.getReceiveTimestamp(timePollReceived);
      expectedMsgId = RANGE;
      Serial.print("Received POLL @ "); Serial.println(timePollReceived.getAsFloat());
      transmitPollAck();
    } else if(msgId == RANGE) {
      DW1000.getReceiveTimestamp(timeRangeReceived);
      expectedMsgId = POLL;
      if(!protocolFailed) {
        timePollSent.setFromBytes(data+1);
        timePollAckReceived.setFromBytes(data+6);
        timeRangeSent.setFromBytes(data+11);
        Serial.print("Received RANGE @ "); Serial.println(timeRangeReceived.getAsFloat());
        Serial.print("POLL sent @ "); Serial.println(timePollSent.getAsFloat());
        Serial.print("POLL ACK received @ "); Serial.println(timePollAckReceived.getAsFloat());
        Serial.print("RANGE sent @ "); Serial.println(timeRangeSent.getAsFloat());
        float curRange = getRange();
        Serial.print("Range time is "); Serial.println(curRange);
        transmitRangeReport(curRange);
      } else {
        transmitRangeFailed();
      }
    }
  }
}

