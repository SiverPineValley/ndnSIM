/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014-2018,  Regents of the University of California,
 *                           Arizona Board of Regents,
 *                           Colorado State University,
 *                           University Pierre & Marie Curie, Sorbonne University,
 *                           Washington University in St. Louis,
 *                           Beijing Institute of Technology,
 *                           The University of Memphis.
 *
 * This file is part of NFD (Named Data Networking Forwarding Daemon).
 * See AUTHORS.md for complete list of NFD authors and contributors.
 *
 * NFD is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NFD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "forwarder.hpp"
#include "algorithm.hpp"
#include "best-route-strategy2.hpp"
#include "strategy.hpp"
#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/node-list.h"
#include "ns3/double.h"
#include "ns3/names.h"
#include "core/logger.hpp"
#include "table/cleanup.hpp"
#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/wifi-net-device.h"
#include <ndn-cxx/lp/tags.hpp>

#include "face/null-face.hpp"

namespace nfd {

NFD_LOG_INIT("Forwarder");

static Name
getDefaultStrategyName()
{
  return fw::BestRouteStrategy2::getStrategyName();
}

Forwarder::Forwarder()
  : m_unsolicitedDataPolicy(new fw::DefaultUnsolicitedDataPolicy())
  , m_fib(m_nameTree)
  , m_pit(m_nameTree)
  , m_measurements(m_nameTree)
  , m_strategyChoice(*this)
  , m_csFace(face::makeNullFace(FaceUri("contentstore://")))
{
  getFaceTable().addReserved(m_csFace, face::FACEID_CONTENT_STORE);

  m_faceTable.afterAdd.connect([this] (Face& face) {
    face.afterReceiveInterest.connect(
      [this, &face] (const Interest& interest) {
        this->startProcessInterest(face, interest);
      });
    face.afterReceiveData.connect(
      [this, &face] (const Data& data) {
        this->startProcessData(face, data);
      });
    face.afterReceiveNack.connect(
      [this, &face] (const lp::Nack& nack) {
        this->startProcessNack(face, nack);
      });
    face.onDroppedInterest.connect(
      [this, &face] (const Interest& interest) {
        this->onDroppedInterest(face, interest);
      });
  });

  m_faceTable.beforeRemove.connect([this] (Face& face) {
    cleanupOnFaceRemoval(m_nameTree, m_fib, m_pit, face);
  });

  m_strategyChoice.setDefaultStrategy(getDefaultStrategyName());
}

Forwarder::~Forwarder() = default;

void
Forwarder::onIncomingInterest(Face& inFace, const Interest& interest)
{
  // receive Interest
  // NFD_LOG_DEBUG("onIncomingInterest face=" << inFace.getId() <<
  //               " interest=" << interest.getName());

  // uint32_t seq = interest.getName().at(-1).toSequenceNumber(); << "\t" << seq
  ns3::Ptr<ns3::Node> node = ns3::NodeList::GetNode(ns3::Simulator::GetContext()); 
  std::string Nname = ns3::Names::FindName(node);
  NFD_LOG_DEBUG(ns3::Simulator::Now().ToDouble(ns3::Time::S) << "\t" << "Interest Received in forwarder" <<  "\t" << Nname << "\t" << node-> GetId() << "\t" << interest.getName().toUri() );

  interest.setTag(make_shared<lp::IncomingFaceIdTag>(inFace.getId()));
  ++m_counters.nInInterests;

  // /localhost scope control
  bool isViolatingLocalhost = inFace.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL &&
                              scope_prefix::LOCALHOST.isPrefixOf(interest.getName());
  if (isViolatingLocalhost) {
    // NFD_LOG_DEBUG("onIncomingInterest face=" << inFace.getId() <<
    //               " interest=" << interest.getName() << " violates /localhost");
    // (drop)
    return;
  }

  // detect duplicate Nonce with Dead Nonce List
  bool hasDuplicateNonceInDnl = m_deadNonceList.has(interest.getName(), interest.getNonce());
  if (hasDuplicateNonceInDnl) {
    // goto Interest loop pipeline
    this->onInterestLoop(inFace, interest);
    return;
  }

  // strip forwarding hint if Interest has reached producer region
  if (!interest.getForwardingHint().empty() &&
      m_networkRegionTable.isInProducerRegion(interest.getForwardingHint())) {
    // NFD_LOG_DEBUG("onIncomingInterest face=" << inFace.getId() <<
    //               " interest=" << interest.getName() << " reaching-producer-region");
    const_cast<Interest&>(interest).setForwardingHint({});
  }

  // PIT insert
  shared_ptr<pit::Entry> pitEntry = m_pit.insert(interest).first;

  // detect duplicate Nonce in PIT entry
  int dnw = fw::findDuplicateNonce(*pitEntry, interest.getNonce(), inFace);
  bool hasDuplicateNonceInPit = dnw != fw::DUPLICATE_NONCE_NONE;
  if (inFace.getLinkType() == ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
    // for p2p face: duplicate Nonce from same incoming face is not loop
    hasDuplicateNonceInPit = hasDuplicateNonceInPit && !(dnw & fw::DUPLICATE_NONCE_IN_SAME);
  }
  if (hasDuplicateNonceInPit) {
    // goto Interest loop pipeline
    this->onInterestLoop(inFace, interest);
    return;
  }

  // is pending?
  if (!pitEntry->hasInRecords()) {
    if (m_csFromNdnSim == nullptr) {
      m_cs.find(interest,
                bind(&Forwarder::onContentStoreHit, this, ref(inFace), pitEntry, _1, _2),
                bind(&Forwarder::onContentStoreMiss, this, ref(inFace), pitEntry, _1));
    }
    else {
      shared_ptr<Data> match = m_csFromNdnSim->Lookup(interest.shared_from_this());
      if (match != nullptr) {
        ////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////
        // Check Interest Payload that requests the Contents which is Huge size.
        ////////////////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////

        std::string content_name = interest.getName().toUri();

        // Huge Data Request Interests
        if ( (content_name.find("Huge") != std::string::npos) || (content_name.find("Mid") != std::string::npos) )
        {
          ns3::Ptr<ns3::Node> n = ns3::NodeList::GetNode(0);
          const nfd::Pit& p = n->GetObject<ns3::ndn::L3Protocol>()->getForwarder()->getPit();
          // Ptr<ns3::NetDevice> dev = n->GetDevice (0);
          // Ptr<ns3::WifiNetDevice> wifi_dev = DynamicCast<WifiNetDevice> (dev);
          // uint32_t qsize = wifi_dev->GetQueueSize();
          int cnt = 0; // num of sensitive packet
          for(auto it = p.begin(); it != p.end(); it++)
          {
            if( it->getName().toUri().find("root") != std::string::npos ) cnt++;
          }
          // If Network is busy. m_pit
          if ( cnt >=7 )
          {
            if((is_mid == 0) && (is_huge == 1) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 1 && is_huge == 1 && (content_name.find("Mid0") == std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 1 && is_huge == 1 && (content_name.find("Mid0") != std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              is_mid--;
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 2 && is_huge == 1 && ((content_name.find("Mid2") != std::string::npos) || (content_name.find("Huge") != std::string::npos) ) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 2 && is_huge == 1 && (content_name.find("Mid1") != std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              is_mid--;
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 3 && is_huge == 1 && (content_name.find("Huge") != std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 3 && is_huge == 1 && (content_name.find("Mid2") != std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              is_mid--;
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if((is_huge == 0) && (content_name.find("Huge") != std::string::npos))
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              is_huge = 1;
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
          }
          else
          {
            if((is_mid == 0) && (is_huge == 1) && (content_name.find("Mid0") == std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 0 && is_huge == 1 && (content_name.find("Mid0") != std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              is_mid++;
            }
            else if(is_mid == 1 && is_huge == 1 && ((content_name.find("Mid0") != std::string::npos) || (content_name.find("Mid1") != std::string::npos)) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              is_mid++;
            }
            else if(is_mid == 1 && is_huge == 1 )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 2 && is_huge == 1 && ((content_name.find("Mid2") != std::string::npos) || (content_name.find("Mid1") != std::string::npos) || (content_name.find("Mid0") != std::string::npos) ) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              is_mid++;
            }
            else if(is_mid == 2 && is_huge == 1 )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 3 && is_huge == 1 && (content_name.find("Huge") != std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              lp::Nack nack(interest);
              nack.setReason(lp::NackReason::CONGESTION);
              const_cast<Face&>(inFace).sendNack(nack);
              this->setExpiryTimer(pitEntry, 0_ms);
              return;
            }
            else if(is_mid == 3 && is_huge == 1 && (content_name.find("Huge") == std::string::npos) )
            {
              // Sending Packet that implies the sending of Huge Data will be started later.
              is_huge = 0;
            }
            // is_huge = 0;
            // is_mid = 3;
          }
          // Else, Network is not busy
          // Find the Matching Contents.
          // If the Data exists in CS, it will start sending Huge Data.
        }
        this->onContentStoreHit(inFace, pitEntry, interest, *match);
      } // Hit
      else {
        this->onContentStoreMiss(inFace, pitEntry, interest);
      }
    }
  }
  else {
    this->onContentStoreMiss(inFace, pitEntry, interest);
  }
}

void
Forwarder::onInterestLoop(Face& inFace, const Interest& interest)
{
  // if multi-access or ad hoc face, drop
  if (inFace.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
    // NFD_LOG_DEBUG("onInterestLoop face=" << inFace.getId() <<
    //               " interest=" << interest.getName() <<
    //               " drop");
    return;
  }

  // NFD_LOG_DEBUG("onInterestLoop face=" << inFace.getId() <<
  //               " interest=" << interest.getName() <<
  //               " send-Nack-duplicate");

  // send Nack with reason=DUPLICATE
  // note: Don't enter outgoing Nack pipeline because it needs an in-record.
  lp::Nack nack(interest);
  nack.setReason(lp::NackReason::DUPLICATE);
  inFace.sendNack(nack);
}

static inline bool
compare_InRecord_expiry(const pit::InRecord& a, const pit::InRecord& b)
{
  return a.getExpiry() < b.getExpiry();
}

void
Forwarder::onContentStoreMiss(const Face& inFace, const shared_ptr<pit::Entry>& pitEntry,
                              const Interest& interest)
{
  // NFD_LOG_DEBUG("onContentStoreMiss interest=" << interest.getName());
  ++m_counters.nCsMisses;

  // insert in-record
  pitEntry->insertOrUpdateInRecord(const_cast<Face&>(inFace), interest);

  // set PIT expiry timer to the time that the last PIT in-record expires
  auto lastExpiring = std::max_element(pitEntry->in_begin(), pitEntry->in_end(), &compare_InRecord_expiry);
  auto lastExpiryFromNow = lastExpiring->getExpiry() - time::steady_clock::now();
  this->setExpiryTimer(pitEntry, time::duration_cast<time::milliseconds>(lastExpiryFromNow));

  // has NextHopFaceId?
  shared_ptr<lp::NextHopFaceIdTag> nextHopTag = interest.getTag<lp::NextHopFaceIdTag>();
  if (nextHopTag != nullptr) {
    // chosen NextHop face exists?
    Face* nextHopFace = m_faceTable.get(*nextHopTag);
    if (nextHopFace != nullptr) {
      // NFD_LOG_DEBUG("onContentStoreMiss interest=" << interest.getName() << " nexthop-faceid=" << nextHopFace->getId());
      // go to outgoing Interest pipeline
      // scope control is unnecessary, because privileged app explicitly wants to forward
      this->onOutgoingInterest(pitEntry, *nextHopFace, interest);
    }
    return;
  }

  // dispatch to strategy: after incoming Interest
  this->dispatchToStrategy(*pitEntry,
    [&] (fw::Strategy& strategy) { strategy.afterReceiveInterest(inFace, interest, pitEntry); });
}

void
Forwarder::onContentStoreHit(const Face& inFace, const shared_ptr<pit::Entry>& pitEntry,
                             const Interest& interest, const Data& data)
{
  // NFD_LOG_DEBUG("onContentStoreHit interest=" << interest.getName());
  ++m_counters.nCsHits;

  data.setTag(make_shared<lp::IncomingFaceIdTag>(face::FACEID_CONTENT_STORE));
  // XXX should we lookup PIT for other Interests that also match csMatch?

  pitEntry->isSatisfied = true;
  pitEntry->dataFreshnessPeriod = data.getFreshnessPeriod();

  // set PIT expiry timer to now
  this->setExpiryTimer(pitEntry, 0_ms);

  beforeSatisfyInterest(*pitEntry, *m_csFace, data);
  this->dispatchToStrategy(*pitEntry,
    [&] (fw::Strategy& strategy) { strategy.beforeSatisfyInterest(pitEntry, *m_csFace, data); });

  // dispatch to strategy: after Content Store hit
  this->dispatchToStrategy(*pitEntry,
    [&] (fw::Strategy& strategy) { strategy.afterContentStoreHit(pitEntry, inFace, data); });
}

void
Forwarder::onOutgoingInterest(const shared_ptr<pit::Entry>& pitEntry, Face& outFace, const Interest& interest)
{
  // NFD_LOG_DEBUG("onOutgoingInterest face=" << outFace.getId() <<
  //               " interest=" << pitEntry->getName());

  // uint32_t seq = interest.getName().at(-1).toSequenceNumber(); << "\t" << seq
  ns3::Ptr<ns3::Node> node = ns3::NodeList::GetNode(ns3::Simulator::GetContext());
  std::string Nname = ns3::Names::FindName(node); 
  double cur = ns3::Simulator::Now().ToDouble(ns3::Time::S);
  // m_cal.insert(std::make_pair(seq,cur));
  NFD_LOG_DEBUG(cur << "\t" << "Interest Sent from forwarder" << "\t" << Nname << "\t" << node->GetId() << "\t" << interest.getName().toUri() );

  // insert out-record
  pitEntry->insertOrUpdateOutRecord(outFace, interest);

    if( interest.getName().toUri().find("root") != std::string::npos )
    {
      // send Interest
      outFace.sendInterest(interest);
      ++m_counters.nOutInterests;
      return;
    }

  ns3::Ptr<ns3::Node> n = ns3::NodeList::GetNode(0);
  const nfd::Pit& p = n->GetObject<ns3::ndn::L3Protocol>()->getForwarder()->getPit();
  int cnt = 0; // num of sensitive packet
  if(!this->m_pit.getTransmit())
  {
    for(auto it = p.begin(); it != p.end(); it++)
    {
      if( it->getName().toUri().find("root") != std::string::npos ) cnt++;
    }
    if( cnt < 7 ) // interest.getPitSize()
    {
      if(!m_rtxInterest.empty())
      {
        // int i = 10;
        auto it = m_rtxInterest.front();
        while((it != m_rtxInterest.back()))
        {
          outFace.sendInterest(it);
          ++m_counters.nOutInterests;
          m_rtxInterest.pop();
          it = m_rtxInterest.front();
          // i--;
        }
      }
      if((ns3::Simulator::Now().GetSeconds()-lock) > 60)
        m_pit.setTransmit(true);
    }
    else if( cnt >= 7 )
    {
      this->setExpiryTimer(pitEntry, ndn::time::milliseconds(60000));
      m_rtxInterest.push(interest);
      return;
    }
  }

  if(this->m_pit.getTransmit())
  {
    // send Interest
    outFace.sendInterest(interest);
    ++m_counters.nOutInterests;
  }
}

void
Forwarder::onInterestFinalize(const shared_ptr<pit::Entry>& pitEntry)
{
  // NFD_LOG_DEBUG("onInterestFinalize interest=" << pitEntry->getName() <<
  //               (pitEntry->isSatisfied ? " satisfied" : " unsatisfied"));

  if (!pitEntry->isSatisfied) {
    beforeExpirePendingInterest(*pitEntry);
  }

  // Dead Nonce List insert if necessary
  this->insertDeadNonceList(*pitEntry, 0);

  // PIT delete
  scheduler::cancel(pitEntry->expiryTimer);
  m_pit.erase(pitEntry.get());
}

void
Forwarder::onIncomingData(Face& inFace, const Data& data)
{
  // receive Data
  // NFD_LOG_DEBUG("onIncomingData face=" << inFace.getId() << " data=" << data.getName());  
  // uint32_t seq = data.getName().at(-1).toSequenceNumber(); << "\t" << seq
  double cur = ns3::Simulator::Now().ToDouble(ns3::Time::S);
  if(cur > 0)
  {
    ns3::Ptr<ns3::Node> node = ns3::NodeList::GetNode(ns3::Simulator::GetContext()); 
    std::string Nname = ns3::Names::FindName(node); 
    NFD_LOG_DEBUG( cur << "\t" << "Data Received in Forwarder" << "\t" << Nname << "\t" << node->GetId() << "\t" << data.getName().toUri() );
  }

  data.setTag(make_shared<lp::IncomingFaceIdTag>(inFace.getId()));
  ++m_counters.nInData;

  // /localhost scope control
  bool isViolatingLocalhost = inFace.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL &&
                              scope_prefix::LOCALHOST.isPrefixOf(data.getName());
  if (isViolatingLocalhost) {
    // NFD_LOG_DEBUG("onIncomingData face=" << inFace.getId() <<
    //               " data=" << data.getName() << " violates /localhost");
    // (drop)
    return;
  }

  // PIT match
  pit::DataMatchResult pitMatches = m_pit.findAllDataMatches(data);
  if (pitMatches.size() == 0) {
    // goto Data unsolicited pipeline
    this->onDataUnsolicited(inFace, data);
    return;
  }

  shared_ptr<Data> dataCopyWithoutTag = make_shared<Data>(data);
  dataCopyWithoutTag->removeTag<lp::HopCountTag>();

  // CS insert
  if (m_csFromNdnSim == nullptr)
  {
    m_cs.insert(*dataCopyWithoutTag);
  }
  else
    m_csFromNdnSim->Add(dataCopyWithoutTag);

  ns3::Ptr<ns3::Node> n = ns3::NodeList::GetNode(0);
  const nfd::Pit& p = n->GetObject<ns3::ndn::L3Protocol>()->getForwarder()->getPit();  
  int cnt = 0; // num of sensitive packet

  if(!this->m_pit.getTransmit())
  {
    for(auto it = p.begin(); it != p.end(); it++)
    {
      if( it->getName().toUri().find("root") != std::string::npos ) cnt++;
    }
    if( (cnt < 7) && (is_huge == 0) ) // data.getPitSize()
    {
      // if(!m_rtxInterest.empty())
      // {
      //   auto it = m_rtxInterest.front();
      //   while(it != m_rtxInterest.back())
      //   {
      //     inFace.sendInterest(it);
      //     m_rtxInterest.pop();
      //     it = m_rtxInterest.front();
      //   }
      // }
      m_pit.setTransmit(true);
    }
  }

  // when only one PIT entry is matched, trigger strategy: after receive Data
  if (pitMatches.size() == 1) {
    auto& pitEntry = pitMatches.front();

    // NFD_LOG_DEBUG("onIncomingData matching=" << pitEntry->getName());

    // set PIT expiry timer to now
    this->setExpiryTimer(pitEntry, 0_ms);

    beforeSatisfyInterest(*pitEntry, inFace, data);
    // trigger strategy: after receive Data
    this->dispatchToStrategy(*pitEntry,
      [&] (fw::Strategy& strategy) { strategy.afterReceiveData(pitEntry, inFace, data); });

    // mark PIT satisfied
    pitEntry->isSatisfied = true;
    pitEntry->dataFreshnessPeriod = data.getFreshnessPeriod();

    // Dead Nonce List insert if necessary (for out-record of inFace)
    this->insertDeadNonceList(*pitEntry, &inFace);

    // delete PIT entry's out-record
    pitEntry->deleteOutRecord(inFace);
  }
  // when more than one PIT entry is matched, trigger strategy: before satisfy Interest,
  // and send Data to all matched out faces
  else {

    std::string content_name = data.getName().toUri();    

    // Huge Data Incoming.
    if ( (content_name.find("Huge") != std::string::npos) || (content_name.find("Mid") != std::string::npos) )
    {
      // // If Network is busy. m_pit.size()
      if ( cnt >= 7 )
      {
        if((is_mid == 0) && (is_huge == 1))
        {
          m_rtxData.push(data);
          return;
        }
        else if( is_mid == 1 && is_huge == 1 && (content_name.find("Mid0") == std::string::npos) )
        {
          m_rtxData.push(data);
          return;
        }
        else if( is_mid == 2 && is_huge == 1 && ((content_name.find("Mid2") != std::string::npos) || (content_name.find("Huge") != std::string::npos) ) )
        {
          m_rtxData.push(data);
          return;
        }
        else if( is_mid == 3 && is_huge == 1 && (content_name.find("Huge") != std::string::npos) )
        {
          m_rtxData.push(data);
          return;
        }
      // Else, If the network is not busy, it will start sending Huge Data.
      }
    }
    // When It is Not Huge Data.

    std::set<Face*> pendingDownstreams;
    auto now = time::steady_clock::now();

    for (const shared_ptr<pit::Entry>& pitEntry : pitMatches) {
      // NFD_LOG_DEBUG("onIncomingData matching=" << pitEntry->getName());

      // remember pending downstreams
      for (const pit::InRecord& inRecord : pitEntry->getInRecords()) {
        if (inRecord.getExpiry() > now) {
          pendingDownstreams.insert(&inRecord.getFace());
        }
      }

      // set PIT expiry timer to now
      this->setExpiryTimer(pitEntry, 0_ms);

      // invoke PIT satisfy callback
      beforeSatisfyInterest(*pitEntry, inFace, data);
      this->dispatchToStrategy(*pitEntry,
        [&] (fw::Strategy& strategy) { strategy.beforeSatisfyInterest(pitEntry, inFace, data); });

      // mark PIT satisfied
      pitEntry->isSatisfied = true;
      pitEntry->dataFreshnessPeriod = data.getFreshnessPeriod();

      // Dead Nonce List insert if necessary (for out-record of inFace)
      this->insertDeadNonceList(*pitEntry, &inFace);

      // clear PIT entry's in and out records
      pitEntry->clearInRecords();
      pitEntry->deleteOutRecord(inFace);
    }

    // foreach pending downstream
    for (Face* pendingDownstream : pendingDownstreams) {
      if (pendingDownstream->getId() == inFace.getId() &&
          pendingDownstream->getLinkType() != ndn::nfd::LINK_TYPE_AD_HOC) {
        continue;
      }
      // goto outgoing Data pipeline
      this->onOutgoingData(data, *pendingDownstream);

    }

    if(!m_rtxData.empty() && ( cnt < 7 ) ) // m_pit
    {
      auto it = m_rtxData.front();
      int i = 0;
      while( ( it != m_rtxData.back() ) || ( i < 10 ) )
      { 

        for (Face* pendingDownstream : pendingDownstreams) {
          if (pendingDownstream->getId() == inFace.getId() &&
              pendingDownstream->getLinkType() != ndn::nfd::LINK_TYPE_AD_HOC) {
            continue;
          }
          // goto outgoing Data pipeline
          (*pendingDownstream).sendData(data);
          ++m_counters.nOutData;
        }

        m_rtxData.pop();
        it = m_rtxData.front();
        i++;
      }
    }

  } // else
}   // function

void
Forwarder::onDataUnsolicited(Face& inFace, const Data& data)
{
  // accept to cache?
  fw::UnsolicitedDataDecision decision = m_unsolicitedDataPolicy->decide(inFace, data);
  if (decision == fw::UnsolicitedDataDecision::CACHE) {
    // CS insert
    if (m_csFromNdnSim == nullptr)
      m_cs.insert(data, true);
    else
      m_csFromNdnSim->Add(data.shared_from_this());
  }

  // NFD_LOG_DEBUG("onDataUnsolicited face=" << inFace.getId() <<
  //               " data=" << data.getName() <<
  //               " decision=" << decision);
}

void
Forwarder::onOutgoingData(const Data& data, Face& outFace)
{
  if (outFace.getId() == face::INVALID_FACEID) {
    NFD_LOG_WARN("onOutgoingData face=invalid data=" << data.getName());
    return;
  }
  // NFD_LOG_DEBUG("onOutgoingData face=" << outFace.getId() << " data=" << data.getName());
  // uint32_t seq = data.getName().at(-1).toSequenceNumber(); << "\t" << seq
  ns3::Ptr<ns3::Node> node = ns3::NodeList::GetNode(ns3::Simulator::GetContext());
  std::string Nname = ns3::Names::FindName(node); 
  NFD_LOG_DEBUG(ns3::Simulator::Now().ToDouble(ns3::Time::S) << "\t" << "Data Sent from forwarder" << "\t" << Nname << "\t" << node->GetId() << "\t" << data.getName().toUri() );
  // /localhost scope control
  bool isViolatingLocalhost = outFace.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL &&
                              scope_prefix::LOCALHOST.isPrefixOf(data.getName());
  if (isViolatingLocalhost) {
    // NFD_LOG_DEBUG("onOutgoingData face=" << outFace.getId() <<
    //               " data=" << data.getName() << " violates /localhost");
    // (drop)
    return;
  }

  // TODO traffic manager

  // send Data
  outFace.sendData(data);
  ++m_counters.nOutData;
}

void
Forwarder::onIncomingNack(Face& inFace, const lp::Nack& nack)
{
  // receive Nack
  nack.setTag(make_shared<lp::IncomingFaceIdTag>(inFace.getId()));
  ++m_counters.nInNacks;

  // if multi-access or ad hoc face, drop
  if (inFace.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
    // NFD_LOG_DEBUG("onIncomingNack face=" << inFace.getId() <<
    //               " nack=" << nack.getInterest().getName() <<
    //               "~" << nack.getReason() << " face-is-multi-access");
    return;
  }

  // PIT match
  shared_ptr<pit::Entry> pitEntry = m_pit.find(nack.getInterest());
  // if no PIT entry found, drop
  if (pitEntry == nullptr) {
    // NFD_LOG_DEBUG("onIncomingNack face=" << inFace.getId() <<
    //               " nack=" << nack.getInterest().getName() <<
    //               "~" << nack.getReason() << " no-PIT-entry");
    return;
  }

  // has out-record?
  pit::OutRecordCollection::iterator outRecord = pitEntry->getOutRecord(inFace);
  // if no out-record found, drop
  if (outRecord == pitEntry->out_end()) {
  //   NFD_LOG_DEBUG("onIncomingNack face=" << inFace.getId() <<
  //                 " nack=" << nack.getInterest().getName() <<
  //                 "~" << nack.getReason() << " no-out-record");
    return;
  }

  // if out-record has different Nonce, drop
  if (nack.getInterest().getNonce() != outRecord->getLastNonce()) {
    // NFD_LOG_DEBUG("onIncomingNack face=" << inFace.getId() <<
    //               " nack=" << nack.getInterest().getName() <<
    //               "~" << nack.getReason() << " wrong-Nonce " <<
    //               nack.getInterest().getNonce() << "!=" << outRecord->getLastNonce());
    return;
  }

  // NFD_LOG_DEBUG("onIncomingNack face=" << inFace.getId() <<
  //               " nack=" << nack.getInterest().getName() <<
  //               "~" << nack.getReason() << " OK");

  // record Nack on out-record
  outRecord->setIncomingNack(nack);
  ns3::Ptr<ns3::Node> n = ns3::NodeList::GetNode(ns3::Simulator::GetContext());
  // Get CONGESTION Nack (congestionnack)
  if ( nack.getReason() == lp::NackReason::CONGESTION )
  {
    // ns3::Ptr<ns3::Node> n = ns3::NodeList::GetNode(ns3::Simulator::GetContext());
    // const nfd::Pit& p = n->GetObject<ns3::ndn::L3Protocol>()->getForwarder()->getPit();    
    std::string content_name = nack.getInterest().getName().toUri();
    if( ((content_name.find("Huge") != std::string::npos) || (content_name.find("Mid") != std::string::npos)) && ( n->GetId() != 0 ) )
    {
      this->setExpiryTimer(pitEntry, ndn::time::milliseconds(60000)); // 60 seconds
      this->dontsend();
      return;
    }
  }
  std::string Nname = ns3::Names::FindName(n); 
  NFD_LOG_DEBUG(ns3::Simulator::Now().ToDouble(ns3::Time::S) << "\t" << "Nack Received in forwarder" << "\t" << Nname << "\t" << n->GetId() << "\t" << nack.getInterest().getName().toUri() << "\t" << "Reason" << "\t" << nack.getReason());  

  // set PIT expiry timer to now when all out-record receive Nack
  if (!fw::hasPendingOutRecords(*pitEntry)) {
    this->setExpiryTimer(pitEntry, 0_ms);
  }

  // trigger strategy: after receive NACK
  this->dispatchToStrategy(*pitEntry,
    [&] (fw::Strategy& strategy) { strategy.afterReceiveNack(inFace, nack, pitEntry); });
}

void
Forwarder::onOutgoingNack(const shared_ptr<pit::Entry>& pitEntry, const Face& outFace,
                          const lp::NackHeader& nack)
{
  if (outFace.getId() == face::INVALID_FACEID) {
    NFD_LOG_WARN("onOutgoingNack face=invalid" <<
                  " nack=" << pitEntry->getInterest().getName() <<
                  "~" << nack.getReason() << " no-in-record");
    return;
  }

  // has in-record?
  pit::InRecordCollection::iterator inRecord = pitEntry->getInRecord(outFace);

  // if no in-record found, drop
  if (inRecord == pitEntry->in_end()) {
    // NFD_LOG_DEBUG("onOutgoingNack face=" << outFace.getId() <<
    //               " nack=" << pitEntry->getInterest().getName() <<
    //               "~" << nack.getReason() << " no-in-record");
    return;
  }

  // if multi-access or ad hoc face, drop
  if (outFace.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
    // NFD_LOG_DEBUG("onOutgoingNack face=" << outFace.getId() <<
    //               " nack=" << pitEntry->getInterest().getName() <<
    //               "~" << nack.getReason() << " face-is-multi-access");
    return;
  }

  ns3::Ptr<ns3::Node> node = ns3::NodeList::GetNode(ns3::Simulator::GetContext());
  std::string Nname = ns3::Names::FindName(node);
  NFD_LOG_DEBUG(ns3::Simulator::Now().ToDouble(ns3::Time::S) << "\t" << "Nack Sent from forwarder" << "\t" << Nname << "\t" << node->GetId() << "\t" << pitEntry->getInterest().getName().toUri() << "\t" << "Reason" << "\t" << nack.getReason());

  // NFD_LOG_DEBUG("onOutgoingNack face=" << outFace.getId() <<
  //               " nack=" << pitEntry->getInterest().getName() <<
  //               "~" << nack.getReason() << " OK");

  // create Nack packet with the Interest from in-record
  lp::Nack nackPkt(inRecord->getInterest());
  nackPkt.setHeader(nack);

  // erase in-record
  pitEntry->deleteInRecord(outFace);

  // send Nack on face
  const_cast<Face&>(outFace).sendNack(nackPkt);
  ++m_counters.nOutNacks;
}

void
Forwarder::onDroppedInterest(Face& outFace, const Interest& interest)
{
  m_strategyChoice.findEffectiveStrategy(interest.getName()).onDroppedInterest(outFace, interest);
}

void
Forwarder::setExpiryTimer(const shared_ptr<pit::Entry>& pitEntry, time::milliseconds duration)
{
  BOOST_ASSERT(duration >= 0_ms);

  scheduler::cancel(pitEntry->expiryTimer);

  pitEntry->expiryTimer = scheduler::schedule(duration,
    bind(&Forwarder::onInterestFinalize, this, pitEntry));
}

static inline void
insertNonceToDnl(DeadNonceList& dnl, const pit::Entry& pitEntry,
                 const pit::OutRecord& outRecord)
{
  dnl.add(pitEntry.getName(), outRecord.getLastNonce());
}

void
Forwarder::insertDeadNonceList(pit::Entry& pitEntry, Face* upstream)
{
  // need Dead Nonce List insert?
  bool needDnl = true;
  if (pitEntry.isSatisfied) {
    BOOST_ASSERT(pitEntry.dataFreshnessPeriod >= 0_ms);
    needDnl = static_cast<bool>(pitEntry.getInterest().getMustBeFresh()) &&
              pitEntry.dataFreshnessPeriod < m_deadNonceList.getLifetime();
  }

  if (!needDnl) {
    return;
  }

  // Dead Nonce List insert
  if (upstream == nullptr) {
    // insert all outgoing Nonces
    const pit::OutRecordCollection& outRecords = pitEntry.getOutRecords();
    std::for_each(outRecords.begin(), outRecords.end(),
                  bind(&insertNonceToDnl, ref(m_deadNonceList), cref(pitEntry), _1));
  }
  else {
    // insert outgoing Nonce of a specific face
    pit::OutRecordCollection::iterator outRecord = pitEntry.getOutRecord(*upstream);
    if (outRecord != pitEntry.getOutRecords().end()) {
      m_deadNonceList.add(pitEntry.getName(), outRecord->getLastNonce());
    }
  }
}

} // namespace nfd
