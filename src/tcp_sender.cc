#include "tcp_sender.hh"
#include "tcp_config.hh"

#include <random>

using namespace std;

// 定义一个开关宏，用于控制是否启用打印
#define ENABLE_DEBUG_PRINT 1

#ifdef ENABLE_DEBUG_PRINT
#include <iostream> // 在开关宏中引入iostream头文件
// 打印文字
#define PRINT_TEXT( text )                                                                                         \
  do {                                                                                                             \
    if ( ENABLE_DEBUG_PRINT ) {                                                                                    \
      std::cout << text << std::endl;                                                                              \
    }                                                                                                              \
  } while ( 0 )

// 打印标签+文字
#define PRINT_WITH_LABEL( label, text )                                                                            \
  do {                                                                                                             \
    if ( ENABLE_DEBUG_PRINT ) {                                                                                    \
      std::cout << "<" << label << ">"                                                                             \
                << ": " << text << std::endl;                                                                      \
    }                                                                                                              \
  } while ( 0 )

// 打印INFO
#define PRINT_INFO( text )                                                                                         \
  do {                                                                                                             \
    if ( ENABLE_DEBUG_PRINT ) {                                                                                    \
      std::cout << "[INFO]: " << text << std::endl;                                                                \
    }                                                                                                              \
  } while ( 0 )
#else
#define PRINT_TEXT( text )
#define PRINT_WITH_LABEL( label, text )
#define PRINT_INFO( text )
#endif

/* TCPSender constructor (uses a random ISN if none given) */
TCPSender::TCPSender( uint64_t initial_RTO_ms, optional<Wrap32> fixed_isn )
  : isn_( fixed_isn.value_or( Wrap32 { random_device()() } ) )
  , initial_RTO_ms_( initial_RTO_ms )
  , current_time( 0 )
  , current_retransmission_timeout( initial_RTO_ms )
  , _sequence_numbers_in_flight( 0 )
  , _consecutive_retransmissions_in_current_window( 0 )
  , next_absolute_sequence_number( 0 )
  , sliding_window()
  , output_queue()
  , tcp_state( TCPState::CLOSED )
{
 PRINT_TEXT("\nInitial TcpSender");
}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return _sequence_numbers_in_flight;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return _consecutive_retransmissions_in_current_window;
}

optional<TCPSenderMessage> TCPSender::maybe_send()
{
  if ( !output_queue.empty() ) {
    auto absolute_seqno = output_queue.front();
    output_queue.pop();
    auto it = sliding_window.sliding_window_items.find( absolute_seqno );
    if ( it != sliding_window.sliding_window_items.end() ) {
      // 开始定时器
      it->second.retransmission_timer.set_current_time( current_time );
      it->second.retransmission_timer.set_timeout( current_retransmission_timeout );
      return it->second.tcp_sender_message;
    } else {
      PRINT_TEXT( "滑动窗口中不应该出现找不到对应的abs_seqno的情况" );
    }
  }
  return {};
}

// void TCPSender::push( Reader& outbound_stream )
// {
//     if(tcp_state==TCPState::CLOSED){
//         TCPSenderMessage msg;
//         msg.seqno=Wrap32::wrap(next_absolute_sequence_number,isn_);
//         msg.SYN=true;
//         sliding_window.sliding_window_items.insert(std::make_pair(next_absolute_sequence_number,ItemInSlidingWindow(msg)));
//         output_queue.push(next_absolute_sequence_number);
//         _sequence_numbers_in_flight+=msg.sequence_length();
//         next_absolute_sequence_number+=1;
//         sliding_window.bytes_pushed++;//SYN和FIN也算
//     }else if(tcp_state==TCPState::ESTABLISHED){
//           //处理window_size_none_zero的情况, payload有最大数量限制, 把不超过payload最大数量限制的弄到滑动窗口里,
//           再尝试"发送", 就是防在队列里等待调用
//         if(sliding_window.window_size>0){
//           //窗口太大了就分开装
//           uint64_t counter=0;
//           while ((outbound_stream.bytes_buffered()>0)&&(counter<sliding_window.available_capacity())){
//             std::string data(outbound_stream.peek());
//             if(data.length()>std::min(TCPConfig::MAX_PAYLOAD_SIZE,sliding_window.available_capacity())){
//               data=data.substr(0,std::min(TCPConfig::MAX_PAYLOAD_SIZE,sliding_window.available_capacity()));
//             }
//             TCPSenderMessage msg;
//             msg.seqno=Wrap32::wrap(next_absolute_sequence_number,isn_);
//             msg.payload=data;

//             //加入到滑动窗口, 然后"发送"
//             sliding_window.sliding_window_items.insert(std::make_pair(next_absolute_sequence_number,ItemInSlidingWindow(msg)));
//             output_queue.push(next_absolute_sequence_number);
//             _sequence_numbers_in_flight+=msg.sequence_length();
//             //算出下一次的abs_seqno
//             next_absolute_sequence_number+=msg.sequence_length();

//             counter+=data.length();
//             sliding_window.bytes_pushed+=data.length();
//             outbound_stream.pop(data.length());
//           }
//           //如果发送的流发送完了, 就关闭
//           if(outbound_stream.is_finished()){
//             TCPSenderMessage msg;
//             msg.seqno=Wrap32::wrap(next_absolute_sequence_number,isn_);
//             msg.FIN=true;
//             sliding_window.sliding_window_items.insert(std::make_pair(next_absolute_sequence_number,ItemInSlidingWindow(msg,true)));
//             output_queue.push(next_absolute_sequence_number);
//             _sequence_numbers_in_flight+=msg.sequence_length();
//             next_absolute_sequence_number+=1;//按理来讲已经没必要再加了
//             sliding_window.bytes_pushed++;//SYN和FIN也算
//           }
//       }else{//如果窗口为0, 也要尝试认为窗口大小是1, 发一个byte的内容, 用来引发ack, 好知道什么时候"活了"
//           std::string data(outbound_stream.peek());
//           data=data.substr(0,1);//取一个字节
//           TCPSenderMessage msg;
//           msg.seqno=Wrap32::wrap(next_absolute_sequence_number,isn_);
//           msg.payload=data;

//           //加入到滑动窗口, 然后"发送"
//           sliding_window.sliding_window_items.insert(std::make_pair(next_absolute_sequence_number,ItemInSlidingWindow(msg)));
//           output_queue.push(next_absolute_sequence_number);
//           _sequence_numbers_in_flight+=msg.sequence_length();
//           //算出下一次的abs_seqno
//           next_absolute_sequence_number+=msg.sequence_length();
//           outbound_stream.pop(data.length());
//       }
//   }

// }
void TCPSender::push( Reader& outbound_stream )
{
  PRINT_INFO( "Entering TCPSender::push" );

  if ( tcp_state == TCPState::CLOSED ) {
    PRINT_INFO( "TCP State: CLOSED" );

    TCPSenderMessage msg;
    msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ );
    
    msg.SYN = true;
    PRINT_INFO( "SYN flag set" );
    next_absolute_sequence_number += 1;
    sliding_window.sliding_window_items.insert(
      std::make_pair( next_absolute_sequence_number, ItemInSlidingWindow( msg ) ) );
    output_queue.push( next_absolute_sequence_number );
    PRINT_WITH_LABEL( "Queue Push", next_absolute_sequence_number );
    _sequence_numbers_in_flight+=msg.sequence_length();
 
    sliding_window.bytes_pushed++; // SYN and FIN also count
    tcp_state=TCPState::SYN_SENT;
    PRINT_INFO( "Leaving TCPSender::push in SYN_SENT state" );
  } else if ( tcp_state == TCPState::ESTABLISHED ) {
    PRINT_INFO( "TCP State: ESTABLISHED" );

    if ( sliding_window.window_size > 0 ) {
      PRINT_INFO( "Window size is non-zero" );
      uint64_t counter = 0;

      while ( ( outbound_stream.bytes_buffered() > 0 ) && ( counter < sliding_window.available_capacity() ) ) {
        std::string data( outbound_stream.peek() );
        PRINT_WITH_LABEL( "Data Peeked", data );

        if ( data.length() > std::min( TCPConfig::MAX_PAYLOAD_SIZE, sliding_window.available_capacity() ) ) {
          data = data.substr( 0, std::min( TCPConfig::MAX_PAYLOAD_SIZE, sliding_window.available_capacity() ) );
          PRINT_WITH_LABEL( "Data Truncated", data );
        }

        TCPSenderMessage msg;
        msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ );
        msg.payload = data;
 
        next_absolute_sequence_number += msg.sequence_length();//为了方便ack, 存的, 这样根据ackno-1, 就找到了
 
        sliding_window.sliding_window_items.insert(
          std::make_pair( next_absolute_sequence_number, ItemInSlidingWindow( msg ) ) );
        output_queue.push( next_absolute_sequence_number );
        PRINT_WITH_LABEL( "Queue Push", next_absolute_sequence_number );
        _sequence_numbers_in_flight+=msg.sequence_length();
   
        counter += data.length();
        sliding_window.bytes_pushed += data.length();
        outbound_stream.pop( data.length() );
        PRINT_WITH_LABEL( "Bytes Popped", data.length() );
      }

      if ( outbound_stream.is_finished() ) {
        PRINT_INFO( "Outbound stream finished, sending FIN" );
        TCPSenderMessage msg;
        msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ );
        msg.FIN = true;
        next_absolute_sequence_number += 1;
        sliding_window.sliding_window_items.insert(
          std::make_pair( next_absolute_sequence_number, ItemInSlidingWindow( msg, true ) ) );
        output_queue.push( next_absolute_sequence_number );
        _sequence_numbers_in_flight+=msg.sequence_length();
    
        sliding_window.bytes_pushed++;
        PRINT_INFO( "FIN message queued" );
      }
    } else {
      PRINT_INFO( "Window size is zero, attempting to send one byte to trigger ACK" );
      std::string data( outbound_stream.peek().substr( 0, 1 ) );
      PRINT_WITH_LABEL( "Data Peeked", data );

      TCPSenderMessage msg;
      msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ );
      msg.payload = data;
      // PRINT_WITH_LABEL( "SeqNo", msg.seqno.value() );
      // PRINT_WITH_LABEL( "Payload", msg.payload );
      // Add to sliding window and "send"
      next_absolute_sequence_number += msg.sequence_length();
      sliding_window.sliding_window_items.insert(
        std::make_pair( next_absolute_sequence_number, ItemInSlidingWindow( msg ) ) );
      output_queue.push( next_absolute_sequence_number );
      PRINT_WITH_LABEL( "Queue Push", next_absolute_sequence_number );
      _sequence_numbers_in_flight+=msg.sequence_length();
      
      outbound_stream.pop( data.length() );
      PRINT_WITH_LABEL( "Bytes Popped", data.length() );
    }

    PRINT_INFO( "Leaving TCPSender::push in ESTABLISHED state" );
  } else {
    PRINT_INFO( "TCP State: Not in CLOSED or ESTABLISHED" );
  }

  PRINT_INFO( "Exiting TCPSender::push" );
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ );
  return { msg };
}

// void TCPSender::receive( const TCPReceiverMessage& msg )
// {
//   sliding_window.window_size = msg.window_size; // 更新窗口
//   // 主要是看哪个被ack了
//   uint64_t abs_seqno = Wrap32( msg.ackno.value() ).unwrap( isn_, next_absolute_sequence_number );
//   if ( abs_seqno == 0 ) {
//     return;
//   } else {
//     abs_seqno -= 1;
//   }
//   /*
//   When the receiver gives the sender an ackno that acknowledges the successful receipt
//   of new data (the ackno reflects an absolute sequence number bigger than any previous
//   ackno):
//     (a) Set the RTO back to its “initial value.”
//     (b) If the sender has any outstanding data, restart the retransmission timer so that it
//     will expire after RTO milliseconds (for the current value of RTO).
//     (c) Reset the count of “consecutive retransmissions” back to zero.
//   */
//   if ( abs_seqno == next_absolute_sequence_number ) {
//     current_retransmission_timeout = initial_RTO_ms_;
//     auto p = sliding_window.sliding_window_items.begin();
//     p++;
//     _consecutive_retransmissions_in_current_window = 0;
//     while ( p != sliding_window.sliding_window_items.end() ) {
//       if ( p->second.acked == false ) {
//         p->second.retransmission_timer = RetransmissionTimer();
//       }
//     }
//   }
//   auto it = sliding_window.sliding_window_items.find( abs_seqno );
//   // 如果是"正确"的应答
//   if ( it != sliding_window.sliding_window_items.end() ) {
//     if ( it->second.acked == false ) {
//       it->second.acked = true;
//       _sequence_numbers_in_flight--;
//     }
//     // 如果是SynAck
//     if ( it->first == 0 ) {
//       tcp_state = TCPState::ESTABLISHED;
//     } else { // 剩下的就是数据的ack和FinAck了
//       // 处理finAck
//       if ( it->second.is_fin ) {
//         tcp_state = TCPState::CLOSED;
//       }
//       // 看看有没有连起来的ack, 有的话就清除掉占用的空间, 尝试向右边滑动窗口
//       if ( it->first == sliding_window.first_unacked_absolute_seqno ) {
//         do {
//           sliding_window.bytes_pushed -= it->second.tcp_sender_message.sequence_length(); // 清除占用的空间
//           sliding_window.first_unacked_absolute_seqno += it->second.tcp_sender_message.sequence_length();
//           sliding_window.sliding_window_items.erase( it );
//           it = sliding_window.sliding_window_items.find( sliding_window.first_unacked_absolute_seqno );
//         } while ( ( it != sliding_window.sliding_window_items.end() ) && ( it->second.acked ) );
//       }
//     }
//   }
// }
void TCPSender::receive(const TCPReceiverMessage& msg) {
    PRINT_INFO("Entering TCPSender::receive");

    sliding_window.window_size = msg.window_size; // 更新窗口
    PRINT_WITH_LABEL("Updated Window Size", sliding_window.window_size);

    uint64_t abs_seqno = Wrap32(msg.ackno.value()).unwrap(isn_, next_absolute_sequence_number);
    PRINT_WITH_LABEL("Absolute Sequence Number", abs_seqno);

    // if (abs_seqno == 0) {
    //     PRINT_INFO("Received Absolute Sequence Number is 0, returning from function");
    //     return;
    // } else {
    //     abs_seqno -= 1;
    //     PRINT_WITH_LABEL("Adjusted Absolute Sequence Number", abs_seqno);
    // }

    if (abs_seqno == next_absolute_sequence_number) {
        PRINT_INFO("Ackno matches next_absolute_sequence_number, resetting RTO and retransmission counts");
        current_retransmission_timeout = initial_RTO_ms_;
        auto p = sliding_window.sliding_window_items.begin();
        p++;
        _consecutive_retransmissions_in_current_window = 0;

        while (p != sliding_window.sliding_window_items.end()) {
            if (p->second.acked == false) {
                p->second.retransmission_timer = RetransmissionTimer();
                PRINT_WITH_LABEL("Resetting Retransmission Timer for Sequence Number", p->first);
            }
            p++;
        }
    }

    auto it = sliding_window.sliding_window_items.find(abs_seqno);
    PRINT_INFO("Checking for received ack in sliding window items");

    if (it != sliding_window.sliding_window_items.end()) {
        PRINT_WITH_LABEL("Found Sequence Number in Sliding Window", it->first);

        if (it->second.acked == false) {
            it->second.acked = true;
            _sequence_numbers_in_flight-=it->second.tcp_sender_message.sequence_length();
            PRINT_INFO("Marking Sequence Number as Acked and Decreasing _sequence_numbers_in_flight");
        }
  
        if (it->first == 1) {
            tcp_state = TCPState::ESTABLISHED;

            PRINT_INFO("TCP State set to ESTABLISHED");
        } else {
            if (it->second.is_fin) {
                tcp_state = TCPState::CLOSED;
                PRINT_INFO("TCP State set to CLOSED due to FIN");
            }

            if (it->first == sliding_window.first_unacked_absolute_seqno) {
                PRINT_INFO("Processing contiguous acks");
                    do {
                        sliding_window.bytes_pushed -= it->second.tcp_sender_message.sequence_length();
                        sliding_window.first_unacked_absolute_seqno += it->second.tcp_sender_message.sequence_length();
                        sliding_window.sliding_window_items.erase(it);
                        PRINT_WITH_LABEL("Erased and Moved sliding window for Sequence Number", it->first);
                        it = sliding_window.sliding_window_items.find(sliding_window.first_unacked_absolute_seqno);
                        PRINT_WITH_LABEL("Looking for next contiguous ack", sliding_window.first_unacked_absolute_seqno);
                      } while ((it != sliding_window.sliding_window_items.end()) && (it->second.acked));
            }
        } 
    }else {
      PRINT_WITH_LABEL("No entry found in sliding window for Sequence Number", abs_seqno);
    }

  PRINT_INFO("Exiting TCPSender::receive");
}


void TCPSender::tick( const size_t ms_since_last_tick )
{
  current_time += ms_since_last_tick; // 更新时间
  // 检查超时丢包, 记录丢包数量, 尝试重发
  // If tick is called and the retransmission timer has expired Retransmit the earliest
  auto it = sliding_window.sliding_window_items.find( sliding_window.first_unacked_absolute_seqno );
  if ( it != sliding_window.sliding_window_items.end() ) {
    it->second.retransmission_timer.set_current_time( current_time ); // 更新定时器的时间
    if ( it->second.acked == false && it->second.retransmission_timer.is_timeout() ) {
      output_queue.push( it->first );

      // i. Keep track of the number of consecutive retransmissions
      it++ = sliding_window.sliding_window_items.begin();
      while ( it != sliding_window.sliding_window_items.end() ) {
        it->second.retransmission_timer.set_current_time( current_time ); // 更新定时器的时间
        if ( it->second.acked == false && it->second.retransmission_timer.is_timeout() ) {
          _consecutive_retransmissions_in_current_window++;
        }
      }

      // ii. Double the value of RTO. This is called “exponential backoff”
      current_retransmission_timeout += current_retransmission_timeout;
    }
  }
}
