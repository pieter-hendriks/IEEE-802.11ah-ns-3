#include "twt-agreement.h"
#include "ns3/simulator.h"
namespace ns3
{
	bool operator==(const TWTAgreementKey &a, const TWTAgreementKey &b)
	{
		return a.macAddress == b.macAddress && a.flowIdentifier == b.flowIdentifier;
	}
	bool operator<(const TWTAgreementKey &a, const TWTAgreementKey &b)
	{
		if (a.macAddress == b.macAddress) {
			return a.flowIdentifier < b.flowIdentifier;
		}
		return a.macAddress < b.macAddress;
	}

	bool TWTAgreementData::IsActive() const
	{
		auto startTime = this->header.GetTargetWakeTime();
		Time endTime;
		if (adjustedMinWake != 0) {
			endTime = startTime + adjustedMinWake;
		} else {
			endTime = startTime + this->header.GetNominalMinimumWakeDuration();
		}
		auto now = Simulator::Now();
		return (now > startTime && now < endTime);
	}
} // namespace ns3