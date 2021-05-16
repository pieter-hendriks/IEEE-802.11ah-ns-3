#pragma once
#ifndef INC_TWT_HEADERS_H_
#define INC_TWT_HEADERS_H_

#include "ns3/header.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/tag.h"
#include <limits>
#include <map>
#include <memory>
#include <ns3/mac48-address.h>
#include <ostream>
#include <random>
#include <utility>

namespace ns3
{

	// Helper enums for readability of magic numbers
	enum : uint8_t
	{
		TWT_ELEMENT_ID = 216
	};
	enum : uint8_t
	{
		TWT_CATEGORY_UNPROTECTED_S1G = 22
	};
	enum : uint8_t
	{
		TWT_ACTION_FRAMETYPE_SETUP = 6,
		TWT_ACTION_FRAMETYPE_TEARDOWN = 7,
		TWT_ACTION_FRAMETYPE_INFORMATION = 11
	};
	enum : uint8_t
	{
		TWT_SETUPCOMMAND_REQUEST = 0,
		TWT_SETUPCOMMAND_SUGGEST = 1,
		TWT_SETUPCOMMAND_DEMAND = 2,
		TWT_SETUPCOMMAND_GROUPING = 3,
		TWT_SETUPCOMMAND_ACCEPT = 4,
		TWT_SETUPCOMMAND_ALTERNATE = 5,
		TWT_SETUPCOMMAND_DICTATE = 6,
		TWT_SETUPCOMMAND_REJECT = 7
	};
	// Header class for handling Target wake time.
	class TWTHeader : public Header
	{
	public:
		TWTHeader();
		TWTHeader(TWTHeader &&) = default;
		TWTHeader(const TWTHeader &) = default;
		TWTHeader &operator=(const TWTHeader &) = default;
		TWTHeader &operator=(TWTHeader &&) = default;
		virtual ~TWTHeader() = default;
		static TypeId GetTypeId(void);
		virtual TypeId GetInstanceTypeId(void) const;
		virtual uint32_t GetSerializedSize(void) const;
		virtual void Serialize(Buffer::Iterator start) const;
		virtual uint32_t Deserialize(Buffer::Iterator start);
		virtual void Print(std::ostream &os) const;

		uint8_t GetCategory() const;
		void SetCategory(uint8_t c);
		uint8_t GetAction() const;
		void SetAction(uint8_t a);
		uint8_t GetDialogToken() const;
		void SetDialogToken(uint8_t t);

		bool IsTwtSetupFrame() const;
		bool IsTwtTeardownFrame() const;
		bool IsTwtInformationFrame() const;
		void SetTwtSetupFrame();
		void SetTwtTeardownFrame();
		void SetTwtInformationFrame();
		// Set the NDP paging field presence bit. Indicates if the field at the end of packet is present or not.
		void SetNdpPaging(bool b);
		bool IsNdpPagingSet() const;
		// Set the Responder PM Mode = power management mode for the responder, see section 11.2 of standard
		// TODO
		void SetResponderPMMode(bool b);
		bool IsResponderPMModeSet() const;
		// Set the request type - should be 1 for twt requesting station, 0 for responding station
		void SetRequest(bool b);
		bool IsRequest() const;
		// values as in section 9.4.2.1, table 9-297
		void SetSetupCommand(uint8_t i);
		uint8_t GetSetupCommand() const;
		// Set implicit/explicit operation, as defined in standard
		void SetImplicit(bool b);
		bool IsImplicit() const;
		// Set the flow type, announced or unannounced. Boolean flag.
		void SetAnnouncedFlowType(bool b);
		bool IsAnnouncedFlowType() const;
		// Set identifier, unique between any two stations.
		void SetFlowIdentifier(uint8_t i);
		uint8_t GetFlowIdentifier() const;

		void SetWakeIntervalExponent(uint8_t i);
		uint8_t GetWakeIntervalExponent() const;
		void SetWakeIntervalMantissa(uint16_t i);
		uint16_t GetWakeIntervalMantissa() const;

		// The protection field indicates to AP that a RAW window corresponding to the TWT should be set
		// TODO
		void SetProtection(bool b);
		bool IsProtected() const;

		// Functions for setting/getting the TWT field in this element
		void SetTargetWakeTime(ns3::Time time);
		ns3::Time GetTargetWakeTime() const;

		// Group assignment functions
		void SetGroupId(uint8_t id);
		uint8_t GetGroupId() const;
		void SetZeroOffsetOfGroupPresent(bool b);
		bool IsZeroOffsetOfGroupPresent() const;
		void SetZeroOffsetOfGroup(uint64_t value);
		uint64_t GetZeroOffsetOfGroup() const;
		uint8_t GetUnit() const;
		void SetUnit(uint8_t value);
		uint16_t GetOffset() const;
		void SetOffset(uint16_t value);
		Time GetNominalMinimumWakeDuration() const;
		void SetNominalMinimumWakeDuration(Time);

		void SetLength(void);
		uint8_t GetElementId() const;
		uint8_t GetLength() const;

		static TWTHeader CreateConfirmationHeader(const TWTHeader &receivedHeader);
		bool IsTwtConfirmationFrame() const;

		void IncrementTargetWakeTime();

		bool IsInfoResponseRequested() const;
		bool IsInfoNextTwtRequested() const;
		uint8_t GetNextTwtInfoSubfieldSize() const;
		ns3::Time GetNextTwtInfo() const;
		void SetInfoResponseRequested(bool);
		void SetInfoNextTwtRequested(bool);
		void SetNextTwtInfoSubfieldSize(uint8_t);
		void SetNextTwt(ns3::Time);

	private:
		uint8_t m_category;										// 8 bits
		uint8_t m_action;											// 8 bits
		uint8_t m_dialogToken;								// 3 bits
		uint8_t m_elementId;									// 8 bits
		uint8_t m_length;											// 8 bits
		uint8_t m_flowIdentifier;							// 3 bits
		bool m_zeroOffsetOfGroupPresent;			// 1 bit
		uint64_t m_zeroOffsetOfGroup;					// 48 bits
		uint8_t m_groupId;										// 7 bits
		uint8_t m_groupOffsetTimeUnit;				// 4 bits
		uint8_t m_groupOffset;								// 12 bits
		uint64_t m_targetWakeTime;						// 64 bits
		bool m_responderPowerManagementMode;	// 1 bit
		bool m_ndpPagingIndicator;						// 1 bit
		bool m_isRequest;											// 1 bit
		uint8_t m_setupCommand;								// 3 bits
		bool m_isImplicit;										// 1 bit
		bool m_isAnnounced;										// 1 bit
		int8_t m_wakeIntervalExponent;				// 5 bits
		bool m_isProtected;										// 1 bit
		uint16_t m_ndpId;											// 9 bits
		uint8_t m_maxNdpPagingPeriod;					// 8 bits
		uint8_t m_ndpPagingPartialTsfOffset;	// 4 bits
		uint8_t m_ndpPagingAction;						// 3 bits
		uint8_t m_ndpPagingMinSleepDuration;	// 6 bits
		uint8_t m_nominalMinimumWakeDuration; // 8 bits
		uint16_t m_wakeIntervalMantissa;			// 16 bits
		uint8_t m_channel;										// 8 bits
		bool m_twtInformationResponseRequested;
		bool m_twtInformationNextTwtRequest;
		uint8_t m_twtInformationNextTwtSubfieldSize;
		uint64_t m_twtInformationNextTwt;

		// Private functions
		void SerializeControlField(Buffer::Iterator &start) const;
		void SerializeRequestType(Buffer::Iterator &start) const;
		void SerializeGroupAssignment(Buffer::Iterator &start) const;
		void SerializeNdpPaging(Buffer::Iterator &start) const;
		void SerializeInformationField(Buffer::Iterator &start) const;
		void DeserializeControlField(Buffer::Iterator &start);
		void DeserializeRequestType(Buffer::Iterator &start);
		void DeserializeGroupAssignment(Buffer::Iterator &start);
		void DeserializeNdpPaging(Buffer::Iterator &start);
		void DeserializeInformationField(Buffer::Iterator &start);
		void SerializeTWTTeardown(Buffer::Iterator &start) const;
		void SerializeTWTSetup(Buffer::Iterator &start) const;
		void SerializeTWTInformation(Buffer::Iterator &start) const;
		void DeserializeTwtSetupFrame(Buffer::Iterator &start);
		void DeserializeTwtTeardownFrame(Buffer::Iterator &start);
		uint32_t GetSerializedSizeForSetupFrameSpecificFields() const;
		uint32_t GetSerializedSizeForTeardownFrameSpecificFields() const;
		uint32_t GetSerializedSizeForInformationFrameSpecificFields() const;
		uint32_t GetSerializedSizeForConfirmationFrameSpecificFields() const;
	};
	std::ostream &operator<<(std::ostream &os, const TWTHeader &header);
	// Struct for marking packets. This allows us to filter the duplicate ones.
	// Unsure if there's a better way to handle this. There must be, I just don't know what it is.
	struct TwtPacketTag : public ns3::Tag
	{
		uint64_t id;
		static TypeId GetTypeId();
		TypeId GetInstanceTypeId() const;

		virtual ~TwtPacketTag() = default;
		void Serialize(ns3::TagBuffer i) const override;
		void Deserialize(ns3::TagBuffer i) override;
		void Print(std::ostream &os) const override;
		uint32_t GetSerializedSize(void) const override;
		static TwtPacketTag Create();
		static TwtPacketTag FromPacket(Ptr<const Packet> packet);
		static TwtPacketTag ERROR();

	private:
		TwtPacketTag() = default;
		TwtPacketTag(uint64_t id);
	};
	bool operator==(const TwtPacketTag &a, const TwtPacketTag &b);
	bool operator!=(const TwtPacketTag &a, const TwtPacketTag &b);
	bool operator<(const TwtPacketTag &a, const TwtPacketTag &b);
} // namespace ns3
#endif