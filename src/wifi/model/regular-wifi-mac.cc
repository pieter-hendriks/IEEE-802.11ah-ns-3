/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#include "regular-wifi-mac.h"
#include "dcf-manager.h"
#include "dcf.h"
#include "mac-low.h"
#include "mac-rx-middle.h"
#include "mac-tx-middle.h"
#include "msdu-aggregator.h"
#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/sta-wifi-mac.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/twt-headers.h"
#include "ns3/uinteger.h"
#include "wifi-phy.h"
namespace ns3
{
	NS_LOG_COMPONENT_DEFINE("RegularWifiMac");

	NS_OBJECT_ENSURE_REGISTERED(RegularWifiMac);

	Time computeNextTwtValue(TWTAgreementKey &key, TWTAgreementData &data)
	{
		return (data.header.GetTargetWakeTime() + data.header.GetNominalMinimumWakeDuration() + Seconds(10));
	}
	RegularWifiMac::RegularWifiMac()
	{
		NS_LOG_FUNCTION(this);
		m_rxMiddle = new MacRxMiddle();
		m_rxMiddle->SetForwardCallback(MakeCallback(&RegularWifiMac::Receive, this));

		m_txMiddle = new MacTxMiddle();

		m_low = CreateObject<MacLow>();
		m_low->SetRxCallback(MakeCallback(&MacRxMiddle::Receive, m_rxMiddle));

		m_dcfManager = new DcfManager();
		m_dcfManager->SetupLowListener(m_low);

		m_dca = CreateObject<DcaTxop>();
		m_dca->SetLow(m_low);
		m_dca->SetManager(m_dcfManager);
		m_dca->SetTxMiddle(m_txMiddle);
		m_dca->SetTxOkCallback(MakeCallback(&RegularWifiMac::TxOk, this));
		m_dca->SetTxFailedCallback(MakeCallback(&RegularWifiMac::TxFailed, this));

		m_dca->GetQueue()->TraceConnect("PacketDropped", "", MakeCallback(&RegularWifiMac::OnQueuePacketDropped, this));

		m_dca->TraceConnect("Collision", "", MakeCallback(&RegularWifiMac::OnCollision, this));

		m_dca->TraceConnect("TransmissionWillCrossRAWBoundary", "", MakeCallback(&RegularWifiMac::OnTransmissionWillCrossRAWBoundary, this));

		// Construct the EDCAFs. The ordering is important - highest
		// priority (Table 9-1 UP-to-AC mapping; IEEE 802.11-2012) must be created
		// first.
		SetupEdcaQueue(AC_VO);
		SetupEdcaQueue(AC_VI);
		SetupEdcaQueue(AC_BE);
		SetupEdcaQueue(AC_BK);

		m_twtChangedCallback = nullptr;
		m_twtParameterAcceptanceFunction = trivialTwtAccept;
		m_twtAlternativeConfigurationFunction = nullptr;

		m_twtStartOfWakePeriodCallback = nullptr;
		m_twtEndOfWakePeriodCallback = nullptr;

		m_computeNextTwtValue = computeNextTwtValue;
	}

	void RegularWifiMac::OnQueuePacketDropped(std::string context, Ptr<const Packet> packet, DropReason reason)
	{
		m_packetdropped(packet, reason);
	}

	void RegularWifiMac::OnCollision(std::string context, uint32_t nrOfBackOffSlots)
	{
		m_collisionTrace(nrOfBackOffSlots);
	}

	void RegularWifiMac::OnTransmissionWillCrossRAWBoundary(std::string context, Time txDuration, Time remainingTimeInRAWSlot)
	{
		std::cout << "TRANSMISSION WILL CROSS RAW BOUNDARY" << std::endl;
		m_transmissionWillCrossRAWBoundary(txDuration, remainingTimeInRAWSlot);
	}

	RegularWifiMac::~RegularWifiMac()
	{
		NS_LOG_FUNCTION(this);
	}

	void RegularWifiMac::DoInitialize()
	{
		NS_LOG_FUNCTION(this);
		m_dca->Initialize();

		for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i) {
			i->second->Initialize();
		}
	}

	void RegularWifiMac::DoDispose()
	{
		NS_LOG_FUNCTION(this);
		delete m_rxMiddle;
		m_rxMiddle = 0;

		delete m_txMiddle;
		m_txMiddle = 0;

		delete m_dcfManager;
		m_dcfManager = 0;

		m_low->Dispose();
		m_low = 0;

		m_phy = 0;
		m_stationManager = 0;

		m_dca->Dispose();
		m_dca = 0;

		for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i) {
			i->second = 0;
		}
	}

	void RegularWifiMac::SetWifiRemoteStationManager(Ptr<WifiRemoteStationManager> stationManager)
	{
		NS_LOG_FUNCTION(this << stationManager);
		m_stationManager = stationManager;
		m_stationManager->SetHtSupported(GetHtSupported());
		// m_stationManager->SetS1gSupported (GetS1gSupported ()); to support
		m_low->SetWifiRemoteStationManager(stationManager);

		m_dca->SetWifiRemoteStationManager(stationManager);

		for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i) {
			i->second->SetWifiRemoteStationManager(stationManager);
		}
	}

	Ptr<WifiRemoteStationManager> RegularWifiMac::GetWifiRemoteStationManager() const
	{
		return m_stationManager;
	}

	void RegularWifiMac::SetupEdcaQueue(enum AcIndex ac)
	{
		NS_LOG_FUNCTION(this << ac);

		// Our caller shouldn't be attempting to setup a queue that is
		// already configured.
		NS_ASSERT(m_edca.find(ac) == m_edca.end());

		Ptr<EdcaTxopN> edca = CreateObject<EdcaTxopN>();
		edca->SetLow(m_low);
		edca->SetManager(m_dcfManager);
		edca->SetTxMiddle(m_txMiddle);
		edca->SetTxOkCallback(MakeCallback(&RegularWifiMac::TxOk, this));
		edca->SetTxFailedCallback(MakeCallback(&RegularWifiMac::TxFailed, this));
		edca->SetAccessCategory(ac);
		edca->CompleteConfig();
		m_edca.insert(std::make_pair(ac, edca));

		edca->GetEdcaQueue()->TraceConnect("PacketDropped", "", MakeCallback(&RegularWifiMac::OnQueuePacketDropped, this));
		edca->TraceConnect("Collision", "", MakeCallback(&RegularWifiMac::OnCollision, this));
		edca->TraceConnect("TransmissionWillCrossRAWBoundary", "", MakeCallback(&RegularWifiMac::OnTransmissionWillCrossRAWBoundary, this));
	}

	void RegularWifiMac::SetTypeOfStation(TypeOfStation type)
	{
		NS_LOG_FUNCTION(this << type);
		for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i) {
			i->second->SetTypeOfStation(type);
		}
	}

	Ptr<DcaTxop> RegularWifiMac::GetDcaTxop() const
	{
		return m_dca;
	}

	Ptr<EdcaTxopN> RegularWifiMac::GetVOQueue() const
	{
		return m_edca.find(AC_VO)->second;
	}

	Ptr<EdcaTxopN> RegularWifiMac::GetVIQueue() const
	{
		return m_edca.find(AC_VI)->second;
	}

	Ptr<EdcaTxopN> RegularWifiMac::GetBEQueue() const
	{
		return m_edca.find(AC_BE)->second;
	}

	Ptr<EdcaTxopN> RegularWifiMac::GetBKQueue() const
	{
		return m_edca.find(AC_BK)->second;
	}

	void RegularWifiMac::SetWifiPhy(Ptr<WifiPhy> phy)
	{
		NS_LOG_FUNCTION(this << phy);
		m_phy = phy;
		m_dcfManager->SetupPhyListener(phy);
		m_low->SetPhy(phy);
	}

	Ptr<WifiPhy> RegularWifiMac::GetWifiPhy(void) const
	{
		NS_LOG_FUNCTION(this);
		return m_phy;
	}

	void RegularWifiMac::ResetWifiPhy(void)
	{
		NS_LOG_FUNCTION(this);
		m_low->ResetPhy();
		m_dcfManager->RemovePhyListener(m_phy);
		m_phy = 0;
	}

	void RegularWifiMac::SetForwardUpCallback(ForwardUpCallback upCallback)
	{
		NS_LOG_FUNCTION(this);
		m_forwardUp = upCallback;
	}

	void RegularWifiMac::SetLinkUpCallback(Callback<void> linkUp)
	{
		NS_LOG_FUNCTION(this);
		m_linkUp = linkUp;
	}

	void RegularWifiMac::SetLinkDownCallback(Callback<void> linkDown)
	{
		NS_LOG_FUNCTION(this);
		m_linkDown = linkDown;
	}

	void RegularWifiMac::SetQosSupported(bool enable)
	{
		NS_LOG_FUNCTION(this);
		m_qosSupported = enable;
	}

	bool RegularWifiMac::GetQosSupported() const
	{
		return m_qosSupported;
	}

	void RegularWifiMac::SetHtSupported(bool enable)
	{
		NS_LOG_FUNCTION(this);
		m_htSupported = enable;
	}

	bool RegularWifiMac::GetHtSupported() const
	{
		return m_htSupported;
	}

	void RegularWifiMac::SetS1gSupported(bool enable)
	{
		NS_LOG_FUNCTION(this);
		m_s1gSupported = enable;
	}

	bool RegularWifiMac::GetS1gSupported() const
	{
		return m_s1gSupported;
	}

	void RegularWifiMac::SetS1gStaType(uint8_t type)
	{
		NS_LOG_FUNCTION(this);
		m_s1gStaType = type;
	}

	uint8_t RegularWifiMac::GetS1gStaType(void) const
	{
		NS_LOG_FUNCTION(this);
		return m_s1gStaType;
	}

	void RegularWifiMac::SetCtsToSelfSupported(bool enable)
	{
		NS_LOG_FUNCTION(this);
		m_low->SetCtsToSelfSupported(enable);
	}

	bool RegularWifiMac::GetCtsToSelfSupported() const
	{
		return m_low->GetCtsToSelfSupported();
	}

	void RegularWifiMac::SetSlot(Time slotTime)
	{
		NS_LOG_FUNCTION(this << slotTime);
		m_dcfManager->SetSlot(slotTime);
		m_low->SetSlotTime(slotTime);
	}

	Time RegularWifiMac::GetSlot(void) const
	{
		return m_low->GetSlotTime();
	}

	void RegularWifiMac::SetSifs(Time sifs)
	{
		NS_LOG_FUNCTION(this << sifs);
		m_dcfManager->SetSifs(sifs);
		m_low->SetSifs(sifs);
	}

	Time RegularWifiMac::GetSifs(void) const
	{
		return m_low->GetSifs();
	}

	void RegularWifiMac::SetEifsNoDifs(Time eifsNoDifs)
	{
		NS_LOG_FUNCTION(this << eifsNoDifs);
		m_dcfManager->SetEifsNoDifs(eifsNoDifs);
	}

	Time RegularWifiMac::GetEifsNoDifs(void) const
	{
		return m_dcfManager->GetEifsNoDifs();
	}

	void RegularWifiMac::SetRifs(Time rifs)
	{
		NS_LOG_FUNCTION(this << rifs);
		m_low->SetRifs(rifs);
	}

	Time RegularWifiMac::GetRifs(void) const
	{
		return m_low->GetRifs();
	}

	void RegularWifiMac::SetPifs(Time pifs)
	{
		NS_LOG_FUNCTION(this << pifs);
		m_low->SetPifs(pifs);
	}

	Time RegularWifiMac::GetPifs(void) const
	{
		return m_low->GetPifs();
	}

	void RegularWifiMac::SetAckTimeout(Time ackTimeout)
	{
		NS_LOG_FUNCTION(this << ackTimeout);
		m_low->SetAckTimeout(ackTimeout);
	}

	Time RegularWifiMac::GetAckTimeout(void) const
	{
		return m_low->GetAckTimeout();
	}

	void RegularWifiMac::SetCtsTimeout(Time ctsTimeout)
	{
		NS_LOG_FUNCTION(this << ctsTimeout);
		m_low->SetCtsTimeout(ctsTimeout);
	}

	Time RegularWifiMac::GetCtsTimeout(void) const
	{
		return m_low->GetCtsTimeout();
	}

	void RegularWifiMac::SetBasicBlockAckTimeout(Time blockAckTimeout)
	{
		NS_LOG_FUNCTION(this << blockAckTimeout);
		m_low->SetBasicBlockAckTimeout(blockAckTimeout);
	}

	Time RegularWifiMac::GetBasicBlockAckTimeout(void) const
	{
		return m_low->GetBasicBlockAckTimeout();
	}

	void RegularWifiMac::SetCompressedBlockAckTimeout(Time blockAckTimeout)
	{
		NS_LOG_FUNCTION(this << blockAckTimeout);
		m_low->SetCompressedBlockAckTimeout(blockAckTimeout);
	}

	Time RegularWifiMac::GetCompressedBlockAckTimeout(void) const
	{
		return m_low->GetCompressedBlockAckTimeout();
	}

	void RegularWifiMac::SetAddress(Mac48Address address)
	{
		NS_LOG_FUNCTION(this << address);
		m_low->SetAddress(address);
	}

	Mac48Address RegularWifiMac::GetAddress(void) const
	{
		return m_low->GetAddress();
	}

	void RegularWifiMac::SetSsid(Ssid ssid)
	{
		NS_LOG_FUNCTION(this << ssid);
		m_ssid = ssid;
	}

	Ssid RegularWifiMac::GetSsid(void) const
	{
		return m_ssid;
	}

	void RegularWifiMac::SetBssid(Mac48Address bssid)
	{
		NS_LOG_FUNCTION(this << bssid);
		m_low->SetBssid(bssid);
	}

	Mac48Address RegularWifiMac::GetBssid(void) const
	{
		return m_low->GetBssid();
	}

	void RegularWifiMac::SetPromisc(void)
	{
		m_low->SetPromisc();
	}

	void RegularWifiMac::Enqueue(Ptr<const Packet> packet, Mac48Address to, Mac48Address from)
	{
		// We expect RegularWifiMac subclasses which do support forwarding (e.g.,
		// AP) to override this method. Therefore, we throw a fatal error if
		// someone tries to invoke this method on a class which has not done
		// this.
		NS_FATAL_ERROR("This MAC entity (" << this << ", " << GetAddress() << ") does not support Enqueue() with from address");
	}

	bool RegularWifiMac::SupportsSendFrom(void) const
	{
		return false;
	}

	void RegularWifiMac::ForwardUp(Ptr<Packet> packet, Mac48Address from, Mac48Address to)
	{
		NS_LOG_FUNCTION(this << packet << from);
		m_forwardUp(packet, from, to);
	}
	void RegularWifiMac::HandleManagementActionFrame(Ptr<Packet> packet, const WifiMacHeader *hdr)
	{
		Mac48Address from = hdr->GetAddr2();
		// There is currently only any reason for Management Action
		// frames to be flying about if we are a QoS STA.
		NS_ASSERT(m_qosSupported);

		WifiActionHeader actionHdr;
		packet->RemoveHeader(actionHdr);

		switch (actionHdr.GetCategory()) {
			case WifiActionHeader::BLOCK_ACK:

				switch (actionHdr.GetAction().blockAck) {
					case WifiActionHeader::BLOCK_ACK_ADDBA_REQUEST: {
						MgtAddBaRequestHeader reqHdr;
						packet->RemoveHeader(reqHdr);

						// We've received an ADDBA Request. Our policy here is
						// to automatically accept it, so we get the ADDBA
						// Response on it's way immediately.
						SendAddBaResponse(&reqHdr, from);
						// This frame is now completely dealt with, so we're done.
						return;
					}
					case WifiActionHeader::BLOCK_ACK_ADDBA_RESPONSE: {
						MgtAddBaResponseHeader respHdr;
						packet->RemoveHeader(respHdr);

						// We've received an ADDBA Response. We assume that it
						// indicates success after an ADDBA Request we have
						// sent (we could, in principle, check this, but it
						// seems a waste given the level of the current model)
						// and act by locally establishing the agreement on
						// the appropriate queue.
						AcIndex ac = QosUtilsMapTidToAc(respHdr.GetTid());
						m_edca[ac]->GotAddBaResponse(&respHdr, from);
						// This frame is now completely dealt with, so we're done.
						return;
					}
					case WifiActionHeader::BLOCK_ACK_DELBA: {
						MgtDelBaHeader delBaHdr;
						packet->RemoveHeader(delBaHdr);

						if (delBaHdr.IsByOriginator()) {
							// This DELBA frame was sent by the originator, so
							// this means that an ingoing established
							// agreement exists in MacLow and we need to
							// destroy it.
							m_low->DestroyBlockAckAgreement(from, delBaHdr.GetTid());
						} else {
							// We must have been the originator. We need to
							// tell the correct queue that the agreement has
							// been torn down
							AcIndex ac = QosUtilsMapTidToAc(delBaHdr.GetTid());
							m_edca[ac]->GotDelBaFrame(&delBaHdr, from);
						}
						// This frame is now completely dealt with, so we're done.
						return;
					}
					default:
						NS_FATAL_ERROR("Unsupported Action field in Block Ack Action frame");
						return;
				}
			default:
				NS_FATAL_ERROR("Unsupported Action frame received");
				return;
		}
	}
	void RegularWifiMac::HandlePsPollFrame(Ptr<Packet> packet, const WifiMacHeader *hdr)
	{
		NS_ASSERT(hdr->IsPsPoll());
		// There was no handling of PS-Poll frames previous to the TWT implementation
		// So at this level, we only handle the TWT announcement function of those frames
		// Any other handling implemented must ensure it still passes the frame to us,
		// to avoid breaking TWT functionality.
		// TODO: Clarify this in documentation etc.
		this->HandleTwtAnnouncementFrame(packet, hdr);
	}

	void RegularWifiMac::HandleTackFrame(Ptr<Packet> packet, const WifiMacHeader *hdr)
	{
		NS_ASSERT(hdr->IsTackFrame());
		// In case of TACK frames from a station we have a TWT session with, we may receive a Next-TWT value that we should use.
		if (hdr->IsNextTwtFieldPresent()) {
			auto nextTwtInfo = hdr->GetTackNextTwtInfo();
			TWTAgreementKey key{hdr->GetAddr2(), nextTwtInfo.first};
			auto *agreement = GetTwtAgreementIfExists(key);
			if (agreement == nullptr) {
				NS_FATAL_ERROR("Unexpected TACK frame.");
			}
			HandleNextTwtInfoField(key, *agreement, nextTwtInfo);
		}
	}

	std::vector<TWTAgreementData *> RegularWifiMac::GetTwtAgreements(const Mac48Address &addr)
	{
		std::vector<TWTAgreementData *> ret;
		for (uint8_t i = 0u; i < 8; ++i) {
			// Iterate over all possible flow identifiers
			auto key = TWTAgreementKey{addr, i};
			auto *result = GetTwtAgreementIfExists(key);
			if (result != nullptr) {
				ret.push_back(result);
			}
		}
		return ret;
	}
	void RegularWifiMac::SendAnnouncedTwtWakeupMessage(Mac48Address to)
	{
		// We'll use this function to send APSD trigger frame
		// Non-AP sta will overload to use PS-Poll frames instead
		NS_FATAL_ERROR("Base send Announcement message used - STA should overload this for PS-Poll messages.");
		auto packet = Create<Packet>();
		auto hdr = WifiMacHeader();
		hdr.SetAddr1(to);
		hdr.SetAddr3(to);
		hdr.SetAddr2(GetAddress());
		hdr.SetAddr4(GetAddress());
		hdr.SetTwtFrame();
		packet->AddPacketTag(TwtPacketTag::Create());
		this->QueueWithTwt(packet, hdr);
	}
	void RegularWifiMac::HandleControlFrame(Ptr<Packet> packet, const WifiMacHeader *hdr)
	{
		NS_ASSERT(hdr->IsCtl());
		if (hdr->IsPsPoll()) {
			HandlePsPollFrame(packet, hdr);
		} else if (hdr->IsTackFrame()) {
			HandleTackFrame(packet, hdr);
		} else {
			NS_FATAL_ERROR("Handling of this control frame is not implemented: type = " << hdr->GetType());
		}
	}
	void RegularWifiMac::Receive(Ptr<Packet> packet, const WifiMacHeader *hdr)
	{
		NS_LOG_FUNCTION(this << packet << hdr);

		// We don't know how to deal with any frame that is not addressed to
		// us (and odds are there is nothing sensible we could do anyway),
		// so we ignore such frames.
		//
		// The derived class may also do some such filtering, but it doesn't
		// hurt to have it here too as a backstop.
		if (hdr->GetAddr1() != GetAddress()) {
			return;
		}
		if (hdr->IsMgt() && hdr->IsAction()) {
			HandleManagementActionFrame(packet, hdr);
		} else if (hdr->IsTwtFrame()) {
			HandleTwtFrame(packet, hdr);
		} else if (hdr->IsCtl()) {
			HandleControlFrame(packet, hdr);
		} else {
			NS_FATAL_ERROR("Don't know how to handle frame (type=" << hdr->GetType());
		}
	}

	void RegularWifiMac::DeaggregateAmsduAndForward(Ptr<Packet> aggregatedPacket, const WifiMacHeader *hdr)
	{
		MsduAggregator::DeaggregatedMsdus packets = MsduAggregator::Deaggregate(aggregatedPacket);

		for (MsduAggregator::DeaggregatedMsdusCI i = packets.begin(); i != packets.end(); ++i) {
			ForwardUp((*i).first, (*i).second.GetSourceAddr(), (*i).second.GetDestinationAddr());
		}
	}

	void RegularWifiMac::SendAddBaResponse(const MgtAddBaRequestHeader *reqHdr, Mac48Address originator)
	{
		NS_LOG_FUNCTION(this);
		WifiMacHeader hdr;
		hdr.SetAction();
		hdr.SetAddr1(originator);
		hdr.SetAddr2(GetAddress());
		hdr.SetAddr3(GetAddress());
		hdr.SetDsNotFrom();
		hdr.SetDsNotTo();

		MgtAddBaResponseHeader respHdr;
		StatusCode code;
		code.SetSuccess();
		respHdr.SetStatusCode(code);
		// Here a control about queues type?
		respHdr.SetAmsduSupport(reqHdr->IsAmsduSupported());

		if (reqHdr->IsImmediateBlockAck()) {
			respHdr.SetImmediateBlockAck();
		} else {
			respHdr.SetDelayedBlockAck();
		}
		respHdr.SetTid(reqHdr->GetTid());
		// For now there's not no control about limit of reception. We
		// assume that receiver has no limit on reception. However we assume
		// that a receiver sets a bufferSize in order to satisfy next
		// equation: (bufferSize + 1) % 16 = 0 So if a recipient is able to
		// buffer a packet, it should be also able to buffer all possible
		// packet's fragments. See section 7.3.1.14 in IEEE802.11e for more details.
		respHdr.SetBufferSize(1023);
		respHdr.SetTimeout(reqHdr->GetTimeout());

		WifiActionHeader actionHdr;
		WifiActionHeader::ActionValue action;
		action.blockAck = WifiActionHeader::BLOCK_ACK_ADDBA_RESPONSE;
		actionHdr.SetAction(WifiActionHeader::BLOCK_ACK, action);

		Ptr<Packet> packet = Create<Packet>();
		packet->AddHeader(respHdr);
		packet->AddHeader(actionHdr);

		// We need to notify our MacLow object as it will have to buffer all
		// correctly received packets for this Block Ack session
		m_low->CreateBlockAckAgreement(&respHdr, originator, reqHdr->GetStartingSequence());

		// It is unclear which queue this frame should go into. For now we
		// bung it into the queue corresponding to the TID for which we are
		// establishing an agreement, and push it to the head.
		m_edca[QosUtilsMapTidToAc(reqHdr->GetTid())]->PushFront(packet, hdr);
	}

	TypeId RegularWifiMac::GetTypeId(void)
	{
		static TypeId tid =
				TypeId("ns3::RegularWifiMac")
						.SetParent<WifiMac>()
						.SetGroupName("Wifi")
						.AddAttribute("QosSupported", "This Boolean attribute is set to enable 802.11e/WMM-style QoS support at this STA",
													BooleanValue(false), MakeBooleanAccessor(&RegularWifiMac::SetQosSupported, &RegularWifiMac::GetQosSupported),
													MakeBooleanChecker())
						.AddAttribute("HtSupported", "This Boolean attribute is set to enable 802.11n support at this STA", BooleanValue(false),
													MakeBooleanAccessor(&RegularWifiMac::SetHtSupported, &RegularWifiMac::GetHtSupported), MakeBooleanChecker())
						//
						.AddAttribute("S1gSupported", "This Boolean attribute is set to enable 802.11ah support at this STA", BooleanValue(false),
													MakeBooleanAccessor(&RegularWifiMac::SetS1gSupported, &RegularWifiMac::GetS1gSupported), MakeBooleanChecker())
						.AddAttribute("S1gStaType",
													"S1g STA type, for non-AP STA, 1 for sensor STA, 2 for non-sensor STA; for AP STA, 1 for "
													"sensor STA, 2 for non-sensor STA, 0 for both STA",
													UintegerValue(1), MakeUintegerAccessor(&RegularWifiMac::SetS1gStaType, &RegularWifiMac::GetS1gStaType),
													MakeUintegerChecker<uint32_t>())
						//
						.AddAttribute("CtsToSelfSupported", "Use CTS to Self when using a rate that is not in the basic set rate", BooleanValue(false),
													MakeBooleanAccessor(&RegularWifiMac::SetCtsToSelfSupported, &RegularWifiMac::GetCtsToSelfSupported),
													MakeBooleanChecker())
						.AddAttribute("DcaTxop", "The DcaTxop object", PointerValue(), MakePointerAccessor(&RegularWifiMac::GetDcaTxop),
													MakePointerChecker<DcaTxop>())
						.AddAttribute("VO_EdcaTxopN", "Queue that manages packets belonging to AC_VO access class", PointerValue(),
													MakePointerAccessor(&RegularWifiMac::GetVOQueue), MakePointerChecker<EdcaTxopN>())
						.AddAttribute("VI_EdcaTxopN", "Queue that manages packets belonging to AC_VI access class", PointerValue(),
													MakePointerAccessor(&RegularWifiMac::GetVIQueue), MakePointerChecker<EdcaTxopN>())
						.AddAttribute("BE_EdcaTxopN", "Queue that manages packets belonging to AC_BE access class", PointerValue(),
													MakePointerAccessor(&RegularWifiMac::GetBEQueue), MakePointerChecker<EdcaTxopN>())
						.AddAttribute("BK_EdcaTxopN", "Queue that manages packets belonging to AC_BK access class", PointerValue(),
													MakePointerAccessor(&RegularWifiMac::GetBKQueue), MakePointerChecker<EdcaTxopN>())
						.AddTraceSource("TxOkHeader", "The header of successfully transmitted packet",
														MakeTraceSourceAccessor(&RegularWifiMac::m_txOkCallback), "ns3::WifiMacHeader::TracedCallback")
						.AddTraceSource("TxErrHeader", "The header of unsuccessfully transmitted packet",
														MakeTraceSourceAccessor(&RegularWifiMac::m_txErrCallback), "ns3::WifiMacHeader::TracedCallback")
						.AddTraceSource("PacketDropped",
														"Trace source indicating a packet "
														"has been dropped from one of the queues",
														MakeTraceSourceAccessor(&RegularWifiMac::m_packetdropped), "ns3::RegularWifiMac::PacketDroppedCallback")

						.AddTraceSource("Collision", "Fired when a collision occurred", MakeTraceSourceAccessor(&RegularWifiMac::m_collisionTrace),
														"ns3::RegularWifiMac::CollisionCallback")
						.AddTraceSource("TransmissionWillCrossRAWBoundary",
														"Fired when a transmission is held off because it won't fit inside the RAW slot",
														MakeTraceSourceAccessor(&RegularWifiMac::m_transmissionWillCrossRAWBoundary),
														"ns3::RegularWifiMac::TransmissionWillCrossRAWBoundaryCallback")

				;
		return tid;
	}

	void RegularWifiMac::FinishConfigureStandard(enum WifiPhyStandard standard)
	{
		uint32_t cwmin;
		uint32_t cwmax;

		switch (standard) {
			case WIFI_PHY_STANDARD_holland:
			case WIFI_PHY_STANDARD_80211a:
			case WIFI_PHY_STANDARD_80211g:
			case WIFI_PHY_STANDARD_80211_10MHZ:
			case WIFI_PHY_STANDARD_80211_5MHZ:
			case WIFI_PHY_STANDARD_80211n_5GHZ:
			case WIFI_PHY_STANDARD_80211n_2_4GHZ:
			case WIFI_PHY_STANDARD_80211ah:
				cwmin = 15;
				cwmax = 1023;
				break;
			case WIFI_PHY_STANDARD_80211b:
				cwmin = 31;
				cwmax = 1023;
				break;
			default:
				NS_FATAL_ERROR("Unsupported WifiPhyStandard in RegularWifiMac::FinishConfigureStandard ()");
		}

		// The special value of AC_BE_NQOS which exists in the Access
		// Category enumeration allows us to configure plain old DCF.
		ConfigureDcf(m_dca, cwmin, cwmax, AC_BE_NQOS);

		// Now we configure the EDCA functions
		for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i) {
			ConfigureDcf(i->second, cwmin, cwmax, i->first);
		}
	}

	void RegularWifiMac::TxOk(const WifiMacHeader &hdr)
	{
		NS_LOG_FUNCTION(this << hdr);
		m_txOkCallback(hdr);
	}

	void RegularWifiMac::TxFailed(const WifiMacHeader &hdr)
	{
		NS_LOG_FUNCTION(this << hdr);
		m_txErrCallback(hdr);
	}
	TWTAgreementData *RegularWifiMac::GetTwtAgreementIfExists(const TWTAgreementKey &key)
	{
		auto location = m_twtAgreements.find(key);
		if (location != m_twtAgreements.end()) {
			return &(location->second);
		}
		// No such agreement
		return nullptr;
	}
	uint8_t RegularWifiMac::GetLastBeaconSequence() const
	{
		return 0;
	}
	void RegularWifiMac::RequestingSTASendTwtAcknowledgment(Ptr<Packet> packet, const WifiMacHeader *hdr, const TWTHeader &twtHeader)
	{
		// Pentapartial/tetrapartial timestamps, next twt info and change sequence fields should be all zeroes
		// when transmitted by TWT requesting STA.
		WifiMacHeader ackHeader;
		ackHeader.SetTackFrame();
		// a TACK frame has frame control (= Pversion, type, subtype, bandwidth, dynamic, power, more data, flow control, nexttwtpresent)
		// Frame control is set automatically, afaik
		// duration, addr1, addr2, beaconsequence,
		// timestamp, nexttwtinfo(optional)
		// FCS added later.
		ackHeader.SetAddr1(hdr->GetAddr2());
		ackHeader.SetAddr2(GetAddress());
		// TWT requesting station sending a tack frame with beacon sequence shall set field to zeroes
		ackHeader.SetTackBeaconSequence(0);
		// TWT requesting station sending a TACK frame shall set the timestamp to zeroes
		ackHeader.SetTackTimestamp(Time::FromInteger(0, Time::Unit::US));
		// Create new packet, send with new header.
		auto p = Create<Packet>();
		p->AddPacketTag(TwtPacketTag::Create());
		this->QueueWithTwt(p, ackHeader);
	}
	void RegularWifiMac::RespondingSTASendTwtAcknowledgment(Ptr<Packet> packet, const WifiMacHeader *hdr, const TWTHeader &twtHeader)
	{
		WifiMacHeader ackHeader;
		ackHeader.SetTackFrame();
		ackHeader.SetAddr1(hdr->GetAddr2());
		ackHeader.SetAddr2(GetAddress());
		ackHeader.SetTackBeaconSequence(GetLastBeaconSequence());
		ackHeader.SetTackTimestamp(Simulator::Now());
		TWTAgreementKey key{hdr->GetAddr2(), twtHeader.GetFlowIdentifier()};
		auto *data = GetTwtAgreementIfExists(key);
		if (data != nullptr) {
			if (!twtHeader.IsImplicit() && !data->periodicTwtOverridden) {
				// Set the NextTwtInfo field if it's an explicit agreement
				// And we've not yet sent one of these.
				// No user-modification allowed here at the moment, but should modify so there's some sort of callback to allow user control
				// TODO
				Time nextTwt = m_computeNextTwtValue(key, *data);
				ackHeader.SetTackNextTwtInfo(nextTwt, twtHeader.GetFlowIdentifier());
			}
		}
		auto p = Create<Packet>();
		p->AddPacketTag(TwtPacketTag::Create());
		this->QueueWithTwt(p, ackHeader);
	}

	void RegularWifiMac::SendTwtAcknowledgmentIfNecessary(Ptr<Packet> packet, const WifiMacHeader *hdr, const TWTHeader &twtHeader)
	{
		// Implementation of TWT acknowledgment procedure as described in 802.11-2020 standard, section 10.47.2
		bool isRequestingSTA;
		auto *agreement = GetTwtAgreementIfExists({hdr->GetAddr2(), twtHeader.GetFlowIdentifier()});
		if (agreement == nullptr) {
			// In this case, we must derive our role from the header.
			// This should be fairly simple - either IsRequest is set or it's not.
			isRequestingSTA = !twtHeader.IsRequest();
			// When the flag is set, the header is a request that we have received.
			// This means we are the responding station.
		} else {
			// Here, we should have our role stored in the agreementdata.
			isRequestingSTA = (agreement->myRole == TWT_REQUESTING_STA);
		}
		if (isRequestingSTA) {
			RequestingSTASendTwtAcknowledgment(packet, hdr, twtHeader);
		} else {
			RespondingSTASendTwtAcknowledgment(packet, hdr, twtHeader);
		}
	}
	void RegularWifiMac::HandleTwtAnnouncementFrame(Ptr<Packet> packet, const WifiMacHeader *hdr)
	{
		NS_LOG_UNCOND("Handling TWT Announcement frame.");
		auto agreements = GetTwtAgreements(hdr->GetAddr2());
		for (auto agreement : agreements) {
			if (agreement->IsActive()) {
				auto key = TWTAgreementKey{hdr->GetAddr2(), agreement->header.GetFlowIdentifier()};
				this->HandleStartOfWakePeriod(key, *agreement);
			}
		}
		NS_LOG_UNCOND("Activated ALL twt agreements trigger frame could apply to.");
		return;
	}
	void RegularWifiMac::HandleTwtFrame(Ptr<Packet> packet, const WifiMacHeader *hdr)
	{
		NS_ASSERT_MSG(this->GetConfiguredStandard() == WifiPhyStandard::WIFI_PHY_STANDARD_80211ah, "TWT only implemented for 802.11ah");
		// Keep a log of TWT packets we've received to filter duplicates.
		static std::set<uint64_t> previous{};
		auto tag = TwtPacketTag::FromPacket(packet);
		if (previous.find(tag.id) != previous.end()) {
			NS_LOG_UNCOND("Encountered duplicate TWT frame (id=" << tag.id << "), dropping...");
			NotifyRxDrop(packet);
			return;
		}
		previous.insert(tag.id);
		if (packet->GetSize() < 5) {
			this->HandleTwtAnnouncementFrame(packet, hdr);
			return;
		}
		// Get the TWTHeader.
		TWTHeader twtHeader;
		packet->RemoveHeader(twtHeader);

		// TWT Acknowledgement handling
		SendTwtAcknowledgmentIfNecessary(packet, hdr, twtHeader);

		NS_LOG_UNCOND("Received non-duplicate TWT frame, id = " << tag.id);
		if (twtHeader.IsTwtConfirmationFrame()) {
			NS_LOG_UNCOND("TWT frame is confirmation.");
			HandleTwtConfirmationFrame(packet, hdr, &twtHeader);
		} else if (twtHeader.IsTwtTeardownFrame()) {
			NS_LOG_UNCOND("TWT frame is Teardown.");
			HandleTwtTeardownFrame(packet, hdr, &twtHeader);
		} else if (twtHeader.IsTwtInformationFrame()) {
			NS_LOG_UNCOND("TWT frame is Information.");
			HandleTwtInformationFrame(packet, hdr, &twtHeader);
		} else if (twtHeader.IsTwtSetupFrame()) {
			NS_LOG_UNCOND("TWT frame is Setup.");
			HandleTwtSetupFrame(packet, hdr, &twtHeader);
		} else {
			NS_ASSERT_MSG(false, "Support for this twt frame type is not yet implemented.");
			// TODO: Keep this up to date - ensure we handle all packet types.
		}
	}

	Time RegularWifiMac::GetNextWakeUpTime() const
	{
		Time closest = Time::Max();
		// have now in var for guaranteed consistency
		// I believe ns3 time shouldn't progress while we're looping over this but better safe than sorry.
		// Would cause rather hard to find non-compliant behaviour if the assumption were wrong
		Time now = Simulator::Now();
		for (const auto &agreement : m_twtAgreements) {
			// Get the target wake time stored in the agreement
			auto timeUntilAgreement = agreement.second.header.GetTargetWakeTime() - now;
			if (timeUntilAgreement < closest)
				closest = timeUntilAgreement;
		}
		return closest;
	}
	Time RegularWifiMac::GetNextWakeUpTimeForAddress(const Mac48Address &APMacAddress) const
	{
		Time closest = Time::Max();
		Time now = Simulator::Now();
		std::list<std::reference_wrapper<const TWTAgreementData>> agreements;
		for (const auto &agreement : m_twtAgreements) {
			if (agreement.first.macAddress == APMacAddress) {
				agreements.emplace_back(std::ref(agreement.second));
			}
		}
		for (const auto &agreementData : agreements) {
			// Get the target wake time stored in the agreement
			auto timeUntilAgreement = agreementData.get().header.GetTargetWakeTime() - now;
			if (timeUntilAgreement < closest)
				closest = timeUntilAgreement;
		}
		return closest;
	}
	bool trivialTwtAccept(const TWTHeader &hdr)
	{
		NS_UNUSED(hdr);
		return true;
	}
	void RegularWifiMac::SendTwtSetupFrame(const Mac48Address &destination)
	{
		NS_ASSERT_MSG(this->GetConfiguredStandard() == WifiPhyStandard::WIFI_PHY_STANDARD_80211ah, "TWT only implemented for 802.11ah");
		NS_LOG_UNCOND("Time @ twt setup frame entry: " << Simulator::Now().GetMilliSeconds());
		NS_LOG_UNCOND("SendTwtSetupFrame START.");

		Ptr<Packet> packet = Create<Packet>();
		// Packet structure: Inner-most header to outermost.
		// TWT Element
		// TWT Action Frame (Both twt headers are serialized when TWTHeader is done)
		// WifiMacHeader
		TWTHeader twtHeader;
		// Sleep for 60 seconds
		Time time = Simulator::Now();
		// First wake up 3 seconds from now,
		twtHeader.SetTargetWakeTime(time + Seconds(1));
		// Then be awake for 60 ms
		// This value is capped at 256*256 us (8-bit field, in units of 256 us)
		// = 65.536ms
		twtHeader.SetNominalMinimumWakeDuration(MilliSeconds(60));

		uint64_t sleepTime_us = 5000000;
		uint8_t exponent = 0;
		// This may lose some precision in some cases
		while (sleepTime_us > std::numeric_limits<uint16_t>::max()) {
			sleepTime_us /= 2;
			exponent += 1;
		}
		NS_ASSERT(sleepTime_us == (sleepTime_us & 0xFFFF));
		uint16_t mantissa = sleepTime_us;
		twtHeader.SetWakeIntervalExponent(exponent);
		twtHeader.SetWakeIntervalMantissa(mantissa);

		twtHeader.SetGroupId(0);
		twtHeader.SetZeroOffsetOfGroupPresent(false);
		twtHeader.SetUnit(0);
		twtHeader.SetOffset(0);
		twtHeader.SetImplicit(1);
		twtHeader.SetAnnouncedFlowType(false);
		twtHeader.SetFlowIdentifier(0);

		// 22 = unprotected S1G - Should be the only one available I think?
		// May be some protected S1G TWT implementations as well I guess, unsupported for now. Unsure what standard says
		// about that.
		twtHeader.SetCategory(TWT_CATEGORY_UNPROTECTED_S1G);
		// 6 = TWT Setup, 7 = TWT teardown, 11 = TWT information
		twtHeader.SetAction(TWT_ACTION_FRAMETYPE_SETUP);
		// We're the requesting station, so set request flag.
		twtHeader.SetSetupCommand(TWT_SETUPCOMMAND_REQUEST);
		// I believe this is only used to identify requests between two same stations, in case of multiple agreements
		// So any value will do.
		twtHeader.SetDialogToken(1u);
		// Header handles the computation for length
		twtHeader.SetLength();

		packet->AddHeader(twtHeader);
		WifiMacHeader macHeader;
		macHeader.SetAddr1(destination);
		macHeader.SetAddr2(GetAddress());
		macHeader.SetAddr3(destination);
		macHeader.SetAddr4(GetAddress());
		macHeader.SetTwtFrame();
		packet->AddPacketTag(TwtPacketTag::Create());
		// Mac header doesn't need to be added manually.
		// m_dca also adds FCS at end of packet +
		// does some timing/fragmentation related work. We simply set parameters and let m_dca take care
		// of the rest.

		this->QueueWithTwt(packet, macHeader);

		NS_LOG_UNCOND("Scheduling packet send to TWT resp station now!");
		Simulator::Schedule(Time::FromInteger(1u, Time::Unit::S), &RegularWifiMac::SendTwtTestTraffic, this, destination);

		NS_LOG_UNCOND("SendTwtSetupFrame STOP.");
	}
	void RegularWifiMac::SendTwtTestTraffic(Mac48Address to)
	{
		static std::set<uint64_t> mytags;
		NS_LOG_UNCOND("Send twt test traffic fired!");

		auto packet = Create<Packet>(150);
		WifiMacHeader hdr;
		hdr.SetAddr1(to);
		hdr.SetAddr3(to);
		hdr.SetAddr2(GetAddress());
		hdr.SetAddr4(GetAddress());
		hdr.SetType(WIFI_MAC_QOSDATA_NULL);
		hdr.SetQosTid(1);
		TwtPacketTag mytag = TwtPacketTag::Create();
		packet->AddPacketTag(mytag);
		mytags.insert(mytag.id);
		const char *sep = "";
		const auto sepActual = ", ";
		std::stringstream ss;
		ss << "[";
		for (auto it = mytags.begin(); it != mytags.end(); ++it) {
			ss << sep << *it;
			sep = sepActual;
		}
		ss << "]";
		NS_LOG_UNCOND("SENT = " << ss.str());
		this->QueueWithTwt(packet, hdr);

		Simulator::Schedule(Seconds(2), &RegularWifiMac::SendTwtTestTraffic, this, to);
	}

	void RegularWifiMac::QueueWithTwt(Ptr<Packet> packet, const WifiMacHeader &header)
	{
		NS_LOG_UNCOND("Sending packet with twt functionality");
		// Check for TWT agreements with the destination
		auto dest = header.GetAddr1();
		auto agreements = GetTwtAgreements(dest);
		if (agreements.empty()) {
			// If none found, send without TWT
			NS_LOG_UNCOND("No TWT agreements active for this mac address - sending immediately.");
			m_dca->Queue(packet, header);
			return;
		}
		bool anyActive = false;
		// Check if we have any active TWT service periods.
		for (auto agreement : agreements) {
			if (agreement->IsActive()) {
				anyActive = true;
				break;
			}
		}
		if (anyActive) {
			NS_LOG_UNCOND("TWT agreement found, currently in active SP. Sending immediately.");
			// We're allowed to send packets currently
			m_dca->Queue(packet, header);
		} else {
			NS_LOG_UNCOND("TWT agreement found, not in any active SP. Queueing the packet.");
			// Else queue them until we get another TWT wake-up
			auto loc = m_twtPacketQueue.find(dest);
			if (loc == m_twtPacketQueue.end()) {
				auto result = m_twtPacketQueue.emplace(dest, std::queue<PacketData>{});
				loc = result.first;
			}
			loc->second.push({packet, header});
			// The queue will be emptied when a TWT agreement's wake-up time happens or when it is torn down
		}
	}

	void RegularWifiMac::SendTwtTeardownFrame(const Mac48Address &destination, uint8_t flowId)
	{
		NS_ASSERT_MSG(this->GetConfiguredStandard() == WifiPhyStandard::WIFI_PHY_STANDARD_80211ah, "TWT only implemented for 802.11ah");
		NS_LOG_UNCOND("Time @ twt teardown frame entry: " << ns3::Simulator::Now().GetMilliSeconds());
		TWTAgreementKey key{destination, flowId};
		auto location = m_twtAgreements.find(key);
		if (location == m_twtAgreements.end()) {
			NS_ASSERT_MSG(false, "Expected teardown frame to be for a TWT agreement we know of locally.");
		}
		Ptr<Packet> packet = Create<Packet>();
		TWTHeader twtHeader;
		twtHeader.SetAction(TWT_ACTION_FRAMETYPE_TEARDOWN);
		twtHeader.SetCategory(TWT_CATEGORY_UNPROTECTED_S1G);
		twtHeader.SetFlowIdentifier(flowId);
		packet->AddHeader(twtHeader);

		WifiMacHeader macHeader;
		macHeader.SetAddr1(destination);
		macHeader.SetAddr2(GetAddress());
		macHeader.SetAddr3(destination);
		macHeader.SetAddr4(GetAddress());
		macHeader.SetType(WIFI_MAC_EXTENSION_TWT_FRAME);
		packet->AddPacketTag(TwtPacketTag::Create());
		NS_LOG_UNCOND("PRESEND PACKET SIZE: " << packet->GetSize());
		this->QueueWithTwt(packet, macHeader);
		NS_LOG_UNCOND("Successfully queued the TWT teardown frame.");
		// TODO: For now we just assume send will succeed, but we need to verify that to be standard compliant
		// Standard states either station can terminate a agreement by successfully transmitting OR receiving a
		// TWT teardown frame. This means we do not need to wait for an accept message or similar, we simply
		// Delete upon local confirmation of successful transmission.
		// For now, we assume that we have that confirmation here after successful queueing, but that is clearly incorrect.
		// Correct implementation is most likely using a send-call back function that will check the packet sent type,
		// And in case of the TWT teardown message being transmitted, then calling this teardown function.
		HandleLocalTwtTeardown(destination, flowId);
	}
	void RegularWifiMac::SendQueuedPackets(const Mac48Address &to)
	{
		auto loc = m_twtPacketQueue.find(to);
		if (loc == m_twtPacketQueue.end()) {
			NS_LOG_UNCOND("Entered queued packet sent, but no packets queued.");
			// This means we have no queued packets for this mac address, so this function is a no-op
			return;
		}
		auto &packets = loc->second;
		NS_LOG_UNCOND("Sending queued packets to " << to << ", total = " << packets.size());
		while (!packets.empty()) {
			auto packetData{packets.front()};
			m_dca->Queue(packetData.packet, packetData.header);
			packets.pop();
		}
		NS_LOG_UNCOND("Exit queued packet send.");
		// Remove the entry from the queue, because we've just handled it.
		m_twtPacketQueue.erase(loc);
	}

	void RegularWifiMac::HandleLocalTwtTeardown(const Mac48Address &destination, uint8_t flowId)
	{
		NS_LOG_UNCOND("Removing TWT agreement with destination " << destination << " and flow id " << static_cast<unsigned>(flowId));
		auto loc = m_twtAgreements.find({destination, flowId});
		NS_ASSERT(loc != m_twtAgreements.end());
		auto &agreement = loc->second;
		Simulator::Cancel(agreement.nextEvent);
		m_twtAgreements.erase(loc);
		if (GetTwtAgreements(destination).empty()) {
			// Empty the packet queue if we deleted the last TWT session between us and destination
			// Since we're no longer time constrained for our sending
			this->SendQueuedPackets(destination);
		}
		// Call the twtchangedcallback if it's set
		if (m_twtChangedCallback) {
			m_twtChangedCallback();
		}
	}

	void RegularWifiMac::HandleTwtConfirmationFrame(Ptr<Packet> packet, const WifiMacHeader *hdr, TWTHeader *twtHeader)
	{
		NS_ASSERT_MSG(hdr->IsTwtFrame(), "Can't handle TWT confirmation from a non-twt frame");
		NS_ASSERT_MSG(twtHeader->IsTwtConfirmationFrame(), "Can't handle TWT confirmation from non-confirmation frame.");
		TWTAgreementKey key{hdr->GetAddr2(), twtHeader->GetFlowIdentifier()};
		auto agreementLocation = m_twtAgreements.find(key);
		if (agreementLocation != m_twtAgreements.end()) {
			NS_ASSERT_MSG(false, "Attempted to insert second agreement with same flow id.");
		}
		CreateLocalTwtAgreement(*twtHeader, hdr->GetAddr2());
	}
	void RegularWifiMac::HandleTwtTeardownFrame(Ptr<Packet> packet, const WifiMacHeader *hdr, TWTHeader *twtHeader)
	{
		static TWTAgreementKey previous{Mac48Address::GetBroadcast(), static_cast<uint8_t>(-1)};
		auto remote = hdr->GetAddr2();
		auto flowId = twtHeader->GetFlowIdentifier();
		TWTAgreementKey key{remote, flowId};
		if (previous == key) {
			NS_LOG_UNCOND("Encountered presumed duplicate twt teardown packet, ignoring...");
			previous = key;
			return;
		}
		previous = key;
		auto location = m_twtAgreements.find(key);
		if (location == m_twtAgreements.end()) {
			NS_ASSERT_MSG(false, "Expected teardown only of agreement we know of locally.");
		}
		// This removes the local agreement + handles callback if needed
		HandleLocalTwtTeardown(remote, flowId);
	}
	void RegularWifiMac::HandleTwtSetupFrame(Ptr<Packet> packet, const WifiMacHeader *hdr, TWTHeader *twtHeader)
	{
		NS_ASSERT(m_twtParameterAcceptanceFunction != nullptr);
		bool acceptable = m_twtParameterAcceptanceFunction(*twtHeader);
		if (acceptable) {
			SendTwtConfirmationFrame(packet, hdr, twtHeader);
			NS_LOG_UNCOND("Sending TWT confirmation frame, creating local agreement.");
			CreateLocalTwtAgreement(*twtHeader, hdr->GetAddr2());
		} else {
			SendTwtAlternativeFrame(packet, hdr, twtHeader);
		}
	}
	void RegularWifiMac::CreateLocalTwtAgreement(TWTHeader &header, Mac48Address from)
	{
		TWTAgreementKey key{from, header.GetFlowIdentifier()};
		TWTAgreementData data{header};
		if (header.IsRequest()) {
			// This means we are creating this after transmitting the confirmation frame,
			// So we are the responding station
			data.myRole = TWT_RESPONDING_STA;
		} else {
			data.myRole = TWT_REQUESTING_STA;
		}
		// Creating the scheduled wake-up before emplacing for simplicity's sake
		auto entry = m_twtAgreements.emplace(key, data);
		if (!entry.second) {
			NS_FATAL_ERROR("Emplacing local TWT agreement failed.");
		}
		// Callback if configured. This lets derived classes handle changes in TWT agreements.
		// e.g. configure looser beacon timings, since TWT config can let us miss some.
		if (m_twtChangedCallback) {
			m_twtChangedCallback();
		}
		NS_LOG_UNCOND("Created local TWT agreement. Map size: " << m_twtAgreements.size());
		auto wakeupDelay = header.GetTargetWakeTime() - Simulator::Now();
		if (!(data.myRole == TWT_RESPONDING_STA && header.IsAnnouncedFlowType())) {
			// In case of either a) an unnanounced TWT agreement
			//                or b) us being the requesting station in an announced twt agreement
			// We schedule a time-based wake up.
			data.nextEvent =
					Simulator::Schedule(wakeupDelay, &RegularWifiMac::HandleStartOfWakePeriod, this, entry.first->first, entry.first->second);
			// In the remaining case, (announced TWT agreement where we are the responding station),
			// We will wake up when we receive a PS-Poll frame from the requesting station.
		}
	}
	void RegularWifiMac::HandleTwtInformationFrame(Ptr<Packet> packet, const WifiMacHeader *hdr, TWTHeader *twtHeader)
	{
		NS_FATAL_ERROR("Currently we provide no handling of TWT information frames.");
	}
	std::list<const TWTHeader *> RegularWifiMac::GetTwtAgreements(const Mac48Address *addressPtr) const
	{
		std::list<const TWTHeader *> ret;
		for (const auto &agreement : m_twtAgreements) {
			// If nullptr, match all, else only those it compares equal to
			if (addressPtr == nullptr || agreement.first.macAddress == *addressPtr) {
				ret.push_back(&agreement.second.header);
			}
		}
		return ret;
	}

	void RegularWifiMac::SendTwtConfirmationFrame(Ptr<Packet> packet, const WifiMacHeader *sourceHeader, TWTHeader *sourceTwtHeader)
	{
		// We've locally created the TWT agreement. Now, we must send a confirm message to the requesting STA.
		TWTHeader twtconfirm = TWTHeader::CreateConfirmationHeader(*sourceTwtHeader);
		NS_LOG_UNCOND(GetAddress() << " sending TWT confirm to " << sourceHeader->GetAddr2());
		SendTwtFrame(twtconfirm, sourceHeader->GetAddr2());
	}

	void RegularWifiMac::SendTwtAlternativeFrame(Ptr<Packet> packet, const WifiMacHeader *sourceHeader, TWTHeader *sourceTwtHeader)
	{
		NS_ASSERT(m_twtAlternativeConfigurationFunction != nullptr);
		TWTHeader newTwtHeader = m_twtAlternativeConfigurationFunction(*sourceTwtHeader);
		SendTwtFrame(newTwtHeader, sourceHeader->GetAddr2());
	}

	void RegularWifiMac::SendTwtFrame(TWTHeader &header, Mac48Address dest)
	{
		Ptr<Packet> packet = Create<Packet>();
		packet->AddHeader(header);
		WifiMacHeader macHeader;
		macHeader.SetAddr1(dest);
		macHeader.SetAddr2(GetAddress());
		macHeader.SetAddr3(dest);
		macHeader.SetAddr4(GetAddress());
		macHeader.SetType(WIFI_MAC_EXTENSION_TWT_FRAME);
		packet->AddPacketTag(TwtPacketTag::Create());

		this->QueueWithTwt(packet, macHeader);
	}

	void RegularWifiMac::HandleNextTwtInfoField(TWTAgreementKey &key, TWTAgreementData &data, std::pair<uint8_t, uint64_t> nextTwt)
	{
		data.periodicTwtOverridden = true;
		// Value in ehader is 45 bytes.
		// Must combine with local timing function to reconstruct full value.
		// So we keep the top (64-45=19) bytes
		auto localTime = static_cast<uint64_t>(Simulator::Now().ToInteger(Time::Unit::US));
		NS_ASSERT(nextTwt.second == (nextTwt.second & 0x00001FFFFFFFFFFF));
		localTime = (localTime & 0xFFFFE00000000000) | nextTwt.second;

		data.header.SetTargetWakeTime(Time::FromInteger(localTime, Time::Unit::US));

		if (data.waitingForNextTwt) {
			this->HandleEndOfWakePeriod(key, data);
		}

		if (m_twtChangedCallback) {
			m_twtChangedCallback();
		}
	}

	void RegularWifiMac::SetTWTCallbackFunction(std::function<void()> fn)
	{
		m_twtChangedCallback = fn;
	}
	std::function<void()> RegularWifiMac::GetTWTCallbackFunction() const
	{
		return m_twtChangedCallback;
	}
	void RegularWifiMac::UnsetTWTCallbackFunction()
	{
		m_twtChangedCallback = nullptr;
	}
	void RegularWifiMac::SetTWTAcceptanceFunction(TWTParamaterAcceptanceFunctionType fn)
	{
		m_twtParameterAcceptanceFunction = fn;
	}
	TWTParamaterAcceptanceFunctionType RegularWifiMac::GetTWTAcceptanceFunction() const
	{
		return m_twtParameterAcceptanceFunction;
	}
	void RegularWifiMac::SetTWTAlternativeConfigurationFunction(TWTAlternativeConfigurationFunctionType fn)
	{
		m_twtAlternativeConfigurationFunction = fn;
	}
	TWTAlternativeConfigurationFunctionType RegularWifiMac::GetTWTAlternativeConfigurationFunction() const
	{
		return m_twtAlternativeConfigurationFunction;
	}
	Time GetAdjustedMinimumWakeDuration(TWTHeader &hdr, Time now)
	{
		auto expectedStartTime = hdr.GetTargetWakeTime();
		auto expectedDuration = hdr.GetNominalMinimumWakeDuration();
		// Adjusted duration is nominal minimum - how long we took to start
		// So if we start e.g. 10 us late, the actual minimum duration = nominal - 10us
		NS_ASSERT_MSG(now >= expectedStartTime, "TWT implementation does not support starting wake early.");
		auto offset = now - expectedStartTime;
		// In TWT header, nominal minimum wake duration is in units of 256 US.
		// Outside it, though, the representation doesn't really matter as long as the functionality is correct.
		// So we simply store it as an ns3::Time object.
		return expectedDuration - offset;
	}
	void RegularWifiMac::HandleTwtTimeUpdateMessage(TWTAgreementKey &key, TWTAgreementData &data)
	{
		// We're the requesting STA, sending a poll to the responding sta.
		// Any message that will get us a next-twt info field will do. Empty information frame, for example.
		TWTHeader twtHdr;
		twtHdr.SetTwtInformationFrame();
		twtHdr.SetFlowIdentifier(key.flowIdentifier);
		twtHdr.SetNextTwtInfoSubfieldSize(0);
		// Sending of an info frame will solicit an acknowledgment
		// This acknowledgment must include the next twt info field and will allow us to set the required information
		// We mark this in the agreement data, this flag will be used to correctly handle the endofwakeperiod
		// when the nexttwt info field arrives
		data.waitingForNextTwt = true;

		auto packet = Create<Packet>();
		packet->AddHeader(twtHdr);

		WifiMacHeader hdr;
		hdr.SetAddr1(key.macAddress);
		hdr.SetAddr3(key.macAddress);
		hdr.SetAddr2(GetAddress());
		hdr.SetAddr4(GetAddress());
		hdr.SetTwtFrame();

		packet->AddPacketTag(TwtPacketTag::Create());
		// This packet send occurs outside the bounds of standard TWT
		// So we must queue this packet like this because the TWT method would queue the packet,
		// Rather than immediately sending as intended
		m_dca->Queue(packet, hdr);
	}
	bool RegularWifiMac::HandleExplicitTwtTimeUpdateIfNeeded(TWTAgreementKey &key, TWTAgreementData &data)
	{
		if (!data.periodicTwtOverridden && data.myRole == TWT_REQUESTING_STA) {
			// If we're the requesting STA and no next time has been established
			// We need to explicitly poll the responding STA to do so here.
			HandleTwtTimeUpdateMessage(key, data);
			return true;
		} else {
			return false;
		}
	}
	void RegularWifiMac::HandleEndOfWakePeriod(TWTAgreementKey &agreementKey, TWTAgreementData &agreementData)
	{
		NS_LOG_UNCOND("Handling end of wake period! (" << static_cast<uint32_t>(agreementData.myRole) << ")");
		NS_LOG_UNCOND(Simulator::Now());
		bool anyChanges = false;
		if (agreementData.header.IsImplicit()) {
			if (!agreementData.periodicTwtOverridden) {
				agreementData.header.IncrementTargetWakeTime();
				anyChanges = true;
			} // else nothing. This happens when new time is set using a next-twt info field.
		} else {
			anyChanges = HandleExplicitTwtTimeUpdateIfNeeded(agreementKey, agreementData);
		}
		Time delay = agreementData.header.GetTargetWakeTime() - Simulator::Now();
		NS_LOG_UNCOND("Scheduled start of wake period " << delay << " from now.");
		agreementData.nextEvent = Simulator::Schedule(delay, &RegularWifiMac::HandleStartOfWakePeriod, this, agreementKey, agreementData);
		if (m_twtEndOfWakePeriodCallback) {
			m_twtEndOfWakePeriodCallback(agreementKey, agreementData);
		}
		if (anyChanges && m_twtChangedCallback) {
			m_twtChangedCallback();
		}
	}

	void RegularWifiMac::HandleStartOfWakePeriod(TWTAgreementKey &agreementKey, TWTAgreementData &agreementData)
	{
		NS_LOG_UNCOND("Handling start of wake period! (" << static_cast<uint32_t>(agreementData.myRole) << ")");
		NS_LOG_UNCOND(Simulator::Now());
		// At start of wake period, we must handle announcement of wake up in case of an announced twt
		if (agreementData.header.IsAnnouncedFlowType() && agreementData.myRole == TWT_REQUESTING_STA) {
			// Here we are a Requesting STA in an announced flow TWT agreement, so we must send the wake announcement
			// This function sends an APSD trigger frame, but may be overridden to be replaced by a PS-Poll frame.
			this->SendAnnouncedTwtWakeupMessage(agreementKey.macAddress);
		} // In case of unannounced TWT, or responding STA, no actions have to be taken.

		// Compute the adjust minimum wake time.
		agreementData.adjustedMinWake = GetAdjustedMinimumWakeDuration(agreementData.header, Simulator::Now());
		// Schedule the end of the wake period.
		NS_LOG_UNCOND("Scheduled end of wake period for " << agreementData.adjustedMinWake << " from now.");
		agreementData.nextEvent =
				Simulator::Schedule(agreementData.adjustedMinWake, &RegularWifiMac::HandleEndOfWakePeriod, this, agreementKey, agreementData);
		// Send any packets we may have had buffered for the destination address
		this->SendQueuedPackets(agreementKey.macAddress);
		// Do callback fn if necessary
		if (m_twtStartOfWakePeriodCallback) {
			m_twtStartOfWakePeriodCallback(agreementKey, agreementData);
		}
	}

} // namespace ns3
