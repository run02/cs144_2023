#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";
    routing_table.push_back(ItemInRouterTable(route_prefix,prefix_length,next_hop,interface_num));
}



void Router::route() {
    auto it = routing_table.begin();
    while (it != routing_table.end()) {
        auto datagram = interface(it->interface_num).maybe_receive();
        if (!datagram) {
            it++;
            continue;
        }
        if (datagram.value().header.ttl > 1) {
            uint8_t current_max_match_prefix = 0;
            std::optional<Address> current_next_hop;
            uint8_t send_out_interface_num=0;
            for (auto interface_item : routing_table) {
                uint32_t x,r; 
                if(interface_item.prefix_length==0){
                  x=0;//runtime error: shift exponent 32 is too large for 32-bit type 'unsigned int'
                  r=interface_item.route_prefix;
                }else{
                  x= datagram.value().header.dst & (UINT32_MAX << (32-interface_item.prefix_length));
                  r=interface_item.route_prefix & (UINT32_MAX << (32-interface_item.prefix_length));
                }
                // std::cout <<"-x: "<<Address::from_ipv4_numeric(x).ip()<<std::endl;
                // std::cout <<"-route prefix:: "<<Address::from_ipv4_numeric(r).ip()<<std::endl;
                if (x == r) {
                    // std::cout <<"x: "<<Address::from_ipv4_numeric(x).ip()<<std::endl;
                    // std::cout <<"Found matching route prefix: "<<Address::from_ipv4_numeric(interface_item.route_prefix).ip()<<std::endl;
                    if (interface_item.prefix_length >= current_max_match_prefix) {
                      if(interface_item.next_hop){
                        current_next_hop =  interface_item.next_hop;
                      }else{
                        current_next_hop =  Address::from_ipv4_numeric(datagram.value().header.dst);
                      }
                      // std::cout << "Updating current next_hop to: " << current_next_hop .value().ip() << std::endl;
                      current_max_match_prefix = interface_item.prefix_length;
                      send_out_interface_num=interface_item.interface_num;
                  }
                } 
            }
            if (current_next_hop) {
                // std::cout << "Routing to: " << current_next_hop.value().ip() << std::endl; // 打印匹配的接口号
                // std::cout << "Routing to interface: " << int(send_out_interface_num) << std::endl; // 打印匹配的接口号
                InternetDatagram ipdgram;
                datagram.value().header.ttl--;
                datagram.value().header.compute_checksum();//别忘了改变了Header之后也要改变checksum!!!
                // std::cout << "ttl: " << int(datagram.value().header.ttl) << std::endl; // 打印匹配的接口号
                interface(send_out_interface_num).send_datagram(datagram.value(), current_next_hop.value());
            } else {
                // std::cout << "wtf? No matched interface found." << std::endl; // 除非没有0.0.0.0/0, 不然应该没有找到,
            }
        } else {
            // std::cout << "Datagram TTL too low." << std::endl; // 数据报TTL过低
        }
        it++;
    }
}
