// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// ARA as an arm of swarm-routing/ns3-tools' comparison harness (routing-bench,
// AntHocNet ADR-0027): compiled only when routing-bench is installed beside
// this module (CMakeLists.txt). routing-compare finds this file and calls
// RegisterArms_ara() at start-up; the arm's name is `ara`, the token AntHocNet's
// `adaptive` arm used, so v2.5.0's command lines run the new module unchanged.
// The two never share a build: a duplicate name aborts.
#include "ns3/ara-helper.h"
#include "ns3/routing-bench-arm.h"

namespace ns3 {
namespace ara {
namespace {

class AraArm : public bench::Arm {
public:
    std::string Name() const override { return "ara"; }
    void InstallRouting(InternetStackHelper& internet, bench::RunContext&) override {
        internet.SetRoutingHelper(m_helper);
    }
    int64_t AssignStreams(bench::RunContext& run, int64_t stream) override {
        return m_helper.AssignStreams(run.nodes, stream);
    }
    // InstallRouting only names the helper; the monitors AraHelper::Install
    // would add (a WifiLinkMonitor per wifi node) are added here instead.
    void AfterStack(bench::RunContext& run) override { SwarmMonitorHelper().Install(run.nodes); }

private:
    AraHelper m_helper;
};

Ptr<bench::Arm> MakeAraArm() {
    return Create<AraArm>();
}

}  // namespace
}  // namespace ara

namespace bench {

void RegisterArms_ara();

void RegisterArms_ara() {
    RegisterArm("ara", "ARA, standalone on swarm-core (Gunes, Sorges & Bouazizi 2002)",
                &ara::MakeAraArm);
}

}  // namespace bench
}  // namespace ns3
