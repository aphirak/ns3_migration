/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2019 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
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
 * Authors: Lucas Pacheco <lucas.pacheco@itec.ufpa.br>
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/lte-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/csma-module.h"
#include <stdlib.h> /* srand, rand */
// #include "ns3/gtk-config-store.h"
#include <sys/stat.h> // file permissions
// Used for cell allocation
#include <math.h> // sin cos pow
#include <limits> // limit of the int type
#include <vector>
#include "matriz.h"

using namespace std;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Ns3Migration");
/*
Algoprithms to be supported:
    MSMF
    greedy
    nomigration
    predictive
    QoS
*/

string algorithm = "MSMF";

// function prototipes
void migrate(Ptr<Node>, Ptr<Node>, Ipv4Address, Ipv4Address);
int getCellId(int nodeId);

// pi
float PI = 3.14159265; // pi
// scenario variables
uint16_t numNodes = 20;
uint16_t numEnbs = 20;
uint16_t numEdgeNodes = numEnbs;

// mobility trace file
string mobilityTrace = "mobil/rw.ns_movements";

// simulation variables
Time simTime = Seconds(40);

// inicialize node containers as global objects
NodeContainer ueNodes;
NodeContainer edgeNodes;
NodeContainer enbNodes;

// ApplicationContariners
ApplicationContainer apps;

// port ranges for each application
static int migrationPort = 1000;
static int applicationPort = 3000;

// enable logs
bool verbose = false;

// perform migrations
bool doMigrate = true;

unsigned int handNumber = 0; // number of handovers executed

// The resources variable tells which server has one or
// more of the recources needed in this simulation
// the resources are:
// base OS (1), base OS + base application(2), or none of these (0)
vector<uint16_t> resources;
int initialResources = 5;
// units of processing used at the moment
vector<uint16_t> serverLoad;
vector<double> qosValues;

Time managerInterval = MilliSeconds(100);
// size of the sevices to be migrated in bytes
uint64_t migrationSize = 2000000000;

// type of cell position allocation
bool rowTopology = false;
bool randomCellAlloc = true;

// data rate
Time interPacketInterval = MilliSeconds(1);

// control matrix
matriz<int> cellUe;

// edge servers responsible for each node
matriz<int> edgeUe;

matriz<int> edgeMigrationChart;

// IP addres of all fog nodes
// the ith server has two addresses, the 7.0.0.X base to communicate with nodes
// and the 22.0.0.X base for migrations
matriz<Ipv4Address> edgeNodesAddresses;

// structure to store handover predictions
// [0] -> time of the handover
// [1] -> source cell
// [2] -> target cell
matriz<int> handoverPredictions;


// function prototypes
int getEdge(int nodeId);
double qosProbe();
void HandoverPrediction(int nodeId, int timeWindow);
int getCellId(int imsi);
void getDelay(Ptr<Node> ueProbeNode, Ptr<Node> edgeProbeNode, Ipv4Address edgeAddress, Ipv4Address ueAddress);
void requestApplication(Ptr<Node>, Ptr<Node>, Ipv4Address);


void HandoverPrediction(int nodeId, int timeWindow)
{

    // means no connection has been found
    // happens if it's called too early in the simulation
    if (getCellId(nodeId) == -1)
        return;

    // receive a nodeId, and a time window, and return if a handover is going to happen in this time window.
    ifstream mobilityFile(mobilityTrace);

    string nodeColumn;
    string fileLines;

    // coordinate variables
    double node_x, node_y, node_z, node_position_time;
    double shortestDistance = numeric_limits<int>::max();
    int closestCell = numeric_limits<int>::max();

    // tmp veriables to read file
    // node_position_time = time of the position
    string aux1, aux2, aux4, aux5;
    string cell_id;

    while (getline(mobilityFile, fileLines)) {
        if (fileLines.find("setdest") != string::npos) {

            stringstream ss(fileLines);
            // cout << ss.str();
            ss >> aux1 >> aux2 >> node_position_time >> aux4 >> aux5 >> node_x >> node_y >> node_z;

            nodeColumn = "\"$node_(" + to_string(nodeId) + ")";
            // cout << "nodeColumn" << nodeColumn << "\n";
            // cout << "aux: " << aux4 << "\n";
            // cin.get();

            // for (int time_offset = 0; time_offset < timeWindow; time_offset++)
            if (aux4 == nodeColumn && Simulator::Now().GetSeconds() + timeWindow == round(node_position_time)) {
                Vector uePos = Vector(node_x, node_y, node_z);

                // double distanceServingCell = CalculateDistance(uePos, enbNodes.Get(getCellId(nodeId))->GetObject<MobilityModel>()->GetPosition ());

                // calculate distance from node to each enb
                for (int i = 0; i < numEnbs; ++i) {
                    // get Ith enb  position
                    Vector enbPos = enbNodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
                    // get distance
                    double distanceUeEnb = CalculateDistance(uePos, enbPos);

                    // get closest enb
                    if (distanceUeEnb < shortestDistance) {
                        closestCell = i;
                        shortestDistance = distanceUeEnb;
                    }

                }

                // if closest enb != current, predict handover
                if (closestCell != getCellId(nodeId)){
                    cout << "Handover to happen at " << node_position_time << endl;
                    cout << "Node " << nodeId << " from cell " << getCellId(nodeId) << " to cell " << closestCell << endl;
                    handoverPredictions[nodeId][0] = node_position_time;
                    handoverPredictions[nodeId][1] = getCellId(nodeId);
                    handoverPredictions[nodeId][2] = closestCell;
                }
            }
        }
    }

    mobilityFile.close();
}

void NotifyConnectionEstablishedUe(string context,
    uint64_t imsi,
    uint16_t cellid,
    uint16_t rnti)
{
    NS_LOG_INFO(Simulator::Now().GetSeconds() << " " << context << " UE IMSI " << imsi << ": connected to CellId " << cellid << " with RNTI " << rnti << "\n");

    stringstream temp_cell_dir;
    stringstream ueId;
    temp_cell_dir << "./v2x_temp/" << cellid;
    ueId << temp_cell_dir.str() << "/" << rnti;
    if (mkdir(temp_cell_dir.str().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
    }
    ofstream outfile(ueId.str().c_str());
    outfile << imsi << endl;
    outfile.close();

    if (resources[cellid - 1] == 0) {
        int realEdge;
        // iterate untill an edge with available resources has been chosen
        do {
            realEdge = rand() % numEdgeNodes;
            // make while condition true to reiterate
            if (resources[realEdge] == 0)
                realEdge = cellid - 1;
        } while (realEdge == cellid - 1);

        cout << "Failed to allocate user" << imsi << " in edge " << cellid - 1 << "\n";
        cout << "allocating to random edge " << realEdge << endl;
        resources[realEdge]--;
        edgeUe[realEdge][imsi - 1] = 1;
    }
    else {
        cout << "User " << imsi << " connected to edge " << cellid - 1 << endl;
        edgeUe[cellid - 1][imsi - 1] = 1;
        resources[cellid - 1]--;
    }

    cellUe[cellid - 1][imsi - 1] = rnti;
}

void NotifyHandoverStartUe(string context,
    uint64_t imsi,
    uint16_t cellid,
    uint16_t rnti,
    uint16_t targetCellId)
{
    cout << Simulator::Now().GetSeconds() << " " << context << " UE IMSI " << imsi << ": previously connected to CellId " << cellid << " with RNTI " << rnti << ", doing handover to CellId " << targetCellId << "\n";

    cellUe[cellid - 1][imsi - 1] = 0;

    stringstream ueId;
    ueId << "./v2x_temp/" << cellid << "/" << rnti;
    remove(ueId.str().c_str());

    if (algorithm == "greedy") {
        // migration of handover start
        Simulator::Schedule(Simulator::Now(), &migrate, edgeNodes.Get(cellid - 1),
            edgeNodes.Get(targetCellId - 1), edgeNodesAddresses[cellid - 1][1], edgeNodesAddresses[targetCellId - 1][1]);
    }

    ++handNumber;
}

void NotifyHandoverEndOkUe(string context,
    uint64_t imsi,
    uint16_t cellid,
    uint16_t rnti)
{
    cout << Simulator::Now().GetSeconds() << " " << context << " UE IMSI " << imsi << ": successful handover to CellId " << cellid << " with RNTI " << rnti << "\n";

    stringstream target_cell_dir;
    stringstream newUeId;
    target_cell_dir << "./v2x_temp/" << cellid;
    newUeId << target_cell_dir.str() << "/" << rnti;
    if (mkdir(target_cell_dir.str().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
    }
    ofstream outfile(newUeId.str().c_str());
    outfile << imsi << endl;
    outfile.close();

    cellUe[cellid - 1][imsi - 1] = rnti;
}

void NotifyConnectionEstablishedEnb(string context,
    uint64_t imsi,
    uint16_t cellid,
    uint16_t rnti)
{
    cout << Simulator::Now().GetSeconds() << " " << context << " eNB CellId " << cellid << ": successful connection of UE with IMSI " << imsi << " RNTI " << rnti << "\n";
}

void NotifyHandoverStartEnb(string context,
    uint64_t imsi,
    uint16_t cellid,
    uint16_t rnti,
    uint16_t targetCellId)
{
    cout << Simulator::Now().GetSeconds() << " " << context << " eNB CellId " << cellid << ": start handover of UE with IMSI " << imsi << " RNTI " << rnti << " to CellId " << targetCellId << "\n";
}

void NotifyHandoverEndOkEnb(string context,
    uint64_t imsi,
    uint16_t cellid,
    uint16_t rnti)
{
    cout << Simulator::Now().GetSeconds() << " " << context << " eNB CellId " << cellid << ": completed handover of UE with IMSI " << imsi << " RNTI " << rnti << "\n";
}

Ptr<ListPositionAllocator> positionAllocator(Ptr<ListPositionAllocator> HpnPosition)
{

    cout << "allocationg cells positions\n";
    int x, y;
    int distance = 1000;
    ofstream outfile("v2x_temp/cellsList", ios::out | ios::trunc);

    if (randomCellAlloc) {
        cout << "random alloc\n";
        for (int i = 0; i < numEnbs; ++i) {
            x = rand() % 200;
            y = rand() % 200;
            HpnPosition->Add(Vector(x, y, 15));
            outfile << i + 1 << " " << x << " " << y << endl;
        }
        outfile.close();
        return HpnPosition;
    }
    else if (rowTopology) {
        cout << "row alloc\n";
        int x_start = 700;
        int y_start = 500;
        for (int i = 0; i < numEnbs; ++i)
            HpnPosition->Add(Vector(x_start + distance * i, y_start, 25));
        return HpnPosition;
    }
    else {
        cout << "hex alloc\n";
        int x_start = 1000;
        int y_start = 1000;

        HpnPosition->Add(Vector(x_start, y_start, 25));

        for (double i = 0; i < 2 * PI; i += PI / 3) {
            HpnPosition->Add(Vector(x_start + distance * cos(i), y_start + distance * sin(i), 25));
        }

        for (double i = 0; i < 2 * PI; i += PI / 3) {
            HpnPosition->Add(Vector(x_start + distance * cos(i) + rand() % 100 + 10, y_start + distance * sin(i) + rand() % 100 + 10, 10));
            HpnPosition->Add(Vector(x_start + distance * cos(i) + rand() % 100 + 10, y_start + distance * sin(i) - rand() % 100 + 10, 10));
            HpnPosition->Add(Vector(x_start + distance * cos(i) + rand() % 100 - 10, y_start + distance * sin(i) + rand() % 100 + 10, 10));
            HpnPosition->Add(Vector(x_start + distance * cos(i) + rand() % 100 - 10, y_start + distance * sin(i) - rand() % 100 + 10, 10));
        }
        return HpnPosition;
    }
}


// migrations manager
void manager()
{
    cout << "manager started at " << Simulator::Now().GetSeconds() << " \n";

    for (int i = 0; i < numEdgeNodes; ++i) {
        cout << "Edge server n " << i << " with " << resources[i] << " resource units\n";
    }

    cout << "..................................\n\n\n";

        for (int i = 0; i < numNodes; ++i) {
            // check if node is being served
            if (getEdge(i) != -1) {

                if(getEdge(i) == getCellId(i))
                    cout << "node " << i << " being served by tier 1\n";
                else
                    cout << "node " << i << "being served by tier 2\n";
                // perform predictions to update prediction vector

                int bestEdgeServer = -1;
                int greatestScore = -1;
                int edgeId = 0;

                HandoverPrediction(i, 5);

                if (Seconds(handoverPredictions[i][0]) > Simulator::Now()) {
                    // for (int edgeId = 0; edgeId < numEdgeNodes; ++edgeId) {
                    while (edgeId < numEdgeNodes) {
                        double qosvalue = 0;
                        double score = 0;
                        // target cell of the handover
                        int handoverTo = handoverPredictions[i][2];

                        if (edgeId == getEdge(i))
                            qosvalue = qosValues[edgeId] / 2;
                        else
                            qosvalue = qosValues[edgeId];
                        score = 0.48 / qosvalue;

                        if (resources[edgeId] == 0 && algorithm == "MSMF")
                            score = 0;

                        if (score > greatestScore) {
                            greatestScore = score;
                            bestEdgeServer = edgeId;
                        }
                        edgeId++;
                    }
                    if (bestEdgeServer != getEdge(i) && algorithm != "nomigration" && algorithm != "greedy") {
                        if (edgeMigrationChart[i][bestEdgeServer] + 5 > Simulator::Now().GetSeconds())
                            return;
                        else {
                            migrate(edgeNodes.Get(getEdge(i)), edgeNodes.Get(bestEdgeServer), 
                                    edgeNodesAddresses[getEdge(i)][1], edgeNodesAddresses[bestEdgeServer][1]);
                            edgeMigrationChart[i][bestEdgeServer] = Simulator::Now().GetSeconds();
                        }
                    }
                }

                // renew applications periodically
                requestApplication(ueNodes.Get(i), edgeNodes.Get(getEdge(i)), edgeNodesAddresses[getEdge(i)][0]);
            }
        }

    Simulator::Schedule(managerInterval, &manager);
}

void getDelayFlowMon(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier)
{
    monitor->CheckForLostPackets();

    Time now = Simulator::Now();

    double txPacketsum;
    double rxPacketsum;
    double DropPacketsum;
    double Delaysum;
    double APD;

    //Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon->GetClassifier ());
    map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    // iterate all flows
    for (map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin(); iter != stats.end(); ++iter) {


        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);

        // get edge id
        int edgeId = -1;
        for (int i = 0; i < numEdgeNodes; ++i)
            if(t.destinationAddress == edgeNodesAddresses[i][0])
                edgeId = i;
        // return if flow does not belong to edge
        // if (edgeId == -1)
        //     return;

        txPacketsum += iter->second.txPackets;
        rxPacketsum += iter->second.rxPackets;
        DropPacketsum += iter->second.packetsDropped.size();
        Delaysum += iter->second.delaySum.GetSeconds();

        cout<<"Flow ID: " << iter->first << " Src Addr " << t.sourceAddress << " Dst Addr " << t.destinationAddress<<"\n";

        APD = (Delaysum / rxPacketsum); // APD = (Delaysum / txPacketsum); //to check
        qosValues[edgeId] = APD;
        cout << "Average Packet Delay: " << APD<< "\n";
    }
    Simulator::Schedule(managerInterval, &getDelayFlowMon, monitor, classifier);
}

int getNodeId(Ptr<Node> node, string type = "edge")
{

    // seleced the desired node container
    NodeContainer tmpNodesContainer;
    if (type == "edge")
        tmpNodesContainer = edgeNodes;
    else if (type == "ue")
        tmpNodesContainer = ueNodes;
    else if (type == "enb")
        tmpNodesContainer = enbNodes;

    // find th enode id
    for (uint32_t i = 0; i < tmpNodesContainer.GetN(); ++i) {
        if (node == tmpNodesContainer.Get(i))
            return i;
    }

    // return -1 if no cell has been found
    return -1;
}

int getEdge(int nodeId)
{
    int edgeId = -1;
    for (int i = 0; i < numEdgeNodes; ++i)
        if (edgeUe[i][nodeId])
            edgeId = i;
    return edgeId;
}

void requestApplication(Ptr<Node> ueNode,
    Ptr<Node> targetServer,
    Ipv4Address targetServerAddress)
{

    // return if migration is not available
    // and if node is being served
    if (!doMigrate || getEdge(getNodeId(ueNode, "ue")) < 0) {
        cout << "Migration not enabled. :(\n";
        return;
    }

    NS_LOG_UNCOND("Node " << getNodeId(ueNode, "ue") << " requesting application from node " << getNodeId(targetServer, "edge"));
    if (resources[getNodeId(targetServer)] <= 0) {
        // NS_LOG_UNCOND("APPLICATION FAILED FOR LACK OF RESOURCES");
        return;
    }


    // cout << "Starting migration from node " << sourceServerAddress << " to node " << targetServerAddress << ".\n";
    ++applicationPort;
    UdpServerHelper server(applicationPort);
    ApplicationContainer apps = server.Install(targetServer);
    apps.Start(Simulator::Now());
    // apps.Stop (Simulator::Now()+Seconds(5));

    //
    // Create one UdpClient application to send UDP datagrams from node zero to
    // node one.
    //

    uint32_t MaxPacketSize = 1024;
    // uint32_t maxPacketCount = migrationSize / MaxPacketSize;
    uint32_t maxPacketCount = 50;
    // tyr to migrate this in 10 senconds at most
    Time interPacketInterval = MilliSeconds(10);
    UdpClientHelper client(targetServerAddress, applicationPort);
    client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
    client.SetAttribute("Interval", TimeValue(interPacketInterval));
    client.SetAttribute("PacketSize", UintegerValue(MaxPacketSize));
    apps = client.Install(ueNode);
    apps.Start(Simulator::Now());
}

void migrate(Ptr<Node> sourceServer,
    Ptr<Node> targetServer,
    Ipv4Address sourceServerAddress,
    Ipv4Address targetServerAddress)
{

    // return if migration is not available
    if (!doMigrate) {
        cout << "Migration not enabled. :(\n";
        return;
    }

    if (resources[getNodeId(targetServer)] <= 0) {
        NS_LOG_UNCOND("MIGRATION FAILED FOR LACK OF RESOURCES");
        return;
    }
    if (getNodeId(targetServer) < 0)
        return;
    
    NS_LOG_UNCOND("Migration from node " << getNodeId(sourceServer) << " to node " << getNodeId(targetServer));

    resources[getNodeId(sourceServer)]++;
    resources[getNodeId(targetServer)]--;

    // cout << "Starting migration from node " << sourceServerAddress << " to node " << targetServerAddress << ".\n";
    ++migrationPort;
    UdpServerHelper server(migrationPort);
    ApplicationContainer apps = server.Install(targetServer);
    apps.Start(Simulator::Now());
    // apps.Stop (Simulator::Now()+Seconds(5));

    //
    // Create one UdpClient application to send UDP datagrams from node zero to
    // node one.
    //

    uint32_t MaxPacketSize = 1024;
    // uint32_t maxPacketCount = migrationSize / MaxPacketSize;
    uint32_t maxPacketCount = 10000;
    // tyr to migrate this in 10 senconds at most
    Time interPacketInterval = MilliSeconds(1);
    UdpClientHelper client(targetServerAddress, migrationPort);
    client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
    client.SetAttribute("Interval", TimeValue(interPacketInterval));
    client.SetAttribute("PacketSize", UintegerValue(MaxPacketSize));
    apps = client.Install(sourceServer);
    apps.Start(Simulator::Now());
}

int getCellId(int nodeId)
{
    // start this variable at an arbitrary value
    int cell = -1;
    // for (int i = 0; i < numNodes; ++i) {
    for (int j = 0; j < numEnbs; ++j) {
        if (cellUe[j][nodeId] != 0) {
            cell = j;
        }
    }
    // }
    // handle the unexpected value at upper layers if no connection if found
    return cell;
}

int main(int argc, char* argv[])
{
    int randomSeed = 5;
    
    // Command line arguments
    CommandLine cmd;
    cmd.AddValue ("algorithm", "algorithm", algorithm);
    cmd.AddValue ("numEnbs", "Number of Enbs in the simulation", numEnbs);
    cmd.AddValue ("numNodes", "number of Nodes in the simulation", numNodes);
    cmd.AddValue ("randomSeed", "randomSeed", randomSeed);
    cmd.AddValue ("verbose", "Tell echo applications to log if true", verbose);

    ConfigStore inputConfig;
    inputConfig.ConfigureDefaults();

    cmd.Parse(argc, argv);
    
    // logs enabled
    if (verbose) {
        LogComponentEnable("UdpL4Protocol", LOG_LEVEL_ALL);
        LogComponentEnable("PacketSink", LOG_LEVEL_ALL);
        // LogComponentEnable("Ipv4L3Protocol", LOG_LEVEL_ALL);
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_ALL);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_ALL);
    }
    LogComponentEnable("Ns3Migration", LOG_LEVEL_ALL);

    // random seed
    RngSeedManager::SetSeed(3); // Changes seed from default of 1 to 3

	numEdgeNodes = numEnbs;
    serverLoad.reserve(numEdgeNodes);
    qosValues.reserve(numEdgeNodes);
    cellUe.setDimensions(numEnbs, numNodes);
    edgeUe.setDimensions(numEdgeNodes, numNodes);
    edgeMigrationChart.setDimensions(numEdgeNodes, numEdgeNodes);
    edgeNodesAddresses.setDimensions(numEdgeNodes,2);
    handoverPredictions.setDimensions(numNodes,3);
    
    // fill all edge nodes with 10 processing units
    resources.assign(numEdgeNodes, initialResources);
    for (int i = 0; i < numEdgeNodes; ++i) {
        resources[i] = rand() % initialResources + 5;
        cout << "Edge server " << i << " initialized with " << resources[i] << " resources" << endl;
    }
    
    srand(randomSeed);
    
    // helpers used
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<EpcHelper> epcHelper;
    epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    Config::SetDefault("ns3::LteEnbNetDevice::DlBandwidth", UintegerValue(100));
    Config::SetDefault("ns3::LteEnbNetDevice::UlBandwidth", UintegerValue(100));

    // ctrl and data channel models
    Config::SetDefault("ns3::LteSpectrumPhy::CtrlErrorModelEnabled", BooleanValue(true));

    // transmission mode (SISO [0], MIMO [1])
    Config::SetDefault("ns3::LteEnbRrc::DefaultTransmissionMode",
        UintegerValue(1));
    // path loss model
    lteHelper->SetAttribute("PathlossModel",
        StringValue("ns3::RangePropagationLossModel"));
    // UL and DL frequencies
    lteHelper->SetEnbDeviceAttribute("DlEarfcn", UintegerValue(100));
    lteHelper->SetEnbDeviceAttribute("UlEarfcn", UintegerValue(18100));
    lteHelper->SetSchedulerType("ns3::PssFfMacScheduler");
    // the maximum number of UE selected by TD scheduler
    lteHelper->SetSchedulerAttribute("nMux", UintegerValue(1));
    // PF scheduler type in PSS
    lteHelper->SetSchedulerAttribute("PssFdSchedulerType", StringValue("CoItA"));

    // RRC model
    Config::SetDefault("ns3::LteHelper::UseIdealRrc", BooleanValue(true));

    // Antenna parameters
    lteHelper->SetEnbAntennaModelType("ns3::CosineAntennaModel");
    lteHelper->SetEnbAntennaModelAttribute("Orientation", DoubleValue(0));
    lteHelper->SetEnbAntennaModelAttribute("Beamwidth", DoubleValue(60));
    lteHelper->SetEnbAntennaModelAttribute("MaxGain", DoubleValue(0.0));

    // Handover configuration
    lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
    lteHelper->SetHandoverAlgorithmAttribute("Hysteresis",
        DoubleValue(0));
    lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger",
        TimeValue(MilliSeconds(0)));

    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // create internet stack
    InternetStackHelper internet;

    // create users, enbs and fog nodes
    // NodeContainer ueNodes;
    // NodeContainer enbNodes;
    // NodeContainer edgeNodes;

    enbNodes.Create(numEnbs);
    ueNodes.Create(numNodes);
    edgeNodes.Create(numEdgeNodes);

    /* edge nodes configuration*/
    internet.Install(edgeNodes);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    for (int i = 0; i < numEdgeNodes; ++i) {
        // create all edge nodes with different delays, some of them unfit fot the application

        // Create the Internet
        PointToPointHelper p2ph;
        p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
        p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
        // random link delay
        p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(rand() % 40)));
        NetDeviceContainer internetDevices = p2ph.Install(pgw, edgeNodes.Get(i));
        Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
        // interface 0 is localhost, 1 is the p2p device
        edgeNodesAddresses[i][0] = internetIpIfaces.GetAddress(1);

        // add network routes to fog nodes
        Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(edgeNodes.Get(i)->GetObject<Ipv4>());
        remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);
        // p2ph.EnablePcapAll("lena-simple-epc-backhaul");
    }

    /*-----------------POSIÇÃO DAS TORRES----------------------------------*/
    Ptr<ListPositionAllocator> HpnPosition = CreateObject<ListPositionAllocator>();
    HpnPosition = positionAllocator(HpnPosition);

    MobilityHelper remoteHostMobility;
    remoteHostMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    remoteHostMobility.Install(edgeNodes);
    remoteHostMobility.Install(pgw);

    MobilityHelper mobilityEnb;
    mobilityEnb.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityEnb.SetPositionAllocator(HpnPosition);
    mobilityEnb.Install(enbNodes);

    // Ns2MobilityHelper ue_mobil = Ns2MobilityHelper("mobil/SanFrancisco.tcl");
    Ns2MobilityHelper ue_mobil = Ns2MobilityHelper(mobilityTrace);
    MobilityHelper ueMobility;
    MobilityHelper enbMobility;

    ue_mobil.Install(ueNodes.Begin(), ueNodes.End());

    // Install LTE Devices to the nodes
    NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice(ueNodes);

    Ipv4AddressHelper s1uIpv4AddressHelper;
    Ipv4AddressHelper edgeIpv4AddressHelper;

    // Create networks of the S1 interfaces
    s1uIpv4AddressHelper.SetBase("10.0.0.0", "255.255.255.0");
    edgeIpv4AddressHelper.SetBase("22.0.0.0", "255.255.255.0");

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Gbps"));
    csma.SetChannelAttribute("Delay", StringValue("0ms"));
    NetDeviceContainer d2345 = csma.Install(edgeNodes);
    Ipv4InterfaceContainer edgeIpIfaces = edgeIpv4AddressHelper.Assign(d2345);
    for (uint32_t i = 0; i < edgeIpIfaces.GetN(); ++i) {
        edgeNodesAddresses[i][1] = edgeIpIfaces.GetAddress(i);
    }
    NS_LOG_UNCOND(edgeIpIfaces.GetAddress(0));

    // Install the IP stack on the UEs
    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIface;
    ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueLteDevs));
    // Assign IP address to UEs, and install applications
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
        Ptr<Node> ueNode = ueNodes.Get(u);
        // Set the default gateway for the UE
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    // Attach one UE per eNodeB
    lteHelper->Attach(ueLteDevs);

    // add x2 interface
    // todo: run handover forecaster
    // with a custom one in this file
    lteHelper->AddX2Interface(enbNodes);

    lteHelper->EnableTraces();
    /*END OF SCENARIO SETUP*/

    // --------------------EVENTS-----------------------------------------


    Simulator::Schedule(Simulator::Now(), &manager);

    // netanim setup
    AnimationInterface anim("migration-animation.xml"); // Mandatory

    // callbacks from handover events
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
        MakeCallback(&NotifyConnectionEstablishedUe));
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
        MakeCallback(&NotifyHandoverStartUe));
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
        MakeCallback(&NotifyHandoverEndOkUe));

    // intall flow monitor and get stats
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

    // getDelayFlowMon(monitor, classifier);
    Simulator::Schedule(Seconds(1), &getDelayFlowMon, monitor, classifier);

    Simulator::Stop(simTime);
    Simulator::Run();

    monitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    for (map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        cout << "Flow " << i->first << " (" << t.sourceAddress << " ->" << t.destinationAddress << ")\n";
        cout << "  Tx Packets: " << i->second.txPackets << "\n";
        cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
        cout << "  TxOffered:  " << i->second.txBytes * 8.0 / 9.0 / 1000 / 1000 << " Mbps\n";
        cout << "  Rx Packets: " << i->second.rxPackets << "\n";
        cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
        cout << "  Lost Packets:   " << i->second.lostPackets << "\n";
        cout << "  Throughput: " << i->second.rxBytes * 8.0 / 9.0 / 1000 / 1000 << " Mbps\n";
        if (i->second.rxBytes)
            cout << "  DelaySum: " << i->second.jitterSum / (i->second.rxPackets + i->second.txPackets) << "\n";
        cout << "......................................\n";
    }

    // serialize flow monitor to xml
    flowmon.SerializeToXmlFile("migration_flowmon.xml", true, true);

    Simulator::Destroy();
    return 0;
}
