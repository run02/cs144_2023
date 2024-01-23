#pragma once

#include "address.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"
#include "arp_message.hh"

#include <iostream>
#include <list>
#include <optional>
#include <queue>
#include <unordered_map>
#include <utility>

// A "network interface" that connects IP (the internet layer, or network layer)
// with Ethernet (the network access layer, or link layer).

// This module is the lowest layer of a TCP/IP stack
// (connecting IP with the lower-layer network protocol,
// e.g. Ethernet). But the same module is also used repeatedly
// as part of a router: a router generally has many network
// interfaces, and the router's job is to route Internet datagrams
// between the different interfaces.

// The network interface translates datagrams (coming from the
// "customer," e.g. a TCP/IP stack or router) into Ethernet
// frames. To fill in the Ethernet destination address, it looks up
// the Ethernet address of the next IP hop of each datagram, making
// requests with the [Address Resolution Protocol](\ref rfc::rfc826).
// In the opposite direction, the network interface accepts Ethernet
// frames, checks if they are intended for it, and if so, processes
// the the payload depending on its type. If it's an IPv4 datagram,
// the network interface passes it up the stack. If it's an ARP
// request or reply, the network interface processes the frame
// and learns or replies as necessary.
class NetworkInterface
{
 const static uint ARP_MAP_TIMEOUT=30000;
 const static uint ARP_REQUEST_INTERVAL=5000;
private:
  // Ethernet (known as hardware, network-access, or link-layer) address of the interface
  EthernetAddress ethernet_address_;

  // IP (known as Internet-layer or network-layer) address of the interface
  Address ip_address_;

  std::queue<EthernetFrame> ethernet_buffer;
  std::unordered_map<uint32_t,EthernetAddress> known_hosts;//记录<ip,mac> Learn mappings from both requests and replies.
  std::unordered_map<uint32_t,uint64_t> hosts_last_replay; //<ip, last_time>记录ip上次应答的时间
  std::unordered_map<uint32_t,uint64_t> arp_requests; //<ip,last_time>上次问的时间>记录上次发出arp请求询问该ip的时间, 如果5s内已经问过了, 就不要再问这个ip的mac了
  uint64_t current_time;
  std::unordered_map<uint32_t,std::queue<InternetDatagram>> unknown_hosts_ip_datagram_pending;
  void send_ipv4_frame(IPv4Datagram dgram,EthernetAddress next_hop){
    EthernetFrame eth_frame;
    eth_frame.header.src=ethernet_address_;
    eth_frame.header.dst=next_hop;
    eth_frame.header.type=EthernetHeader::TYPE_IPv4;
    eth_frame.payload=serialize(dgram);
    ethernet_buffer.push(eth_frame);
  }
  void pending_ip_datagram(IPv4Datagram dgram,uint32_t ip4){
    unknown_hosts_ip_datagram_pending[ip4].push(dgram);
  }
  void send_arp_request_frame(uint32_t ip4){
    EthernetFrame eth_frame;
    eth_frame.header.src=ethernet_address_;
    auto arp_cache=arp_requests.find(ip4);
    if((arp_cache!=arp_requests.end())&&(current_time - arp_cache->second<ARP_REQUEST_INTERVAL)){
      return;
    }
    eth_frame.header.type=EthernetHeader::TYPE_ARP;
    eth_frame.header.dst=ETHERNET_BROADCAST;
    ARPMessage arp_msg;
    arp_msg.sender_ip_address=ip_address_.ipv4_numeric();
    arp_msg.sender_ethernet_address=ethernet_address_;
    arp_msg.opcode=ARPMessage::OPCODE_REQUEST;
    arp_msg.target_ip_address=ip4;
    eth_frame.payload=serialize(arp_msg);
    ethernet_buffer.push(eth_frame);
    arp_requests[ip4]=current_time;
  }
  void send_arp_reply_frame(ARPMessage arp_message){
    ARPMessage arp_msg;
    arp_msg.sender_ip_address=ip_address_.ipv4_numeric();
    arp_msg.sender_ethernet_address=ethernet_address_;
    arp_msg.opcode=ARPMessage::OPCODE_REPLY;
    arp_msg.target_ip_address=arp_message.sender_ip_address;
    arp_msg.target_ethernet_address=arp_message.sender_ethernet_address;
    EthernetFrame eth_frame;
    eth_frame.header.type=EthernetHeader::TYPE_ARP;
    eth_frame.header.src=ethernet_address_;
    eth_frame.header.dst=arp_message.sender_ethernet_address;
    eth_frame.payload=serialize(arp_msg);
    ethernet_buffer.push(eth_frame); 
  }
  
public:
  // Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer)
  // addresses
  NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address );

  // Access queue of Ethernet frames awaiting transmission
  std::optional<EthernetFrame> maybe_send();

  // Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination
  // address). Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address
  // for the next hop.
  // ("Sending" is accomplished by making sure maybe_send() will release the frame when next called,
  // but please consider the frame sent as soon as it is generated.)
  void send_datagram( const InternetDatagram& dgram, const Address& next_hop );

  // Receives an Ethernet frame and responds appropriately.
  // If type is IPv4, returns the datagram.
  // If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
  // If type is ARP reply, learn a mapping from the "sender" fields.
  std::optional<InternetDatagram> recv_frame( const EthernetFrame& frame );

  // Called periodically when time elapses
  void tick( size_t ms_since_last_tick );
};
