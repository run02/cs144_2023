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

void TCPSender::save_to_sliding_window_and_push_in_send_queue(TCPSenderMessage msg){
  next_absolute_sequence_number += msg.sequence_length();//为了方便ack, 存的, 这样根据ackno, 就找到了
  sliding_window.sliding_window_items.insert(std::make_pair( next_absolute_sequence_number, ItemInSlidingWindow( msg ) ) );
  output_queue.push( next_absolute_sequence_number );
  _sequence_numbers_in_flight+=msg.sequence_length();
  sliding_window.bytes_pushed+=msg.payload.length(); 
  PRINT_WITH_LABEL( "next_absolute_sequence_number", next_absolute_sequence_number );
}

void TCPSender::push( Reader& outbound_stream ){
    PRINT_INFO( "Entering TCPSender::push" );
    if ( tcp_state == TCPState::CLOSED ) {
      save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(true));
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
          save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(data));
          counter += data.length();
          outbound_stream.pop( data.length() );
          PRINT_WITH_LABEL( "Bytes Popped", data.length() );
        }

        if ( outbound_stream.is_finished() ) {
          PRINT_INFO( "Outbound stream finished, sending FIN" );
          save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(false,true));
          PRINT_INFO( "FIN message queued" );
        }
      } else {
        PRINT_INFO( "Window size is zero, attempting to send one byte to trigger ACK" );
        std::string data( outbound_stream.peek().substr( 0, 1 ) );
        PRINT_WITH_LABEL( "Data Peeked", data );
        save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(data));      
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

void TCPSender::receive(const TCPReceiverMessage& msg) {
    PRINT_INFO("Entering TCPSender::receive");
    sliding_window.window_size = msg.window_size; 
    PRINT_WITH_LABEL("Updated Window Size", sliding_window.window_size);
    uint64_t abs_seqno = Wrap32(msg.ackno.value()).unwrap(isn_, next_absolute_sequence_number);
    PRINT_WITH_LABEL("Absolute Sequence Number", abs_seqno);
    {
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
              } 
              if (it->second.is_fin) {
                  tcp_state = TCPState::CLOSED;
                  PRINT_INFO("TCP State set to CLOSED due to FIN");
              }
          }else {
            PRINT_WITH_LABEL("No entry found in sliding window for Sequence Number", abs_seqno);
          }
    }
    if (abs_seqno == next_absolute_sequence_number) {
        PRINT_INFO("Ackno matches next_absolute_sequence_number, resetting RTO and retransmission counts");
        current_retransmission_timeout = initial_RTO_ms_;
        auto it = sliding_window.sliding_window_items.begin(); it++;
        _consecutive_retransmissions_in_current_window = 0;
        while (it != sliding_window.sliding_window_items.end()) {
            auto p=it; 
            ++it;
            if ((p->second.acked == false) && (p->second.retransmission_timer.is_timeout())) {//找到超时的, 重置时钟
                p->second.retransmission_timer = RetransmissionTimer();
                PRINT_WITH_LABEL("Resetting Retransmission Timer for Sequence Number", p->first);
            }else{//找到被ack过的, 释放占用的sliding_window的内存和状态
              sliding_window.bytes_pushed-=p->second.tcp_sender_message.payload.length(); 
              sliding_window.sliding_window_items.erase(p);
            }
        }
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
