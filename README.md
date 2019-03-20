ndnSIM
======

[![Build Status](https://travis-ci.org/named-data-ndnSIM/ndnSIM.svg)](https://travis-ci.org/named-data-ndnSIM/ndnSIM)

A new release of [NS-3 based Named Data Networking (NDN) simulator](http://ndnsim.net/)
went through extensive refactoring and rewriting.  The key new features of the new
version:

- Packet format changed to [NDN Packet Specification](http://named-data.net/doc/ndn-tlv/)

- ndnSIM uses implementation of basic NDN primitives from
  [ndn-cxx library (NDN C++ library with eXperimental eXtensions)](http://named-data.net/doc/ndn-cxx/)

  Based on version `0.6.0`

- All NDN forwarding and management is implemented directly using source code of
  [Named Data Networking Forwarding Daemon (NFD)](http://named-data.net/doc/NFD/)

  Based on version `0.6.0`

- Allows [simulation of real applications](http://ndnsim.net/guide-to-simulate-real-apps.html)
  written against ndn-cxx library

- Requires a modified version of NS-3 based on version `ns-3.27-22-g90fb309d5`

[ndnSIM documentation](http://ndnsim.net)
---------------------------------------------

For more information, including downloading and compilation instruction, please refer to
http://ndnsim.net or documentation in `docs/` folder.


Assignment Description
---------------------------------------------

In the network environment centering on the current TCP / IP, it is necessary to know the exact location of the contents to achieve desired contents by using the address as a host-centric network. However, in the ICN network environment, since contents are retrieved through the name of contents, contents can be received through all nodes having corresponding contents even if they do not know the exact position of contents. This feature is one of the solutions to overcome the problems in existing TCP / IP architecture. In addition, since the ICN network has high scalability and flexibility, it is possible to add various functions relatively easily within a protocol supporting ICN.

In this repositories, we have studied to improve the overall transmission efficiency in a specific situation such as a network combining a wired and a wireless environment. In order to transmit a large amount of contents (i.e. software patch) to the wireless nodes which are connected to the wifi AP in the IoT environment, a forwarding strategy is designed to allow the AP to check the status of the network and determine the transmission. When the network is highly congested, we set the behavior of Consumer and Producer nodes to limit and control the transmission of contents for a certain period of time in order to prevent more severe congestion caused by massive transmission. Later, when the network condition is clear, the transmission can be resumed again. In this report, we will discuss the simulation environment, the method for measuring network congestion, limiting the transmission of contents.
