#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

using namespace std;

// ethernet_address: Ethernet (what ARP calls "hardware") address of the interface
// ip_address: IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address )
  : ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
  , ethernet_buffer()
  , known_hosts()
  , hosts_last_replay()
  , arp_requests()
  , current_time(0)
  , unknown_hosts_ip_datagram_pending()
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

// dgram: the IPv4 datagram to be sent
// next_hop: the IP address of the interface to send it to (typically a router or default gateway, but
// may also be another host if directly connected to the same network as the destination)

// Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) by using the
// Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  cout<<"NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )"<<endl;
  uint32_t ip4=next_hop.ipv4_numeric();
  auto it=known_hosts.find(ip4);
  //如果找到了, 看看是否过期, 没过期, 就发送
  if(it!=known_hosts.end()){
    //超过5s没询问了
    if(current_time -  hosts_last_replay[it->first] >= NetworkInterface::ARP_MAP_TIMEOUT){
      //每5s只发一次, 防止arp请求泛洪
      send_arp_request_frame(ip4);
      pending_ip_datagram(dgram,ip4);
    }else{
      send_ipv4_frame(dgram,it->second);
    }
  }else{//, 找不到就先存着, 发Arp请求, 等找到了再发送
    pending_ip_datagram(dgram,ip4);
    send_arp_request_frame(ip4);
  }
}

// frame: the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  cout<<"NetworkInterface::recv_frame( const EthernetFrame& frame )"<<endl;
  //如果是ipv4的数据包就发给调用的, 如果是Arp就处理Aro请求
  if(frame.header.type==EthernetHeader::TYPE_IPv4&&frame.header.dst==ethernet_address_){
    InternetDatagram ipv4_frame;
    if (parse(ipv4_frame,frame.payload)){
       return ipv4_frame;
    }
  }else if(frame.header.type==EthernetHeader::TYPE_ARP){//Learn mappings from both requests and replies.
    ARPMessage arp_message;
    if (parse(arp_message,frame.payload)){
      if(arp_message.opcode==ARPMessage::OPCODE_REPLY){
        known_hosts[arp_message.sender_ip_address]=arp_message.sender_ethernet_address;
        hosts_last_replay[arp_message.sender_ip_address]=current_time;
        auto pending= unknown_hosts_ip_datagram_pending.find(arp_message.sender_ip_address);
          if(pending!=unknown_hosts_ip_datagram_pending.end()){
            while(!pending->second.empty()){
              send_ipv4_frame(pending->second.front(),arp_message.sender_ethernet_address);
              pending->second.pop();
            }
          }
      }else if(arp_message.opcode==ARPMessage::OPCODE_REQUEST){//如果是询问本机的mac地址和ip, 就应答
        known_hosts[arp_message.sender_ip_address]=arp_message.sender_ethernet_address;
        hosts_last_replay[arp_message.sender_ip_address]=current_time;
        if(arp_message.target_ip_address==ip_address_.ipv4_numeric()){
          send_arp_reply_frame(arp_message);
        }
      }  
    }
  }
  return {};
}

// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  current_time+=ms_since_last_tick;
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if(!ethernet_buffer.empty()){
    auto ethernet_frame=ethernet_buffer.front();
    ethernet_buffer.pop();
    return ethernet_frame;
  }
  return {};
}
