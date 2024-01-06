#include "tcp_receiver.hh"
#include <iostream>
using namespace std;



void TCPReceiver::receive( TCPSenderMessage message, Reassembler& reassembler, Writer& inbound_stream )
{
  cout << "Receiving message. SYN: " << message.SYN << ", FIN: " << message.FIN << endl;
  
  if(message.SYN&&!establish){//收到SYN就初始化initial_sequence_number
    initial_sequence_number=Wrap32(message.seqno);
    establish=true;
    syn_=true;
  }else{
    syn_=false;
  }

  if(establish){
    uint64_t absolute_seqno=Wrap32(message.seqno).unwrap(initial_sequence_number,inbound_stream.bytes_pushed());//seqno转absolute_seqno
    cout << "Converted SeqNo to Absolute SeqNo: " << absolute_seqno << endl;
    if(syn_&&message.payload.length()>0){
      reassembler.insert(absolute_seqno,message.payload,message.FIN,inbound_stream);
    }else{
      reassembler.insert(absolute_seqno-1,message.payload,message.FIN,inbound_stream);
      cout << "Inserted into Reassembler. Bytes pushed: " << inbound_stream.bytes_pushed() << endl;
    }
    if(message.FIN){
      if(reassembler.bytes_pending()==0){
        fin_=true;
        inbound_stream.close();
        cout << "Finished. Stream closed." << endl;
      }
    }
  }  
}

TCPReceiverMessage TCPReceiver::send( const Writer& inbound_stream ) const
{
  TCPReceiverMessage ack_message;
  cout<<"Inside send. Current SYN: "<<syn_<<" and FIN: "<<fin_<<endl;
  if(establish){
    if(syn_){
      ack_message.ackno=initial_sequence_number+1;
    }
    ack_message.ackno=Wrap32::wrap(inbound_stream.bytes_pushed(),initial_sequence_number)+1; //把absolute_seqno转为seqno, 注意, 要Ack LSR+1
    if(fin_){
      *ack_message.ackno=*ack_message.ackno+1;
    }
  }else{
    cout<<"not eastablished."<<endl;
  }
  ack_message.window_size=inbound_stream.available_capacity()>65535?65535:inbound_stream.available_capacity(); //窗口
  cout << "Setting window size: " << ack_message.window_size << endl;
  return {ack_message};
}
