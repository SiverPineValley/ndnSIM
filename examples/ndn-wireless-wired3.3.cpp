#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/netanim-module.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ctime>

NS_LOG_COMPONENT_DEFINE ("ndn-wireless-wired");

using namespace std;

namespace ns3
{

/**
 * To run scenario and see what is happening, use the following command:
 *
 *     ./waf --run=ndn-wireless-wired
 */
  

  // CallBack Funcitons

    ns3::DelayJitterEstimation es;
    uint32_t dropcnt = 0;
    uint32_t tdropcnt = 0;

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
        std::cout << "MAC Delay" << "\t" << es.GetLastDelay().GetSeconds() << " second" << 
        "\t" << "Simulation" << "\t" << Simulator::Now().GetSeconds() << "\n";
    }

    void
    MacTxDropTrace(std::string s, Ptr<const Packet> p)
    {
        // std::cout << "MAC Tx Drop" << "\t" << Simulator::Now().GetSeconds() << " second" << "\n";
        tdropcnt++;
    }

    void
    MacRxDropTrace(std::string s, Ptr<const Packet> p)
    {
        // std::cout << "MAC Rx Drop" << "\t" << Simulator::Now().GetSeconds() << " second"  << "\t" << "UID" << "\t" << p->GetUid() << "\n";
        dropcnt++;
    }

    void
    PhyTxDropTrace(std::string s, Ptr<const Packet> p)
    {
     std::cout << "Phy Tx Drop" << "\t" << Simulator::Now().GetSeconds() << " second" << "\n";
    }

    void
    PhyRxDropTrace(std::string s, Ptr<const Packet> p)
    {
     std::cout << "Phy Rx Drop" << "\t" << Simulator::Now().GetSeconds() << " second" << "\t" << "UID" << "\t" << p->GetUid() << "\n";
    }

    void
    CheckPIT(Ptr<Node> n)
    {
        cout << "Node\t" << n->GetId() << "\t" << "Simulation" << "\t" << Simulator::Now().GetSeconds() << "\n";
        const nfd::Pit& pit = n->GetObject<ndn::L3Protocol>()->getForwarder()->getPit();
        cout << "Size\t" << pit.size() << "\n";
        // for(nfd::Pit::const_iterator entry = pit.begin(); entry != pit.end(); entry++)
        // {
        //  cout << entry->getName() << "\n";
        // }
        cout << "==============================\n";

    }

    void
    CheckMacDelay(void)
    {
        std::cout << "MAC Delay" << "\t" << es.GetLastDelay().GetSeconds() << " second" << 
        "\t" << "Simulation" << "\t" << Simulator::Now().GetSeconds() << "\n";
    }

    // void
    // WillBeCalledWhenInterestIsReceived( std::shared_ptr<const ndn::Interest> i, Ptr<ndn::App> a, std::shared_ptr<ndn::Face> f )
    // {
    //     cout << "Interest is Received at " << f->getId() << "\t" << i->getName().toUri();
    // }

    // void
    // WillBeCalledWhenDataIsReceived( shared_ptr<const ndn::Data> d , Ptr<ndn::App> a, shared_ptr<ndn::Face> f )
    // {  
    //     cout << "Data is Received at " << f->getId() << "\t" << d->getName().toUri() << "\t" << d->getContent().value();
    // }

    // void
    // ReceivedNack( shared_ptr< const lp::Nack >, Ptr< ndn::App >, shared_ptr< nfd::Face > )
    // {
    //     cout << "Data is Received at " << f->getId() << "\t" << d->getName().toUri() << "\t" << d->getContent().value();
    // }

    void
    printMacDrop(void)
    {
        uint32_t sumofdrop = dropcnt + tdropcnt;
        std::cout << "Total Mac Drop: " << sumofdrop << "\n";
        std::cout << "RX: " << dropcnt << "\n";
        std::cout << "TX: " << tdropcnt << "\n";
    }

  int main (int argc, char *argv[])
  {
    std::string phyMode ("DsssRate1Mbps");
    std::string dataMode ("DsssRate11Mbps"); 
    double distance = 500;  // m
    uint32_t packetSize = 1000; // bytes
    uint32_t numPackets = 1;
    uint32_t consumerNum = 4;
    double interval = 1.0; // seconds
    bool verbose = false;

    CommandLine cmd;

    cmd.AddValue ("phyMode", "Wifi Phy mode", phyMode);
    cmd.AddValue ("distance", "distance (m)", distance);
    cmd.AddValue ("packetSize", "size of application packet sent", packetSize);
    cmd.AddValue ("numPackets", "number of packets generated", numPackets);
    cmd.AddValue ("interval", "interval (seconds) between packets", interval);
    cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
    // cmd.AddValue ("consumerNum", "number of Consumers", consumerNum);

    cmd.Parse (argc, argv);
    // Convert to time object
    Time interPacketInterval = Seconds (interval);

    // set Random Variables
    srand((unsigned int)time(0));
    RngSeedManager::SetSeed (rand());
    RngSeedManager::SetRun (rand());
    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable> ();

    // Reading file for topology setup
    AnnotatedTopologyReader topologyReader("", 1);
    topologyReader.SetFileName("src/ndnSIM/examples/topologies/topo-wireless-wired3.txt");
    topologyReader.Read();

    // Getting containers for the consumer/producer/wifi-ap
    Ptr<Node> consumers[4] = {Names::Find<Node>("p1"),
                                Names::Find<Node>("p2"),
                                Names::Find<Node>("p3"),
                                Names::Find<Node>("p4")};
                                // Names::Find<Node>("p5"),
                                // Names::Find<Node>("p6"),
                                // Names::Find<Node>("p7"),
                                // Names::Find<Node>("p8"),
                                // Names::Find<Node>("p9"),
                                // Names::Find<Node>("p10")};
    Ptr<Node> wifiApNode = Names::Find<Node>("ap");

    // disable fragmentation for frames below 2200 bytes
    Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue("2200"));
    // turn off RTS/CTS for frames below 2200 bytes
    Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
    // Fix non-unicast data rate to be the same as that of unicast
    Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue (dataMode));

    //////////////////////
    //////////////////////
    //////////////////////

    // Wifi Setting
    WifiHelper wifi;
    wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
    
    // Turn on all Wifi logging
    if(verbose)
    {
      wifi.EnableLogComponents ();
    }

    // Wifi Channel Setting
    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    // wifiChannel.AddPropagationLoss ("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(100));
    wifiChannel.AddPropagationLoss("ns3::ThreeLogDistancePropagationLossModel");
    wifiChannel.AddPropagationLoss("ns3::NakagamiPropagationLossModel");
    // The below FixedRssLossModel will cause the rss to be fixed regardless
    // of the distance between the two stations, and the transmit power
    /* wifiChannel.AddPropagationLoss ("ns3::FixedRssLossModel","Rss",DoubleValue(rss)); */

    // Wifi Physical Setting
    // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
    YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
    wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11);
    wifiPhy.Set("TxPowerStart", DoubleValue(5));
    wifiPhy.Set("TxPowerEnd", DoubleValue(5));
    wifiPhy.SetChannel(wifiChannel.Create());

    // Wifi Mac Setting
    // Add a non-QoS upper mac, and disable rate control
    NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default();
    wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                  "DataMode", StringValue (dataMode),
                                  "ControlMode", StringValue (phyMode));

    // Setup Producers
    wifiMac.SetType ("ns3::StaWifiMac","ActiveProbing", BooleanValue (false));

    NodeContainer producers;
    producers.Create (consumerNum);

    NetDeviceContainer staDevice = wifi.Install (wifiPhy, wifiMac, producers);
    NetDeviceContainer devices = staDevice;

    
    // Setup AP.
    wifiMac.SetType ("ns3::ApWifiMac");
    NetDeviceContainer apDevice = wifi.Install (wifiPhy, wifiMac, wifiApNode);
    devices.Add (apDevice);

    /* for (int i = 0; i < 6; i++) {
        NetDeviceContainer apDevice = wifi.Install (wifiPhy, wifiMac, wifiApNodes[i]);
        devices.Add (apDevice);
    } */
    
    // Set Positions.
    // for AP node
    MobilityHelper sessile;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0, -20.0, 0.0));
    sessile.SetPositionAllocator (positionAlloc);
    sessile.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    sessile.Install (wifiApNode);

    // For Consumer Nodes
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> initialAlloc = CreateObject<ListPositionAllocator>();
    initialAlloc->Add(Vector(0, 0., 0.));
    initialAlloc->Add(Vector(4, 0., 0.));
    initialAlloc->Add(Vector(2, 0., 0.));
    initialAlloc->Add(Vector(-2, 0., 0.));
    mobility.SetPositionAllocator(initialAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(producers);

    // 3. Install NDN stack on all nodes
    NS_LOG_INFO("Installing NDN stack");
    ndn::StackHelper ndnHelper;
    ndnHelper.SetOldContentStore("ns3::ndn::cs::Lru", "MaxSize", "1000");
    // ndnHelper.SetDefaultRoutes(true);
    ndnHelper.InstallAll();

    // Choosing forwarding strategy
    ndn::StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");

    // Installing global routing interface on all nodes
    ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
    ndnGlobalRoutingHelper.InstallAll();

    // 4. Set up applications
    NS_LOG_INFO("Installing Applications");
    
    std::string pprefix[10] = { "/root0", "/root1", "/root2", "/root3",
        "/root4", "/root5", "/root6", "/root7", "/root8", "/root9" };

    ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
    consumerHelper.SetAttribute("Frequency", DoubleValue(100.0));
    consumerHelper.SetAttribute("Randomize", StringValue("uniform"));
    consumerHelper.SetAttribute("MaxSeq", StringValue("12000"));

    // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    consumerHelper.SetPrefix(pprefix[1]);
    consumerHelper.Install(consumers[1]);

    // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    consumerHelper.SetPrefix(pprefix[2]);
    consumerHelper.Install(consumers[2]);

    // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    consumerHelper.SetPrefix(pprefix[3]);
    consumerHelper.Install(consumers[3]);

    // // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    // consumerHelper.SetPrefix(pprefix[4]);
    // consumerHelper.Install(consumers[4];

    // // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    // consumerHelper.SetPrefix(pprefix[5]);
    // consumerHelper.Install(consumers[5]);

    // // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    // consumerHelper.SetPrefix(pprefix[6]);
    // consumerHelper.Install(consumers[6]);

    // // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    // consumerHelper.SetPrefix(pprefix[7]);
    // consumerHelper.Install(consumers[7]);

    // // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    // consumerHelper.SetPrefix(pprefix[8]);
    // consumerHelper.Install(consumers[8]);

    // // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    // consumerHelper.SetPrefix(pprefix[9]);
    // consumerHelper.Install(consumers[9]);

    // request delay-Insensitive
    // consumerHelper.SetAttribute("StartTime", DoubleValue((rand()%1000+1)/1000.00));
    consumerHelper.SetPrefix(pprefix[0]);
    consumerHelper.SetAttribute("Frequency", DoubleValue(10.0));
    consumerHelper.SetAttribute("MaxSeq", StringValue("1200"));
    consumerHelper.Install(consumers[0]);

    // Register /root prefix with global routing controller and
    // install producer that will satisfy Interests in /root namespace
    ndn::AppHelper producerHelper("ns3::ndn::Producer");
    producerHelper.SetAttribute("PayloadSize", StringValue("1024"));

    for( int i = 0; i < 4; i++ )
    {
        ndnGlobalRoutingHelper.AddOrigins(pprefix[i], producers.Get(i));
        producerHelper.SetPrefix(pprefix[i]);
        producerHelper.Install(producers.Get(i));
    }

    // Calculate and install FIBs
    ndn::GlobalRoutingHelper::CalculateRoutes();

    // Tracing
    // wifiPhy.EnablePcap ("ndn-wireless-wired", devices);

    // Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTx", MakeCallback(&MacTxTrace));
    // Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRx", MakeCallback(&MacRxTrace));
    // Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTxDrop", MakeCallback(&MacTxDropTrace));
    // Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRxDrop", MakeCallback(&MacRxDropTrace));
    // Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxDrop", MakeCallback(&PhyTxDropTrace));
    // Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyRxDrop", MakeCallback(&PhyRxDropTrace));
    // Config::Connect("/NodeList/*/ApplicationList/*/$ns3::ndn::App/ReceivedInterests", MakeCallback(&WillBeCalledWhenInterestIsReceived));
    // Config::Connect("/NodeList/*/ApplicationList/*/$ns3::ndn::App/ReceivedDatas", MakeCallback(&WillBeCalledWhenDataIsReceived));
    // Config::Connect("/NodeList/*/ApplicationList/*/$ns3::ndn::App/ReceivedNacks", MakeCallback(&ReceivedNack));
    // ndn::L3RateTracer::InstallAll("ndn-wireless-wired-trace1.3.txt", Seconds(0.5));
    // L2RateTracer::InstallAll("ndn-wireless-wired-trace.txt", Seconds(0.5));
    // ndn::CsTracer::InstallAll("ndn-wireless-wired-cs-trace.txt", Seconds(1));
    // ndn::AppDelayTracer::InstallAll("ndn-wireless-wired-app-delays-trace3.3.txt");
    ndn::AppDelayTracer::Install(consumers[0],"ndn-wireless-wired-app-delays-trace3.3.txt");

    // wifiApNode
    // consumers.Get(10)
    // producers[0]
    // Simulator::Schedule(Seconds(0.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(5.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(10.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(15.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(20.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(25.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(30.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(35.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(40.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(45.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(50.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(55.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(60.0), CheckPIT, wifiApNode);
    // Simulator::Schedule(Seconds(120.0), printMacDrop);


    // Simulator::Schedule(Seconds(0.5), CheckMacDelay);
    // Simulator::Schedule(Seconds(5.0), CheckMacDelay);
    // Simulator::Schedule(Seconds(10.0), CheckMacDelay);
    // Simulator::Schedule(Seconds(15.0), CheckMacDelay);
    // Simulator::Schedule(Seconds(20.0), CheckMacDelay);
    // Simulator::Schedule(Seconds(25.0), CheckMacDelay);
    // Simulator::Schedule(Seconds(30.0), CheckMacDelay);

    Simulator::Stop (Seconds (120.0));

    Simulator::Run ();
    Simulator::Destroy ();

    return 0;
}   // main

}   // namespace ns3

int main(int argc, char* argv[])
{
  return ns3::main(argc, argv);
}