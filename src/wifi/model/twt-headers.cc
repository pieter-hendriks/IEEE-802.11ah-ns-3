#include "twt-headers.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include <limits>

NS_LOG_COMPONENT_DEFINE("TWT_HEADERS");
namespace ns3
{
	void WriteHtonU48(Buffer::Iterator &start, uint64_t value)
	{
		NS_ASSERT_MSG((value & 0xFFFFFFFFFFFF) == value, "Can't write more than 48 bits using this function.");
		uint32_t lsb{0}; // 32 least significant bits
		uint16_t msb{0}; // 16 most significant bits
		// Numeric values should be written in Big Endian order.
		lsb = static_cast<uint32_t>(value & std::numeric_limits<uint32_t>::max());
		msb = static_cast<uint16_t>((value >> 32) & std::numeric_limits<uint16_t>::max());
		start.WriteHtonU32(lsb);
		start.WriteHtonU16(msb);
	}
	uint64_t ReadNtohU48(Buffer::Iterator &start)
	{
		uint32_t lsb{0};
		uint16_t msb{0};
		lsb = start.ReadNtohU32();
		msb = start.ReadNtohU16();
		return ((static_cast<uint64_t>(msb) << 32) | lsb);
	}
	// uint8_t groupId : 7;
	// uint8_t zeroOffsetPresent : 1;
	// TWTZeroOffsetOfGroupField zeroOffsetOfGroup;
	// uint16_t unit : 4;
	// uint16_t offset : 12;
	void TWTHeader::SetGroupId(uint8_t id)
	{
		NS_ASSERT_MSG((id & 0x7F) == id, "Incorrect GroupId value. Field is 7 bits long, value must be between 0x0 and 0x7f (inclusive).");
		m_groupId = id;
	}
	uint8_t TWTHeader::GetGroupId() const
	{
		return m_groupId;
	}
	void TWTHeader::SetZeroOffsetOfGroupPresent(bool b)
	{
		m_zeroOffsetOfGroupPresent = b;
	}
	bool TWTHeader::IsZeroOffsetOfGroupPresent() const
	{
		return m_zeroOffsetOfGroupPresent;
	}
	void TWTHeader::SetZeroOffsetOfGroup(uint64_t offset)
	{
		NS_ASSERT_MSG((offset & 0xFFFFFFFFFFFF) == offset, "Field is 48 bits long - passed value is larger than max.");
		m_zeroOffsetOfGroup = offset;
	}
	uint64_t TWTHeader::GetZeroOffsetOfGroup() const
	{
		return m_zeroOffsetOfGroup;
	}
	uint8_t TWTHeader::GetUnit() const
	{
		return m_groupOffsetTimeUnit;
	}
	void TWTHeader::SetUnit(uint8_t value)
	{
		NS_ASSERT_MSG((value & 0x000F) == value, "Invalid value - unit field is 4 bits long, so value must be between 0 and 15.");
		m_groupOffsetTimeUnit = value;
	}
	uint16_t TWTHeader::GetOffset() const
	{
		return m_groupOffset;
	}
	void TWTHeader::SetOffset(uint16_t value)
	{
		NS_ASSERT_MSG((value & 0x0FFF) == value, "Invalid value - offset field is 12 bits long, so value must be between 0x0 and 0x0FFF");
		m_groupOffset = value;
	}
	void TWTHeader::SetTargetWakeTime(ns3::Time time)
	{
		m_targetWakeTime = time.GetMicroSeconds();
	}
	ns3::Time TWTHeader::GetTargetWakeTime() const
	{
		// We immediately do the conversion from the representation to the ns3::Time
		// No reason to bother the user with implementation detail
		return ns3::Time::FromInteger(m_targetWakeTime, ns3::Time::Unit::US);
	}
	void TWTHeader::SetLength()
	{
		m_length = this->GetSerializedSize() - 2;
	}

	void TWTHeader::SerializeControlField(Buffer::Iterator &start) const
	{
		uint8_t controlField{0};
		if (m_ndpPagingIndicator)
			controlField |= (1 << 7);
		if (m_responderPowerManagementMode)
			controlField |= (1 << 6);
		start.WriteU8(controlField);
	}
	void TWTHeader::DeserializeControlField(Buffer::Iterator &start)
	{
		uint8_t read = start.ReadU8();
		m_ndpPagingIndicator = (read & (1 << 7)) != 0;
		m_responderPowerManagementMode = (read & (1 << 6)) != 0;
	}
	void TWTHeader::SerializeRequestType(Buffer::Iterator &start) const
	{
		uint16_t requestTypeField{0};
		if (m_isRequest)
			requestTypeField |= (1 << 15);
		requestTypeField |= (m_setupCommand << 12);
		// 1 reserved bit here.
		if (m_isImplicit)
			requestTypeField |= (1 << 10);
		// m_isAnnounced is true if the field is zero
		if (!m_isAnnounced)
			requestTypeField |= (1 << 9);
		requestTypeField |= (m_flowIdentifier << 6);
		requestTypeField |= (m_wakeIntervalExponent << 1);
		if (m_isProtected)
			requestTypeField |= (1 << 0);
		start.WriteU16(requestTypeField);
	}
	void TWTHeader::DeserializeRequestType(Buffer::Iterator &start)
	{
		uint16_t read = start.ReadU16();
		m_isRequest = ((read >> 15) & 0b1) > 0;
		m_setupCommand = (read >> 12) & 0b111;
		m_isImplicit = ((read >> 10) & 0b1) > 0;
		// m_isAnnounced is true if the flow type field is zero
		m_isAnnounced = ((read >> 9) & 0b1) == 0;
		m_flowIdentifier = (read >> 6) & 0b111;
		m_wakeIntervalExponent = (read >> 1) & 0b11111;
		m_isProtected = ((read >> 0) & 0b1) > 0;
	}
	void TWTHeader::SerializeGroupAssignment(Buffer::Iterator &start) const
	{
		uint8_t byte{0};
		byte |= (m_groupId << 1);
		byte |= ((m_zeroOffsetOfGroupPresent ? 1 : 0) << 0);
		start.WriteU8(byte);
		if (m_zeroOffsetOfGroupPresent) {
			WriteHtonU48(start, m_zeroOffsetOfGroup);
		}
		uint16_t trailer{0};
		trailer |= (m_groupOffsetTimeUnit << 12); // time unit should be 4 bits
		trailer |= (m_groupOffset << 0);					// group offset should be 12 bits
		start.WriteU16(trailer);
	}
	void TWTHeader::DeserializeGroupAssignment(Buffer::Iterator &start)
	{
		uint8_t byte = start.ReadU8();
		m_groupId = (byte >> 1) & 0b1111111;
		m_zeroOffsetOfGroupPresent = ((byte >> 0) & 0b1) > 0;
		if (m_zeroOffsetOfGroupPresent) {
			m_zeroOffsetOfGroup = ReadNtohU48(start);
		}
		uint16_t trailer = start.ReadU16();
		m_groupOffsetTimeUnit = (trailer >> 12) & 0b1111; // 4 bits
		m_groupOffset = (trailer >> 0) & 0b111111111111;	// 12 bits
	}
	void TWTHeader::SerializeNdpPaging(Buffer::Iterator &start) const
	{
		// This field is fully optional, so only include if the flag is on
		if (m_ndpPagingIndicator) {
			uint32_t data{0};
			data |= (m_ndpId << 23);
			data |= (m_maxNdpPagingPeriod << 15);
			data |= (m_ndpPagingPartialTsfOffset << 11);
			data |= (m_ndpPagingAction << 8);
			data |= (m_ndpPagingMinSleepDuration << 2);
			// and two reserved bits at the end
			start.WriteU32(data);
		}
	}
	void TWTHeader::DeserializeNdpPaging(Buffer::Iterator &start)
	{
		if (m_ndpPagingIndicator) {
			uint32_t data = start.ReadU32();
			m_ndpId = (data >> 23) & 0b111111111;									// 9 bits
			m_maxNdpPagingPeriod = (data >> 15) & 0b11111111;			// 8 bits
			m_ndpPagingPartialTsfOffset = (data >> 1) & 0b1111;		// 4 bits
			m_ndpPagingAction = (data >> 8) & 0b111;							// 3 bits
			m_ndpPagingMinSleepDuration = (data >> 2) & 0b111111; // 6 bits
																														// 2 reserved at end, ignored.
		}
	}
	bool TWTHeader::IsTwtConfirmationFrame() const
	{
		return !m_isRequest && m_setupCommand == TWT_SETUPCOMMAND_ACCEPT;
	}
	void TWTHeader::SerializeTWTTeardown(Buffer::Iterator &start) const
	{
		start.WriteU8(m_category);
		start.WriteU8(m_action);
		uint8_t teardownFlowIdField;
		teardownFlowIdField |= (m_flowIdentifier << 5);
		start.WriteU8(teardownFlowIdField);
	}
	void TWTHeader::SerializeTWTSetup(Buffer::Iterator &start) const
	{
		start.WriteU8(m_category);
		start.WriteU8(m_action);
		start.WriteU8(m_dialogToken);
		// This function handles the trivial writes at top level.
		// More specific fields (i.e. those containing subfields)
		// are handled in their own functions to keep things readable.
		start.WriteU8(m_elementId);
		start.WriteU8(m_length);

		SerializeControlField(start);
		SerializeRequestType(start);
		start.WriteHtonU64(m_targetWakeTime);
		SerializeGroupAssignment(start);
		start.WriteU8(m_nominalMinimumWakeDuration);
		start.WriteHtonU16(m_wakeIntervalMantissa);
		start.WriteU8(m_channel);
		SerializeNdpPaging(start);
	}
	void TWTHeader::SerializeTWTInformation(Buffer::Iterator &start) const
	{
		start.WriteU8(m_category);
		start.WriteU8(m_action);
		SerializeInformationField(start);
	}

	void TWTHeader::SerializeInformationField(Buffer::Iterator &start) const
	{
		uint8_t b;
		b |= (m_flowIdentifier & 0b111) << 5;
		b |= (m_twtInformationResponseRequested & 0b1) << 4;
		b |= (m_twtInformationNextTwtRequest & 0b1) << 3;
		b |= (m_twtInformationNextTwtSubfieldSize & 0b11) << 1;
		// 1 reserved bit
		start.WriteU8(b);
		if (m_twtInformationNextTwtSubfieldSize == 1) {
			// 32 bits
			start.WriteHtonU32(m_twtInformationNextTwt);
		} else if (m_twtInformationNextTwtSubfieldSize == 2) {
			WriteHtonU48(start, m_twtInformationNextTwt);
		} else if (m_twtInformationNextTwtSubfieldSize == 3) {
			start.WriteHtonU64(m_twtInformationNextTwt);
		} // else not present, so noop
	}
	void TWTHeader::Serialize(Buffer::Iterator start) const
	{
		if (IsTwtSetupFrame()) {
			SerializeTWTSetup(start);
		} else if (IsTwtTeardownFrame()) {
			SerializeTWTTeardown(start);
		} else if (IsTwtInformationFrame()) {
			SerializeTWTInformation(start);
		} else {
			NS_FATAL_ERROR("Unexpected TWT frame action type.");
		}
	}
	uint8_t TWTHeader::GetElementId() const
	{
		return m_elementId;
	}
	uint8_t TWTHeader::GetLength() const
	{
		return m_length;
	}

	void TWTHeader::DeserializeTwtSetupFrame(Buffer::Iterator &start)
	{
		// Category and action are present in every frame,
		// So we don't deserialize those here
		// Need to do that in calling function to be able to be able to tell what type of frame we have
		m_dialogToken = start.ReadU8();
		m_elementId = start.ReadU8();
		m_length = start.ReadU8();
		DeserializeControlField(start);
		DeserializeRequestType(start);
		m_targetWakeTime = start.ReadNtohU64();
		DeserializeGroupAssignment(start);
		m_nominalMinimumWakeDuration = start.ReadU8();
		m_wakeIntervalMantissa = start.ReadNtohU16();
		m_channel = start.ReadU8();
		DeserializeNdpPaging(start);
	}

	void TWTHeader::DeserializeTwtTeardownFrame(Buffer::Iterator &start)
	{
		uint8_t read{start.ReadU8()};
		m_flowIdentifier = (read >> 5) & 0b111;
	}

	void TWTHeader::DeserializeInformationField(Buffer::Iterator &start)
	{
		uint8_t b = start.ReadU8();
		m_twtInformationNextTwtSubfieldSize = (b >> 1) & 0b11;
		m_twtInformationNextTwtRequest = (b >> 3) & 0b1;
		m_twtInformationResponseRequested = (b >> 4) & 0b1;
		m_flowIdentifier = (b >> 5) & 0b111;
		switch (m_twtInformationNextTwtSubfieldSize) {
			case 1:
				m_twtInformationNextTwt = start.ReadNtohU32();
				break;
			case 2:
				m_twtInformationNextTwt = ReadNtohU48(start);
				break;
			case 3:
				m_twtInformationNextTwt = start.ReadNtohU64();
				break;
			case 0:
			default:
				// No-op
				break;
		}
	}

	uint32_t TWTHeader::Deserialize(Buffer::Iterator start)
	{
		Buffer::Iterator i = start;
		m_category = i.ReadU8();
		m_action = i.ReadU8();
		if (IsTwtSetupFrame()) {
			DeserializeTwtSetupFrame(i);
		} else if (IsTwtTeardownFrame()) {
			DeserializeTwtTeardownFrame(i);
		} else {
			NS_ASSERT_MSG(false, "Unexpected m_action in TWTHeader::Deserialize (or error in implementation).");
		}
		return i.GetDistanceFrom(start);
	}
	void TWTHeader::Print(std::ostream &os) const
	{
		os << "TWT Element header. Data unspecified, print is unsupported at this time.";
	}

	void TWTHeader::SetNdpPaging(bool b)
	{
		m_ndpPagingIndicator = b;
	}
	bool TWTHeader::IsNdpPagingSet() const
	{
		return m_ndpPagingIndicator != 0;
	}
	void TWTHeader::SetResponderPMMode(bool b)
	{
		m_responderPowerManagementMode = b;
	}
	bool TWTHeader::IsResponderPMModeSet() const
	{
		return m_responderPowerManagementMode;
	}
	void TWTHeader::SetRequest(bool b)
	{
		m_isRequest = b;
	}
	bool TWTHeader::IsRequest() const
	{
		return m_isRequest;
	}
	void TWTHeader::SetSetupCommand(uint8_t i)
	{
		NS_ASSERT_MSG((i & 0x07) == i, "Invalid value for SetSetupCommand. The field is only 3 bits long - value must be between 0 and 7.");
		m_setupCommand = i;
	}
	uint8_t TWTHeader::GetSetupCommand() const
	{
		return m_setupCommand;
	}
	void TWTHeader::SetImplicit(bool b)
	{
		m_isImplicit = b;
	}
	bool TWTHeader::IsImplicit() const
	{
		return m_isImplicit;
	}
	void TWTHeader::SetAnnouncedFlowType(bool b)
	{
		m_isAnnounced = b;
	}
	bool TWTHeader::IsAnnouncedFlowType() const
	{
		return m_isAnnounced;
	}
	// Set identifier, unique between any two stations.
	void TWTHeader::SetFlowIdentifier(uint8_t i)
	{
		NS_ASSERT_MSG((i & 0x07) == i, "Invalid value for SetFlowIdentifier. Field is only 3 bits long - value must be between 0 and 7.");
		m_flowIdentifier = i;
	}
	uint8_t TWTHeader::GetFlowIdentifier() const
	{
		return m_flowIdentifier;
	}
	void TWTHeader::SetWakeIntervalExponent(uint8_t i)
	{
		NS_ASSERT_MSG((i & 0x1F) == i,
									"Invalid value for SetWakeIntervalExponent. Field is only 5 bits long - value must be between 0 and 31.");
		m_wakeIntervalExponent = i;
	}
	uint8_t TWTHeader::GetWakeIntervalExponent() const
	{
		return m_wakeIntervalExponent;
	}
	void TWTHeader::SetWakeIntervalMantissa(uint16_t i)
	{
		m_wakeIntervalMantissa = i;
	}
	uint16_t TWTHeader::GetWakeIntervalMantissa() const
	{
		return m_wakeIntervalMantissa;
	}
	void TWTHeader::SetProtection(bool b)
	{
		m_isProtected = b;
	}
	bool TWTHeader::IsProtected() const
	{
		return m_isProtected;
	}

	TypeId TWTHeader::GetTypeId()
	{
		static TypeId tid = TypeId("ns3::TWTHeader").SetParent<Header>().SetGroupName("Network");
		return tid;
	}

	TypeId TWTHeader::GetInstanceTypeId(void) const
	{
		return TWTHeader::GetTypeId();
	}
	uint32_t TWTHeader::GetSerializedSizeForSetupFrameSpecificFields() const
	{
		uint32_t size{0};
		// GetSerializedSize for a setup frame
		size += 1;												 // Dialog token 1 byte
		size += 1 + 1 + 1 + 2 + 1 + 2 + 1; // element id, length, control (1 each)
		// req type (2), nominal wake dur (1), mantissa (2), channel (1)
		if (m_zeroOffsetOfGroupPresent) {
			size += 6;
		}
		if (m_ndpPagingIndicator) {
			size += 4;
		}
		// if m_twtIncluded {
		size += 8;
		//} // We only implement packets with twt included for now
		// if m_groupAssignmentFieldIncluded {
		size += 3;
		//} // We only implement packets with GA field included for now
		return size;
	}
	uint32_t TWTHeader::GetSerializedSizeForTeardownFrameSpecificFields() const
	{
		uint32_t size{0};
		size += 1; // Only contains 1-byte (3 bits + 5 reserved) flow id field
		return size;
	}
	uint32_t TWTHeader::GetSerializedSizeForInformationFrameSpecificFields() const
	{
		NS_FATAL_ERROR("Information frame size is not yet implemented.");
		// 3 bits flow id, 1 bit response request, 1 bit next twt request,
		// 2 bits next twt subfield size, 1 bit reserved
		// 0, 32, 48 or 64 bits next twt
		// So we have 1 byte + whatever size specified in nexttwtsubfieldsize.
		uint32_t size = 1u;
		if (m_twtInformationNextTwtSubfieldSize == 1) {
			size += 4;
		} else if (m_twtInformationNextTwtSubfieldSize == 2) {
			size += 6;
		} else if (m_twtInformationNextTwtSubfieldSize == 3) {
			size += 8;
		} // else size += 0, noop
		return size;
	}
	uint32_t TWTHeader::GetSerializedSizeForConfirmationFrameSpecificFields() const
	{
		// TODO: Verify this is correct.
		// Confirmation frame is simply a setup frame with the action set to accept
		// So the serialized size is equal to the serialized size of a setup frame
		return GetSerializedSizeForSetupFrameSpecificFields();
	}
	uint32_t TWTHeader::GetSerializedSize(void) const
	{
		uint32_t size{0};
		// Category and S1G action fields are present in all options
		size += 2; // And they are 1 byte each
		if (IsTwtSetupFrame()) {
			size += GetSerializedSizeForSetupFrameSpecificFields();
		} else if (IsTwtTeardownFrame()) {
			size += GetSerializedSizeForTeardownFrameSpecificFields();
		} else if (IsTwtInformationFrame()) {
			size += GetSerializedSizeForInformationFrameSpecificFields();
		} else if (IsTwtConfirmationFrame()) {
			size += GetSerializedSizeForConfirmationFrameSpecificFields();
		} else {
			NS_ASSERT_MSG(false, "Encountered unexpected/unhandled type in GetSerializedSize");
		}
		// TODO: Fix this so it also handles all required frame types
		return size;
	}

	uint8_t TWTHeader::GetCategory() const
	{
		return m_category;
	}
	void TWTHeader::SetCategory(uint8_t c)
	{
		m_category = c;
	}

	uint8_t TWTHeader::GetAction() const
	{
		return m_action;
	}
	void TWTHeader::SetAction(uint8_t a)
	{
		m_action = a;
	}
	uint8_t TWTHeader::GetDialogToken() const
	{
		return m_dialogToken;
	}
	void TWTHeader::SetDialogToken(uint8_t t)
	{
		m_dialogToken = t;
	}

	Time TWTHeader::GetNominalMinimumWakeDuration() const
	{
		// m_nominalWakeDuration is set in units of 256 microseconds
		return Time::FromInteger(m_nominalMinimumWakeDuration * 256u, Time::Unit::US);
	}
	void TWTHeader::SetNominalMinimumWakeDuration(Time v)
	{
		// m_nominalWakeDuration is set in units of 256 microseconds
		m_nominalMinimumWakeDuration = static_cast<uint8_t>(v.ToInteger(Time::Unit::US) / 256u + 0.5);
	}
	std::ostream &operator<<(std::ostream &os, const TWTHeader &header)
	{
		header.Print(os);
		return os;
	}
	bool TWTHeader::IsTwtSetupFrame() const
	{
		return m_action == TWT_ACTION_FRAMETYPE_SETUP;
	}
	bool TWTHeader::IsTwtTeardownFrame() const
	{
		return m_action == TWT_ACTION_FRAMETYPE_TEARDOWN;
	}
	bool TWTHeader::IsTwtInformationFrame() const
	{
		return m_action == TWT_ACTION_FRAMETYPE_INFORMATION;
	}
	void TWTHeader::SetTwtSetupFrame()
	{
		m_action = TWT_ACTION_FRAMETYPE_SETUP;
	}
	void TWTHeader::SetTwtTeardownFrame()
	{
		m_action = TWT_ACTION_FRAMETYPE_TEARDOWN;
	}
	void TWTHeader::SetTwtInformationFrame()
	{
		m_action = TWT_ACTION_FRAMETYPE_INFORMATION;
	}

	void TWTHeader::IncrementTargetWakeTime()
	{
		// This function should also not be called after the Implicit TWT session has been updated using a TWT information frame/subfield,
		// specifying a non-zero Next TWT value In that case, some flag should be used to avoid claling this function. If this function would be
		// called in that case, the wake time would be incorrect.
		NS_ASSERT_MSG(m_isImplicit, "IncrementTargetWakeTime() member function is only relevant for implicit TWT sessions.");
		auto increment = m_wakeIntervalMantissa * std::pow(2ull, m_wakeIntervalExponent);
		m_targetWakeTime += increment;
	}

	TWTHeader TWTHeader::CreateConfirmationHeader(const TWTHeader &receivedHeader)
	{
		// The only thing defining TWT requesting vs TWT responding is the request field
		// So we'll only update that one for now until I encounter other differences
		// TODO
		TWTHeader myHeader = receivedHeader;
		myHeader.SetRequest(false);
		myHeader.SetSetupCommand(TWT_SETUPCOMMAND_ACCEPT);
		return myHeader;
	}

	bool TWTHeader::IsInfoResponseRequested() const
	{
		return m_twtInformationResponseRequested;
	}
	bool TWTHeader::IsInfoNextTwtRequested() const
	{
		return m_twtInformationNextTwtRequest;
	}
	uint8_t TWTHeader::GetNextTwtInfoSubfieldSize() const
	{
		return m_twtInformationNextTwtSubfieldSize;
	}
	ns3::Time TWTHeader::GetNextTwtInfo() const
	{
		return ns3::Time::FromInteger(m_twtInformationNextTwt, ns3::Time::Unit::US);
	}
	void TWTHeader::SetInfoResponseRequested(bool b)
	{
		m_twtInformationResponseRequested = b;
	}
	void TWTHeader::SetInfoNextTwtRequested(bool b)
	{
		m_twtInformationNextTwtRequest = b;
	}
	void TWTHeader::SetNextTwtInfoSubfieldSize(uint8_t size)
	{
		m_twtInformationNextTwtSubfieldSize = size;
	}
	void TWTHeader::SetNextTwt(Time time)
	{
		m_targetWakeTime = time.GetMicroSeconds();
	}

	// m_elementId: Per standard, table 9-92 (section 9.4.2.1)
	TWTHeader::TWTHeader()
			: m_category{TWT_CATEGORY_UNPROTECTED_S1G}, m_action{TWT_ACTION_FRAMETYPE_SETUP}, m_dialogToken{0}, m_elementId{TWT_ELEMENT_ID},
				m_length{0}, m_flowIdentifier{0}, m_zeroOffsetOfGroupPresent{false}, m_zeroOffsetOfGroup{0}, m_groupId{0}, m_groupOffsetTimeUnit{0},
				m_groupOffset{0}, m_targetWakeTime{0}, m_responderPowerManagementMode{false}, m_ndpPagingIndicator{false}, m_isRequest{true},
				m_setupCommand{0}, m_isImplicit{false}, m_isAnnounced{false}, m_wakeIntervalExponent{0}, m_isProtected{false}, m_ndpId{0},
				m_maxNdpPagingPeriod{0}, m_ndpPagingPartialTsfOffset{0}, m_ndpPagingAction{0}, m_ndpPagingMinSleepDuration{0},
				m_nominalMinimumWakeDuration{0}, m_wakeIntervalMantissa{0}, m_channel{0}
	{
		this->SetLength();
	}
	TwtPacketTag TwtPacketTag::ERROR()
	{
		static TwtPacketTag errorTag{static_cast<uint64_t>(-1)};
		return errorTag;
	}
	TwtPacketTag::TwtPacketTag(uint64_t i)
	{
		NS_ASSERT(i == static_cast<uint64_t>(-1));
		// Only allowed for error case
		id = i;
	}

	TypeId TwtPacketTag::GetTypeId()
	{
		static TypeId tid = TypeId("ns3::TwtPacketTag").SetParent<ns3::Tag>().SetGroupName("Network");
		return tid;
	}
	TypeId TwtPacketTag::GetInstanceTypeId() const
	{
		return TwtPacketTag::GetTypeId();
	}

	void TwtPacketTag::Serialize(ns3::TagBuffer i) const
	{
		i.WriteU64(id);
	}
	void TwtPacketTag::Deserialize(ns3::TagBuffer i)
	{
		id = i.ReadU64();
	}
	void TwtPacketTag::Print(std::ostream &os) const
	{
		os << "TwtPacketTag with id value = " << id << std::endl;
	}
	uint32_t TwtPacketTag::GetSerializedSize(void) const
	{
		return 8;
	}
	TwtPacketTag TwtPacketTag::Create()
	{
		static uint64_t s_id = 0;
		TwtPacketTag tag;
		tag.id = s_id++;
		return tag;
	}
	TwtPacketTag TwtPacketTag::FromPacket(Ptr<const Packet> packet)
	{
		TwtPacketTag tag;
		if (packet->PeekPacketTag(tag)) {
			return tag;
		}
		return TwtPacketTag::ERROR();
	}
	bool operator==(const TwtPacketTag &a, const TwtPacketTag &b)
	{
		return a.id == b.id;
	}
	bool operator!=(const TwtPacketTag &a, const TwtPacketTag &b)
	{
		return a.id != b.id;
	}
	bool operator<(const TwtPacketTag &a, const TwtPacketTag &b)
	{
		return a.id < b.id;
	}

} // namespace ns3
