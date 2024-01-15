#include "tcp_sender.hh"
#include "tcp_config.hh"

#include <random>

using namespace std;

// 定义一个开关宏，用于控制是否启用打印
// #define ENABLE_DEBUG_PRINT 1
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
  , fin_sent(false)
  , biggest_previous_ackno(0)
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
      PRINT_INFO("message sent");
      PRINT_WITH_LABEL("current_time",current_time);
      PRINT_WITH_LABEL("timeout",current_time+current_retransmission_timeout);
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
  //  sliding_window.bytes_pushed+=msg.sequence_length(); fin出现占用一个window的情况在已经在push中额外考虑了, 所以是加data.length没错
  PRINT_WITH_LABEL( "insert_absolute_sequence_number", next_absolute_sequence_number );
}

void TCPSender::push( Reader& outbound_stream ){
    PRINT_INFO( "---> Entering TCPSender::push" );
    if(fin_sent) return;
    if ( tcp_state == TCPState::CLOSED ) {
      if(outbound_stream.is_finished()){//SYN + FIN
          save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(true,true));
          fin_sent=true;
      }else{
          save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(true));
          tcp_state=TCPState::SYN_SENT;
          PRINT_INFO( "Leaving TCPSender::push in SYN_SENT state" );
      }
     
    } else if ( tcp_state == TCPState::ESTABLISHED ) {
      PRINT_INFO( "TCP State: ESTABLISHED" );
      if ( sliding_window.window_size > 0 ) {
        PRINT_INFO( "Window size is non-zero" );
        while ( ( outbound_stream.bytes_buffered() > 0 ) && (sliding_window.available_capacity()>0) ) {
          std::string data( outbound_stream.peek() );
          PRINT_WITH_LABEL( "Data Peeked length", data.length() );
          if ( data.length() > std::min( TCPConfig::MAX_PAYLOAD_SIZE, sliding_window.available_capacity() ) ) {
            data = data.substr( 0, std::min( TCPConfig::MAX_PAYLOAD_SIZE, sliding_window.available_capacity() ) );
            PRINT_WITH_LABEL( "Data Truncated", data.length()  );
          }
          TCPSenderMessage msg=to_tcp_sender_message(data);
          outbound_stream.pop( data.length() );
          if(outbound_stream.is_finished()){
            PRINT_INFO("Outbound stream finished");
            msg.FIN=true;
            if(sliding_window.available_capacity()>=msg.sequence_length()){
              fin_sent=true;
            }else{
              msg.FIN=false;
              PRINT_INFO("Outbound stream finished, but FIN not sent yet due to no available capacity in sliding window.");
            }
          }
          save_to_sliding_window_and_push_in_send_queue(msg);
         
          PRINT_WITH_LABEL( "Bytes Popped", data.length() );
          PRINT_WITH_LABEL( "sliding window available_capacity", sliding_window.available_capacity());
        }
        if ((outbound_stream.is_finished())&&(!fin_sent)) {//fin也算是一个byte
          PRINT_INFO("fin_sent");
          if(sliding_window.available_capacity()>0){
            PRINT_INFO( "Outbound stream finished, sending FIN" );
            save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(false,true));
            fin_sent=true;
            PRINT_INFO( "FIN message queued" );
            }
        }
        
      } else {//窗口是0当作窗口是1
        if(sequence_numbers_in_flight()>0){//如果已经发送出去了至少是1的数据了, 就不用再发了, 超时会重传的
          return;
        }
        if(outbound_stream.bytes_buffered()>0){
          PRINT_INFO( "Window size is zero, attempting to send one byte to trigger ACK" );
          std::string data( outbound_stream.peek().substr( 0, 1 ) );
          PRINT_WITH_LABEL( "Data Peeked", data );
          save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(data));      
          outbound_stream.pop( data.length() );
          PRINT_WITH_LABEL( "Bytes Popped", data.length() );
        }else if(outbound_stream.is_finished()){
          save_to_sliding_window_and_push_in_send_queue(to_tcp_sender_message(false,true));
        }
      }
      PRINT_INFO( "Leaving TCPSender::push in ESTABLISHED state" );
    } else {
      PRINT_INFO( "TCP State: Not in CLOSED or ESTABLISHED" );
    }
    
   
    PRINT_INFO( "<------Exiting TCPSender::push" );
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ );
  return { msg };
}

void TCPSender::receive(const TCPReceiverMessage& msg) {
    PRINT_INFO("===>>Entering TCPSender::receive");
    sliding_window.window_size = msg.window_size; 
    PRINT_WITH_LABEL("Updated Window Size", sliding_window.window_size);
    if(!msg.ackno){return;}
    uint64_t abs_seqno = Wrap32(msg.ackno.value()).unwrap(isn_, next_absolute_sequence_number);
    /*找到问题了, receiver可能会一次ack 1个, 两个, n个, 或者是窗口的全部, 也有可能是窗口中的左半部分, 得先往左看, 消除左边所有的, 然后再往右看? 想不明白了, 先睡觉, 明天再说*/
    PRINT_WITH_LABEL("Absolute Sequence Number", abs_seqno);
    {
          auto it = sliding_window.sliding_window_items.find(abs_seqno);
          PRINT_INFO("Checking for received ack in sliding window items");
          if(it!=sliding_window.sliding_window_items.end()){
              PRINT_WITH_LABEL("Found Sequence Number in Sliding Window", it->first);
              auto p=sliding_window.sliding_window_items.begin();
              while((p!=sliding_window.sliding_window_items.end())&&(p->first <= it->first)) {
                  PRINT_WITH_LABEL("Checking left Sequence Number in Sliding Window", p->first);
                  if (p->second.acked == false) {
                      p->second.acked = true;
                      _sequence_numbers_in_flight -= p->second.tcp_sender_message.sequence_length();
                      PRINT_INFO("Marking Sequence Number as Acked and Decreasing _sequence_numbers_in_flight");
                  }
                  if (p->first == 1) {
                      tcp_state = TCPState::ESTABLISHED;
                      PRINT_INFO("TCP State set to ESTABLISHED");
                  } 
                  p++;
              }
          }
          
    }
    if (abs_seqno > biggest_previous_ackno) {
        biggest_previous_ackno=abs_seqno;
        PRINT_INFO("Ackno matches next_absolute_sequence_number, resetting RTO and retransmission counts");
        current_retransmission_timeout = initial_RTO_ms_;
        auto it = sliding_window.sliding_window_items.begin(); 
        _consecutive_retransmissions_in_current_window = 0;
        while (it != sliding_window.sliding_window_items.end()) {
            auto p=it++; 
            if ((p->second.acked == false)) {//找到oudstanding data, 重置时钟
                p->second.retransmission_timer = RetransmissionTimer();
                p->second.retransmission_timer.set_current_time( current_time );
                p->second.retransmission_timer.set_timeout( current_retransmission_timeout );
                PRINT_WITH_LABEL("Resetting Retransmission Timer for Sequence Number", p->first);
            }else{//找到被ack过的, 释放占用的sliding_window的内存和状态
              sliding_window.bytes_pushed-=p->second.tcp_sender_message.payload.length(); 
              sliding_window.sliding_window_items.erase(p);
            }
        }
    }

  PRINT_INFO("<<===Exiting TCPSender::receive");
}


void TCPSender::tick(const size_t ms_since_last_tick) {
    // PRINT_INFO("Entering TCPSender::tick");
    // PRINT_WITH_LABEL("ms_since_last_tick", ms_since_last_tick);

    current_time += ms_since_last_tick; // 更新时间
    // PRINT_WITH_LABEL("Updated current_time", current_time);

    bool find_first_timeout = false;
    auto it = sliding_window.sliding_window_items.begin();
    while (it != sliding_window.sliding_window_items.end()) {
        it->second.retransmission_timer.set_current_time(current_time); // 更新定时器的时间
        // PRINT_WITH_LABEL("Set current time for retransmission timer of seqno", it->first);

        if ((it->second.acked == false )&& (it->second.retransmission_timer.is_timeout())) {
            _consecutive_retransmissions_in_current_window++;
            // PRINT_WITH_LABEL("Packet timeout detected for seqno", it->first);
            // PRINT_WITH_LABEL("Consecutive retransmissions count", _consecutive_retransmissions_in_current_window);
        }
        if ((find_first_timeout == false)&&(it->second.acked == false)) {
          find_first_timeout = true;
          if(it->second.retransmission_timer.is_timeout()){
            output_queue.push(it->first);
            if(sliding_window.window_size>0){
                current_retransmission_timeout += current_retransmission_timeout;
            }else if(tcp_state==TCPState::SYN_SENT){//啊? 测试用例认为一开始应该认为窗口>0? ...... 额...
              current_retransmission_timeout += current_retransmission_timeout;
            }
          }
          // PRINT_WITH_LABEL("First timeout packet, pushed to output queue seqno", it->first);          
          // PRINT_WITH_LABEL("Exponential backoff, new RTO", current_retransmission_timeout);
        }
        it++;
    }

    // PRINT_INFO("Exiting TCPSender::tick");
}
