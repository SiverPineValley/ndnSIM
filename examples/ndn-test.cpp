#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"

#include "ns3/ndnSIM-module.h"
#include <iostream>

NS_LOG_COMPONENT_DEFINE ("ndn-test");

 /*
 * To run scenario and see what is happening, use the following command:
 *
 *     ./waf --run=ndn-test
 * 
 * With LOGGING: e.g.
 *
 *     NS_LOG=ndn.Consumer:ndn.Producer ./waf --run=ndn-test 2>&1 | tee log.txt
 */

namespace ns3 {

	double t1;
	double t2;
	double mac_delay;

	void
	MacTxTrace(std::string s, Ptr<const Packet> p)
	{
		t1 = Simulator::Now().GetSeconds();
	}

	void
	MacRxTrace(std::string s, Ptr<const Packet> p)
	{
		t2 = Simulator::Now().GetSeconds();
		mac_delay = t2 - t1;
		std::cout << "MAC Delay" << "\t" << mac_delay << " second" << "\n";
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

		void
	PhyRxDropTrace(std::string s, Ptr<const Packet> p)
	{
		std::cout << "Phy Rx Drop" << "\t" << Simulator::Now().GetSeconds() << " second" << "\t" << "UID" << "\t" << p->GetUid() << "\n";
	}


	int main(int argc, char* argv[]) {

		std::string phyMode ("DsssRate1Mbps");
		// AP's Range
		int range = 5;

	    // disable fragmentation, RTS/CTS for frames below 2200 bytes and fix non-unicast data rate
	    Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue("2200"));
	    Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("2200"));
	    Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue(phyMode));
      	// setting default parameters for PointToPoint links and channels
	  	// Config::SetDefault("ns3::QueueBase::MaxPackets", UintegerValue(100));

		CommandLine cmd;
		cmd.Parse(argc, argv);

		// Read Topology
		AnnotatedTopologyReader topologyReader("", 1);
		topologyReader.SetFileName("src/ndnSIM/examples/topologies/topo-test.txt");
		topologyReader.Read();
		
		// Getting containers for the consumer/producer
 		Ptr<Node> consumers[5] = {Names::Find<Node>("node-1"), Names::Find<Node>("node-2"),
                            Names::Find<Node>("node-3"), Names::Find<Node>("node-4"),
                            Names::Find<Node>("node-5")};
        Ptr<Node> wifiApNode = Names::Find<Node>("AP");
 		Ptr<Node> producer = Names::Find<Node>("root");
		
 		// Connecting nodes using two links

		// Set wifi
		WifiHelper wifi;
		wifi.SetStandard(WIFI_PHY_STANDARD_80211b);
		wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager","DataMode",StringValue("OfdmRate24Mbps"));
		YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
		
		////// ns-3 supports RadioTap and Prism tracing extensions for 802.11b
    	wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO);

		//// Delay - constant or random
		//// NakagamiPropagationLossModel accounts for the variations in signal strength due to multi path fading.
		YansWifiChannelHelper wifiChannel;

		wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
		wifiChannel.AddPropagationLoss ("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(range));
		wifiPhy.SetChannel (wifiChannel.Create ());
    	// wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
     //                              "DataMode", StringValue (phyMode),
     //                              "ControlMode", StringValue (phyMode));
		// Set SSid
    	Ssid ssid = Ssid ("wifi-default");

    	////// Add a non-QoS upper mac of STAs, and disable rate control
    	// Consumer's wifiMacHelper
	    NqosWifiMacHelper wifiMacHelper = NqosWifiMacHelper::Default ();
	    ////// Active associsation of STA to AP via probing.
	    wifiMacHelper.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid),
	                           "ActiveProbing", BooleanValue (true),
	                           "ProbeRequestTimeout", TimeValue(Seconds(0.25)));

	    ////// Creating devices
 	   	NetDeviceContainer devices;

	    ////// Setup AP.
	    // Producer's wifiMacHelper
	    NqosWifiMacHelper wifiAp = NqosWifiMacHelper::Default ();
	    wifiAp.SetType ("ns3::AdhocWifiMac", "Ssid", SsidValue (ssid));

	    NetDeviceContainer apDevice[6];
	    apDevice[0] = wifi.Install (wifiPhy, wifiAp, wifiApNode);
	    devices.Add(apDevice[0]);

		// Install NDN stack on all nodes
		NS_LOG_INFO("Installing NDN stack");
		ndn::StackHelper ndnHelper;
		ndnHelper.SetDefaultRoutes(true);
		ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", "1000");
		ndnHelper.InstallAll();

		// Choosing forwarding Strategy
		ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/best-route");

		// Installing global routing interface on all nodes
		ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
		ndnGlobalRoutingHelper.InstallAll();

	    // 4. Set up applications
 	   	NS_LOG_INFO("Installing Applications");
 	   	ndn::AppHelper consumerHelper0("ns3::ndn::ConsumerCbr");
 	   	ndn::AppHelper consumerHelper1("ns3::ndn::ConsumerCbr");
 	   	ndn::AppHelper consumerHelper2("ns3::ndn::ConsumerCbr");
 	   	ndn::AppHelper consumerHelper3("ns3::ndn::ConsumerCbr");
 	   	ndn::AppHelper consumerHelper4("ns3::ndn::ConsumerCbr");
 	   	// ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");

 	   	consumerHelper0.SetAttribute("Frequency", StringValue("10"));
 	   	consumerHelper1.SetAttribute("Frequency", StringValue("10"));
 	   	consumerHelper2.SetAttribute("Frequency", StringValue("10"));
 	   	consumerHelper3.SetAttribute("Frequency", StringValue("10"));
 	   	consumerHelper4.SetAttribute("Frequency", StringValue("10"));

 	   	consumerHelper0.SetAttribute("StartTime", StringValue("0"));
 	   	consumerHelper1.SetAttribute("StartTime", StringValue("0"));
 	   	consumerHelper2.SetAttribute("StartTime", StringValue("0"));
 	   	consumerHelper3.SetAttribute("StartTime", StringValue("0"));
 	   	consumerHelper4.SetAttribute("StartTime", StringValue("0"));

 	   	consumerHelper0.SetAttribute("StopTime", StringValue("10"));
 	   	consumerHelper1.SetAttribute("StopTime", StringValue("20"));
 	   	consumerHelper2.SetAttribute("StopTime", StringValue("30"));
 	   	consumerHelper3.SetAttribute("StopTime", StringValue("40"));
 	   	consumerHelper4.SetAttribute("StopTime", StringValue("10"));

 	   	consumerHelper0.SetPrefix("/root/" + Names::FindName(consumers[0]));
 	   	consumerHelper1.SetPrefix("/root/" + Names::FindName(consumers[1]));
 	   	consumerHelper2.SetPrefix("/root/" + Names::FindName(consumers[2]));
 	   	consumerHelper3.SetPrefix("/root/" + Names::FindName(consumers[3]));
 	   	consumerHelper4.SetPrefix("/root/" + Names::FindName(consumers[4]));

 		// Add Consumer Helpers
 		consumerHelper0.Install(consumers[0]);
 		consumerHelper1.Install(consumers[1]);
 		consumerHelper2.Install(consumers[2]);
 		consumerHelper3.Install(consumers[3]);
 		consumerHelper4.Install(consumers[4]);

 		// devices.Add(apDevice);

  		for (int i = 0; i < 5; i++) {
			// Install Wifi
  			apDevice[i+1] = wifi.Install(wifiPhy, wifiMacHelper, consumers[i]);
  			devices.Add(apDevice[i+1]);
  		}

  		// Add Producer Helpers
		ndn::AppHelper producerHelper("ns3::ndn::Producer");
		producerHelper.SetAttribute("PayloadSize", StringValue("1024"));

		// Register /root prefix with global routing controller and
		 // install producer that will satisfy Interests in /root namespace
		ndnGlobalRoutingHelper.AddOrigins("/root", producer);
		producerHelper.SetPrefix("/root");
		producerHelper.Install(producer);

		 // Calculate and install FIBs
		ndn::GlobalRoutingHelper::CalculateRoutes();
	    // Tracing
	    // wifiPhy.EnablePcap ("ndn-test", devices);
	    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTx", MakeCallback(&MacTxTrace));
		Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRx", MakeCallback(&MacRxTrace));
		// Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTxDrop", MakeCallback(&MacTxDropTrace));
		Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRxDrop", MakeCallback(&MacRxDropTrace));
		// Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxDrop", MakeCallback(&PhyTxDropTrace));
		Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyRxDrop", MakeCallback(&PhyRxDropTrace));
		// ndn::AppDelayTracer::InstallAll("ndn-test-trace.txt");
		ndn::L3RateTracer::InstallAll("ndn-test-trace.txt", Seconds(1.0));
	
		NS_LOG_INFO("Run Simulation.");
		Simulator::Stop(Seconds(20.0));
		Simulator::Run();
		Simulator::Destroy();

		return 0;

	}

} // namespace ns3

int main(int argc, char* argv[])
{
  return ns3::main(argc, argv);
}
