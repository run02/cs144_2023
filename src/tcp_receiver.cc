#include "tcp_receiver.hh"
#include <iostream>
using namespace std;



void TCPReceiver::receive( TCPSenderMessage message, Reassembler& reassembler, Writer& inbound_stream )
{
  if(message.SYN){//收到SYN就初始化initial_sequence_number
    initial_sequence_number=Wrap32(message.seqno);
    if(message.payload.length()>0){
      reassembler.insert(0,message.payload,message.FIN,inbound_stream);
    }
    if(message.FIN) {
      inbound_stream.close();
      return;  
    }
  }
  if(initial_sequence_number){
    uint64_t absolute_seqno=Wrap32(message.seqno).unwrap(initial_sequence_number.value(),inbound_stream.bytes_pushed());//seqno转absolute_seqno
    reassembler.insert(absolute_seqno-1,message.payload,message.FIN,inbound_stream);
  }
}

TCPReceiverMessage TCPReceiver::send( const Writer& inbound_stream ) const
{
  TCPReceiverMessage ack_message;
  if(initial_sequence_number){
     ack_message.ackno=Wrap32::wrap(inbound_stream.bytes_pushed(),initial_sequence_number.value())+1; //把absolute_seqno转为seqno, 注意, 要Ack LSR+1
     if(inbound_stream.is_closed()){
      ack_message.ackno=ack_message.ackno.value()+1;
     }
  }
  ack_message.window_size=inbound_stream.available_capacity()>65535?65535:inbound_stream.available_capacity(); //窗口
  return {ack_message};
}
