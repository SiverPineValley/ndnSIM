/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014-2017,  Regents of the University of California,
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

#include "custom-strategy.hpp"
#include "algorithm.hpp"

namespace nfd {
namespace fw {

CustomStrategyBase::CustomStrategyBase(Forwarder& forwarder)
  : Strategy(forwarder)
{
}

void
CustomStrategyBase::afterReceiveInterest(const Face& inFace, const Interest& interest,
                                            const shared_ptr<pit::Entry>& pitEntry)
{
  if (hasPendingOutRecords(*pitEntry)) {
    // not a new Interest, don't forward
    return;
  }

  const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
  const fib::NextHopList& nexthops = fibEntry.getNextHops();

  for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it)
  {
    Face& outFace = it->getFace();
    if (!wouldViolateScope(inFace, interest, outFace) &&
        canForwardToLegacy(*pitEntry, outFace)) {
      this->sendInterest(pitEntry, outFace, interest);
      return;
    }
  }

  this->rejectPendingInterest(pitEntry);
}

void
CustomStrategyBase::afterReceiveNack(const Face& inFace, const lp::Nack& nack,
                           const shared_ptr<pit::Entry>& pitEntry)
{
  // NFD_LOG_DEBUG("afterReceiveNack inFace=" << inFace.getId() << " pitEntry=" << pitEntry->getName());

  // Get CONGESTION Nack
  if ( nack.getReason() == lp::NackReason::CONGESTION )
  {
    const shared_ptr<pit::Entry>& extEntry = this->lookupPit(nack.getInterest());
    this->setExpiryTimer(extEntry, ndn::time::milliseconds(250)); // 3 seconds
  }
}

NFD_REGISTER_STRATEGY(CustomStrategy);

CustomStrategy::CustomStrategy(Forwarder& forwarder, const Name& name)
  : CustomStrategyBase(forwarder)
{
  ParsedInstanceName parsed = parseInstanceName(name);
  if (!parsed.parameters.empty()) {
    BOOST_THROW_EXCEPTION(std::invalid_argument("CustomStrategy does not accept parameters"));
  }
  if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion()) {
    BOOST_THROW_EXCEPTION(std::invalid_argument(
      "CustomStrategy does not support version " + to_string(*parsed.version)));
  }
  this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

const Name&
CustomStrategy::getStrategyName()
{
  static Name strategyName("/localhost/nfd/strategy/custom/%FD%01");
  return strategyName;
}

} // namespace fw
} // namespace nfd
