#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"

#include "ns3/ndnSIM-module.h"

using namespace std;
namespace ns3 {

	NS_LOG_COMPONENT_DEFINE("ndn-wireless-test");

//
// DISCLAIMER:  Note that this is an extremely simple example, containing just 2 wifi nodes
// communicating directly over AdHoc channel.
//

// Ptr<ndn::NetDeviceFace>
// MyNetDeviceFaceCallback (Ptr<Nodenode, Ptr<ndn::L3Protocolndn, Ptr<NetDevicedevice)
// {
//   // NS_LOG_DEBUG ("Create custom network device " << node->GetId ());
//   Ptr<ndn::NetDeviceFaceface = CreateObject<ndn::MyNetDeviceFace(node, device);
//   ndn->AddFace (face);
//   return face;
// }
// void ReceivePacket (Ptr<Socketsocket>)
// {
// 	while (socket->Recv ())
// 	{
// 		NS_LOG_UNCOND ("Received one packet!");
// 	}
// }

// static void GenerateTraffic (Ptr<Socketsocket>, uint32_t pktSize,uint32_t pktCount, Time pktInterval )
// {
// 	if (pktCount 0)
// 	{
// 		socket->Send (Create<Packet(pktSize));
// 		Simulator::Schedule (pktInterval, &GenerateTraffic,socket, pktSize,pktCount-1, pktInterval);
// 	}
// 	else
// 	{
// 		socket->Close ();
// 	}
// }

	ns3::DelayJitterEstimation es;

	void
	MacTxTrace(std::string s, Ptr<const Packet> p)
	{
		es.PrepareTx(p);
	}

	// 0.4 Second Mac Delay -> Very Congest Network State
	void
	MacRxTrace(std::string s, Ptr<const Packet> p)
	{
		es.RecordRx(p);
		// std::cout << "MAC Delay" << "\t" << es.GetLastDelay().GetSeconds() << " second" << 
		// "\t" << "Simulation" << "\t" << Simulator::Now().GetSeconds() << "\n";
	}

	// void
	// MacTxDropTrace(std::string s, Ptr<const Packet> p)
	// {
	// 	std::cout << "MAC Tx Drop" << "\t" << Simulator::Now().GetSeconds() << " second" << "\n";
	// }

	void
	MacRxDropTrace(std::string s, Ptr<const Packet> p)
	{
		std::cout << "MAC Rx Drop" << "\t" << Simulator::Now().GetSeconds() << " second"  << "\t" << "UID" << "\t" << p->GetUid() << "\n";
	}

	// void
	// PhyTxDropTrace(std::string s, Ptr<const Packet> p)
	// {
	// 	std::cout << "Phy Tx Drop" << "\t" << Simulator::Now().GetSeconds() << " second" << "\n";
	// }

	// void
	// PhyRxDropTrace(std::string s, Ptr<const Packet> p)
	// {
	// 	std::cout << "Phy Rx Drop" << "\t" << Simulator::Now().GetSeconds() << " second" << "\t" << "UID" << "\t" << p->GetUid() << "\n";
	// }

	void
	CheckPIT(Ptr<Node> n)
	{
		cout << "Node\t" << n->GetId() << "\t" << "Simulation" << "\t" << Simulator::Now().GetSeconds() << "\n";
		const nfd::Pit& pit = n->GetObject<ndn::L3Protocol>()->getForwarder()->getPit();
		cout << "Size\t" << pit.size() << "\n";
		// for(nfd::Pit::const_iterator entry = pit.begin(); entry != pit.end(); entry++)
		// {
		// 	cout << entry->getName() << "\n";
		// }
		cout << "==============================\n";

	}

	void
	CheckMacDelay(void)
	{
		std::cout << "MAC Delay" << "\t" << es.GetLastDelay().GetSeconds() << " second" << 
		"\t" << "Simulation" << "\t" << Simulator::Now().GetSeconds() << "\n";
	}


int
main(int argc, char* argv[])
{

	std::string phyMode ("DsssRate1Mbps");
	double distance = 500;  // m
	uint32_t packetSize = 1000; // bytes
	uint32_t numPackets = 1;
	// uint32_t numNodes = 25;  // by default, 5x5
	// uint32_t sinkNode = 0;
	// uint32_t sourceNode = 24;
	double interval = 1.0; // seconds
	bool verbose = false;

	CommandLine cmd;

	cmd.AddValue ("phyMode", "Wifi Phy mode", phyMode);
	cmd.AddValue ("distance", "distance (m)", distance);
	cmd.AddValue ("packetSize", "size of application packet sent", packetSize);
	cmd.AddValue ("numPackets", "number of packets generated", numPackets);
	cmd.AddValue ("interval", "interval (seconds) between packets", interval);
	cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
	// cmd.AddValue ("numNodes", "number of nodes", numNodes);
	// cmd.AddValue ("sinkNode", "Receiver node number", sinkNode);
	// cmd.AddValue ("sourceNode", "Sender node number", sourceNode);

	cmd.Parse (argc, argv);
	// Convert to time object
	Time interPacketInterval = Seconds (interval);
  
	// disable fragmentation
	Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue("2200"));
	Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("2200"));
	Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",StringValue(phyMode));

	//////////////////////
	//////////////////////
	//////////////////////

	// Wifi Setting
	WifiHelper wifi;
	// wifi.SetRemoteStationManager ("ns3::AarfWifiManager");
	wifi.SetStandard(WIFI_PHY_STANDARD_80211a);

	// Wifi Channel Setting
	YansWifiChannelHelper wifiChannel; // = YansWifiChannelHelper::Default ();
	wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");

	// wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
	wifiChannel.AddPropagationLoss("ns3::ThreeLogDistancePropagationLossModel");
	wifiChannel.AddPropagationLoss("ns3::NakagamiPropagationLossModel");

	// Wifi Physical Setting
	// ns-3 supports RadioTap and Prism tracing extensions for 802.11b
	YansWifiPhyHelper wifiPhyHelper = YansWifiPhyHelper::Default();
	wifiPhyHelper.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO); 
	wifiPhyHelper.SetChannel(wifiChannel.Create());
	wifiPhyHelper.Set("TxPowerStart", DoubleValue(5));
	wifiPhyHelper.Set("TxPowerEnd", DoubleValue(5));

	NqosWifiMacHelper wifiMacHelper = NqosWifiMacHelper::Default();
	wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
	wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",StringValue (phyMode),
                                "ControlMode",StringValue (phyMode));
	wifiMacHelper.SetType("ns3::AdhocWifiMac");
	// wifiMacHelper.SetType("ns3::ApWifiMac");

	MobilityHelper mobility;
	Ptr<ListPositionAllocator> initialAlloc = CreateObject<ListPositionAllocator>();
	initialAlloc->Add(Vector(0, 1., 0.));
	initialAlloc->Add(Vector(0, 2., 0.));
	initialAlloc->Add(Vector(0, 3., 0.));
	initialAlloc->Add(Vector(0, 4., 0.));
	initialAlloc->Add(Vector(0, 5., 0.));
	initialAlloc->Add(Vector(0, 6., 0.));
	initialAlloc->Add(Vector(0, 7., 0.));
	initialAlloc->Add(Vector(0, 8., 0.));
	initialAlloc->Add(Vector(0, 9., 0.));
	initialAlloc->Add(Vector(0, 10., 0.));

	initialAlloc->Add(Vector(1, 6., 0.)); // AP node

	initialAlloc->Add(Vector(2, 1., 0.));
	initialAlloc->Add(Vector(2, 2., 0.));
	initialAlloc->Add(Vector(2, 3., 0.));
	initialAlloc->Add(Vector(2, 4., 0.));
	initialAlloc->Add(Vector(2, 5., 0.));
	initialAlloc->Add(Vector(2, 6., 0.));
	initialAlloc->Add(Vector(2, 7., 0.));
	initialAlloc->Add(Vector(2, 8., 0.));
	initialAlloc->Add(Vector(2, 9., 0.));
	initialAlloc->Add(Vector(2, 10., 0.));
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.SetPositionAllocator(initialAlloc);

	NodeContainer nodes;
	nodes.Create(21);

	////////////////
	// 1. Install Wifi
	NetDeviceContainer wifiNetDevices = wifi.Install(wifiPhyHelper, wifiMacHelper, nodes);

	// 2. Install Mobility model
	mobility.Install(nodes);

	// 3. Install NDN stack
	NS_LOG_INFO("Installing NDN stack");
	ndn::StackHelper ndnHelper;
	// ndnHelper.AddNetDeviceFaceCreateCallback (WifiNetDevice::GetTypeId (), MakeCallback
	// (MyNetDeviceFaceCallback));
	ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", "1000");
	ndnHelper.SetDefaultRoutes(true);
	ndnHelper.Install(nodes);

	// Set BestRoute strategy
	ndn::StrategyChoiceHelper::Install(nodes, "/", "/localhost/nfd/strategy/best-route");
 
	// ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
	// ndnGlobalRoutingHelper.InstallAll();

	// 4. Set up applications
	NS_LOG_INFO("Installing Applications");

	// std::string prefix = "/prefix";
	std::string cprefix[10] = { "/data1/prefix", "/data2/prefix", "/data3/prefix", "/data4/prefix",
		"/data5/prefix", "/data6/prefix", "/data7/prefix", "/data8/prefix", "/data9/prefix", "/data10/prefix" };

	std::string pprefix[10] = { "/data1", "/data2", "/data3", "/data4",
		"/data5", "/data6", "/data7", "/data8", "/data9", "/data10" };
	
	ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
	consumerHelper.SetAttribute("Frequency", DoubleValue(10.0));
	consumerHelper.SetAttribute("StartTime", StringValue("0"));
	consumerHelper.Install(nodes.Get(10));

	consumerHelper.SetPrefix(cprefix[0]);
	consumerHelper.SetAttribute("StopTime", StringValue("10"));
	consumerHelper.Install(nodes.Get(11));

	consumerHelper.SetPrefix(cprefix[1]);
	consumerHelper.SetAttribute("StopTime", StringValue("10"));
	consumerHelper.Install(nodes.Get(12));

	consumerHelper.SetPrefix(cprefix[2]);
	consumerHelper.SetAttribute("StopTime", StringValue("10"));
	consumerHelper.Install(nodes.Get(13));

	consumerHelper.SetPrefix(cprefix[3]);
	consumerHelper.SetAttribute("StopTime", StringValue("20"));
	consumerHelper.Install(nodes.Get(14));

	consumerHelper.SetPrefix(cprefix[4]);
	consumerHelper.SetAttribute("StopTime", StringValue("20"));
	consumerHelper.Install(nodes.Get(15));

	consumerHelper.SetPrefix(cprefix[5]);
	consumerHelper.SetAttribute("StopTime", StringValue("20"));
	consumerHelper.Install(nodes.Get(16));

	consumerHelper.SetPrefix(cprefix[6]);
	consumerHelper.SetAttribute("StopTime", StringValue("30"));
	consumerHelper.Install(nodes.Get(17));

	consumerHelper.SetPrefix(cprefix[7]);
	consumerHelper.SetAttribute("StopTime", StringValue("30"));
	consumerHelper.Install(nodes.Get(18));

	consumerHelper.SetPrefix(cprefix[8]);
	consumerHelper.SetAttribute("StopTime", StringValue("30"));
	consumerHelper.Install(nodes.Get(19));

	consumerHelper.SetPrefix(cprefix[9]);
	consumerHelper.SetAttribute("StopTime", StringValue("30"));
	consumerHelper.Install(nodes.Get(20));

	/* for (int i = 11; i < 21; i++)
	{
		consumerHelper.SetPrefix(cprefix[i-11]);
		consumerHelper.Install(nodes.Get(i));
		consumerHelper.Install(nodes.Get()); 
	} */

	  // consumerHelper.SetPrefix("/test/prefix1");
	  // consumerHelper.SetAttribute("Frequency", DoubleValue(10.0));
	  // consumerHelper.Install(nodes.Get(1));
  

	ndn::AppHelper producerHelper("ns3::ndn::Producer");
	producerHelper.SetAttribute("PayloadSize", StringValue("1200"));
	for (int i = 0; i < 10; i++)
	{
		producerHelper.SetPrefix(pprefix[i]);
		producerHelper.Install(nodes.Get(i));
	}


	////////////////

	// ndnGlobalRoutingHelper.AddOrigins(prefix, nodes.Get(7));
	// ndnGlobalRoutingHelper.AddOrigins(prefix, nodes.Get(6));
	// ndnGlobalRoutingHelper.AddOrigins(prefix, nodes.Get(5));

	// Calculate and install FIBs
	// ndn::GlobalRoutingHelper::CalculateRoutes();

	// wifiPhyHelper.EnablePcap ("ndn-wireless-test",  wifiNetDevices);
	Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTx", MakeCallback(&MacTxTrace));
	Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRx", MakeCallback(&MacRxTrace));
	// Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTxDrop", MakeCallback(&MacTxDropTrace));
	Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRxDrop", MakeCallback(&MacRxDropTrace));
	// Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxDrop", MakeCallback(&PhyTxDropTrace));
	// Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyRxDrop", MakeCallback(&PhyRxDropTrace));
	// ndn::L3RateTracer::InstallAll("ndn-wireless-test-trace.txt", Seconds(0.5));
	L2RateTracer::InstallAll("ndn-wireless-test-trace.txt", Seconds(0.5));

	Simulator::Schedule(Seconds(0.0), CheckPIT, nodes.Get(10));
	Simulator::Schedule(Seconds(5.0), CheckPIT, nodes.Get(10));
	Simulator::Schedule(Seconds(10.0), CheckPIT, nodes.Get(10));
	Simulator::Schedule(Seconds(15.0), CheckPIT, nodes.Get(10));
	Simulator::Schedule(Seconds(20.0), CheckPIT, nodes.Get(10));
	Simulator::Schedule(Seconds(25.0), CheckPIT, nodes.Get(10));
	Simulator::Schedule(Seconds(30.0), CheckPIT, nodes.Get(10));

	// Simulator::Schedule(Seconds(0.5), CheckMacDelay);
	// Simulator::Schedule(Seconds(5.0), CheckMacDelay);
	// Simulator::Schedule(Seconds(10.0), CheckMacDelay);
	// Simulator::Schedule(Seconds(15.0), CheckMacDelay);
	// Simulator::Schedule(Seconds(20.0), CheckMacDelay);
	// Simulator::Schedule(Seconds(25.0), CheckMacDelay);
	// Simulator::Schedule(Seconds(30.0), CheckMacDelay);

	Simulator::Stop(Seconds(30.0));

	Simulator::Run();
	Simulator::Destroy();

	return 0;
} // main

} // namespace ns3

int
main(int argc, char* argv[])
{
	return ns3::main(argc, argv);
}