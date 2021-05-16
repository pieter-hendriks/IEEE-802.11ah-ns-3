# TWT Implementation #

## Implementation caveats ##

### TWT Acknowledgment Procedure ###

Summary: All frames are always acknowledged. Every acknowledgment frame sent is a TACK acknowledgment frame.
While the standard specifies behaviour for frame aggregation and requesting stations that don't have the S1G capability TWT responder, we do not support those cases at this time.

#### TWT Responding STA Acknowledgment Procedure ####

* Standard specifies we should transmit ACKs only to those stations that have the RXVECTOR parameter RESPONSE_INDICATION set to NORMAL_RESPONSE. The implementation does not support this - we always send the response.
* The standard specifies we should transmit BAT (block acknowledgement TWT) frames in response to Aggregated MPDUs. However, this is not supported by the implementation. Specifically, the implementation assumes every single received frame is an S-MPDU. This means that, per section 10.47.2, we always acknowledge using a TACK frame. The BlockAckReq frame mentioned is not supported, either.

#### TWT Requesting STA Acknowledgment Procedure ####

* Similar caveats to the TWT Responding STA case. We do not support frame aggregation/block acknowledgments.
* The S1G capabilities data is ignored - we simply assume that every station has S1G capability TWT Responder set to 1.

#### TACK frame field values ####

The Duration field in a TACK frame is supposed to be set to:
>the value obtained from the Duration/ID field of the frame that elicited the response minus the time, in microseconds, between the end of the PPDU carrying the frame that elicited the response and the end of the PPDU carrying the TACK frame.
This PPDU timing information is not currently implemented, so the duration field can not be meaningfully set. Its value is left unchanged from WifiMacHeader construction, may contain garbage data.

### Send duration ###

We assume that, no matter how much data is queued, it's possible to do the transmission within a TWT SP. When we have an agreement between two stations, whose service period begins, all traffic for this link gets queued. There's most likely a more elegant solution for this, that correctly accounts for transmission time etc.

Packets are being dropped over the link sometimes - not sure why. Generally happens when they're in transit, not time-out related. Have removed bunch of noise and stuff from channel, doesn't seem to have corrected things.

## Current notes ##

Implicit TWT:
	Periodic. Increment twt at end of each wake interval, unless received non-zero next-twt info.
		If we've received a non-zero next-twt info, the specified twt will be the next twt.
	TWT responding STA shall include starting TWT value for first SP in accept frame.
		Subsequent times determined by increment
	TWT requesting STA may transition to oze after AdjustedMinimumWakeDuration (=nominal wake - start offset)
		or after receiving EOSP field == 1 from responding station (end of service period, presumably in information field? Unsure.)
	TWT responding STA may respond to frame from requesting STA with frame that contains Next TWT Info/Suspend Duration field.
			BAT/TACK/STACK.
			--> Implement receive only.
			--> Our Responding STA won't send these frames, we simply implement a trivial case.
				The receiving STA should know how to handle them, though. So we may have to extend both STA and AP receive functions.
				And I really hope there aren't any others, because that would cause issues.

## Next TWT ##

See standard figure 9-28. A S1G ppdu with subtype 3 can have a Next TWT info field. This field is currently unsupported, but should be.

Probably many more, search code for TODOs.

## TWT Setup Frame ##

Sent by requesting STA to request setup,
Sent by responding STA to indicate status of request TWT SP (Service Period)

The TWT setup frame is a frame sent over S1G, with a WifiMacHeader
Under that header, we have the following structure:

 1) Category
 2) unprotected S1G action : 6 = twt setup, 7 = twt teardown, 11 = twt information
 3) dialog token
 4) TWT
Category S1G, 1 octet  = 22
Action, 1 octet = 6
dialog token = 1 octet, = variable, used to match request to response. Should store token used, responding
station will use the same token to respond to us.

TWT Individual STA <-> AP setup sequence:
 Sta sends TWT request
  TWT Setup Frame (9.6.25.8)
   1 category (9.4.1.11)
   2 Unprotected S1G action (9.6.25.1)
   3 Dialog Token
    (request field == 1 ? ==> dialog token )
   4 TWT (9.4.2.200)

 AP responds

TWT Unit field is in twt group assignment field
9.262l (in 9.4.2.200):
 TWT Unit subfield value TWT Unit time value
       0 32 us
       1 256 us
       2 1024 us
       3 8.192 ms
       4 32.768 ms
       5 262.144 ms
       6 1.048576 s
       7 8.388608 s
       8 33.554432 s
       9 268.435456 s
       10 1073.741824 s
       11 8589.934592 s
       12–15 Reserved

## Wifi Mac header ##

The header structure is defined in section 9.2.3, general frame format.
Any further specification is in the frame body field.
Frame control (2 bytes)
duration (2 bytes)
address1 (6 bytes)
address2 (6 bytes)
address3 (6 bytes)
sequence control (2 bytes)
address4 (6 bytes)
QoS control (2 bytes)
HT control (4 bytes)
In sequence control, the +HTC flag sets whether HT Control field is included.
TODO: Unsure where extra address inclusion is specified.
In any case, we will, for now, simply assume we include 4 addresses (transmitter, receiver, source, destination), 6 bytes each.
Frame body (variable)
FCS (4 bytes)
TODO: Figure out how to include trailer. Simply ignoring for now.

### Type and Subtype field combinations ###

For the TWT implementation currently, type 0 and subtype 0b1101 (13) is supported.
Per section 9.2.4.1.3, the 0 type indicates a Management frame. Subtype 0b1101 is for an Action frame (Table 9-1).
TWT Setup/Teardown/Information frames are all action frames. Specifically, these are unprotected S1G action frames. They differ from normal action frames in that

Section 9.6 defines action frame format. The unprotected S1G action frames are discussed in 9.6.24. The action category is 22, per table 9-51 (section 9.4.1.11), for unprotected S1G.

#### Frame control field format ####

This field's format is defined by the combination of type and subtype. These fields are statically present in every possible format for this element and so can always be located. In this case, we are transmitting a S1G PPDU, with subtype 13 (that is, not equal to 3 and not equal to 10). The format is thus shown in figure 9-27, section 9.3.1.1.
2 bits protocol version, 2 bits type, 4 bits subtype (Uniform across all options).
3 bits: bandwidth indication
1 bit: dynamic indication
1 bit: power management
1 bit: more data
1 bit: protected frame
1 bit: +HTC (marked for HT Control field).

## Unprotected S1G action field ##

Action frame format: Category (1oct), action (1oct), dialog token (1oct), body.
Category is set at 22. Action is variable, either 6 (TWT setup), 7 (TWT teardown) or 11 (TWT Information).
Dialog token is used to uniquely identify a TWT agreement between two stations. Value is implementation defined, simply counting up from 0 seems very sensible.
The body, then, is a TWT Setup Frame/TWT Teardown frame/TWT Information frame.

## TWT Setup frame ##

category - action - dialog token - twt element (9.4.2.199): See section 9.6.24.8
If TWT Request is set to 1, we initialize the dialog token to some chosen value. When TWT request == 0, we should copy the token value from whichever request we're responding to.

## TWT Teardown frame ##

category - action - flow: See section 9.6.24.9
Flow contains an identifier for twt agreement between the two stations. This is the flow being torn down. 3 bits + 5 reserved

## TWT Information frame ##

category - action -twt information (9.4.1.60): See section 9.6.24.12

## Main implementation ##

Initial setup was a simple case - just create two nodes, configure nothing unnecessary.
However, it seems that some of the RAW configuration can't just be ignored. The simulator was erroring when not setting any RAW related parameters. As such, from the s1g-test-tim-raw case, I've added the configuration code in order to resolve that problem. The simulator now correctly runs without crashing when the AP attempts to send a beacon.

## AP Implementation ##

After initial simple packet send was implemented on the STA side, I went ahead and started implementing on AP side. Simply identifying the packet based on the MAC header should be possible.

## Frame addressing ##

We don't send an A-MSDU (aggregate of multiple MSDU subframes), so we're sending a 4-address packet.
In our case, addr 1 is immediate destination, addr 2 is immediate transmitter
addr 3 is final destination, addr 4 is initial source
So in our case (with only one hop), in all cases, addr 1 == addr 3 and addr 2 == addr 4

### Four-address MAC header ###

The addressing of the frame with a four-address MAC header shall be as follows:
— Address 1 is the MAC address of the immediate destination STA (the receiver of the MPDU) or a
SYNRA.
— Address 2 is the MAC address of the transmitter STA (the transmitter of the MPDU).
— Address 3 is the DA of the MSDU (the destination address of the MSDU), or BSSID for a Basic
A-MSDU.
— Address 4 is the SA of the MSDU (the source address of the MSDU), or BSSID for a Basic
A-MSDU.

### Three-address MAC header ###

The addressing of the frame with the three-address MAC header format containing a basic A-MSDU shall
be as follows:
— Address 1 is the MAC address of the immediate destination STA (the receiver of the MPDU).
— Address 2 is the MAC address of the transmitter STA (the transmitter of the MPDU).
— Address 3 is the BSSID.
— DA in A-MSDU subframe header is the DA of the MSDU (the destination address of the MSDU).
— SA in A-MSDU subframe header is the SA of the MSDU (the source address of the MSDU).

## STA-Count ##

Initially taken as copy from raw test directory. I believe this was sixty.
Hard set to 1 STA now to limit any extra packets and stuff popping up.
Will probably have to add some error handling later when I correct this.

## Questions ##

sta-wifi-mac.cc:
> if (hdr->GetAddr3() == GetAddress())
> {
> NS_LOG_LOGIC("packet sent by us.");
> return;
> }
This check doesn't seem correct. The address 3 (as discussed in section Frame Addressing) denotes either a) the destination address, b) the BSSID for a Basic A-MSDU or c) the BSSID in the three-address format.
This means that in no case where it is the STA's address does it mean that it was the station's own send. This check has been commented out - I saw no clear indication that anything else broke, so I'm keeping it like that for now. My code relies on this addressing to function correctly, since I fill the address three with the destination, which in this case is the STA's address for a packet originating at the AP.

## No disassociate while TWT active ##

== actually implementing the TWT functionality rather than just the message exchange
Seems like disassociation happens with beacon watchdog.
Beacon watch dog has a delay variable, which I think is the amount of time it can miss beacons for before disassociating.
So we'll set that when the TWT session is configured locally, and re-set it to the normal value when we tear down a TWT session.
The beacon watch dog re-set function only handles adding time:
  set = std::max(now() + delay, oldValue)
 So if we want to lower it (i.e. cancelled TWT session), we'll need new functionality.
 This has been added as an extra boolean value to the restart function.
 It defaults to false, preserving old behaviour. If it is true, it'll simply force set the new value.
m_beaconWatch dog is an event that is created, scheduling the MissedBeacons function at a certain point in time. m_beaconWatchdogEnd sets a timer for it, unsure how they interact exactly - I'm just hoping my reasoning is solid and the small changes I've made work.

For now, only a single beacon interval can be set. Will need to wrap the time-get calculation into a function so that there's an easier way to get at this information.
