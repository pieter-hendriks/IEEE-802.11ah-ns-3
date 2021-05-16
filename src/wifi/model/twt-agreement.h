#pragma once
#ifndef INC_SRC_WIFI_MODEL_TWT_AGREEMENT_H_
#define INC_SRC_WIFI_MODEL_TWT_AGREEMENT_H_
#include "ns3/event-id.h"
#include "ns3/mac48-address.h"
#include "ns3/nstime.h"
#include "ns3/twt-headers.h"

#include <cinttypes>
namespace ns3
{
	struct TWTAgreementKey
	{
		// MAC address of the station the agreement is with
		Mac48Address macAddress;
		// Flow identifier for the agreement
		uint8_t flowIdentifier;
		// This key represents exactly what is allowed by the standard,
		// Any two stations may have up to 8 (==3bits) agreements, distinguished by a unique flow identifier
		// We store flowid as a uint8_t, but correct use requires that its value never exceeds 7.
	};
	bool operator==(const TWTAgreementKey &a, const TWTAgreementKey &b);
	bool operator<(const TWTAgreementKey &a, const TWTAgreementKey &b);
	enum TWTRole
	{
		TWT_REQUESTING_STA = 0,
		TWT_RESPONDING_STA = 1
	};
	struct TWTAgreementData
	{
		// The TWT header for this agreement. Contains lots of data we need, simply accessing header is easier than
		// duplicating all the contained information
		TWTHeader header;
		// Simulator event for wake up - allows cancelling in case of e.g. teardown
		EventId nextEvent;
		// Adjust for late start, e.g. in announced twt
		Time adjustedMinWake;
		// Can happen in case of implicit session receiving a next-twt info
		bool periodicTwtOverridden;
		// Set whether we're the sending or responding station.
		uint8_t myRole;
		// Mark the agreement as waiting for next twt.
		// Used after sending out a poll frame in case no timing update is received during explicit TWT service period
		bool waitingForNextTwt;

		bool IsActive() const;
	};
	typedef std::map<TWTAgreementKey, TWTAgreementData> TWTAgreementMap;
} // namespace ns3
#endif