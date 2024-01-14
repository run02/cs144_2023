#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <map>

//超时 重传 , 三次Ack, 重传.
class RetransmissionTimer{
  private:
    uint64_t current_time;
    uint64_t expire_time;
  public:
    RetransmissionTimer():current_time(0),expire_time(UINT64_MAX){}//2^64毫秒是一个非常大的值, 比地球年龄都大, 不用担心因为初始化的值太小了引起不必要的过期
    void refresh_time(uint64_t ms_since_last_tick){this->current_time+=ms_since_last_tick;}
    void set_current_time(uint64_t time){this->current_time=time;}
    void set_timeout(uint64_t retransmission_timeout){this->expire_time=retransmission_timeout+this->current_time;}
    void restart(uint64_t retransmission_timeout){this->set_timeout(retransmission_timeout);}
    bool is_timeout(){return this->current_time>this->expire_time;}
};
class ItemInSlidingWindow{
  public:
    ItemInSlidingWindow(TCPSenderMessage msg):
      tcp_sender_message(msg),
      acked(false),
      is_fin(false),
      retransmission_timer(RetransmissionTimer()){}
    ItemInSlidingWindow(TCPSenderMessage msg,bool fin):
      tcp_sender_message(msg),
      acked(false),
      is_fin(fin),
      retransmission_timer(RetransmissionTimer()){}
    TCPSenderMessage tcp_sender_message;
    bool acked;
    bool is_fin;
    RetransmissionTimer retransmission_timer; 
};

class SlidingWindow{
  public:
    SlidingWindow()
    : window_size(0)
    , bytes_pushed(0)
    , first_unacked_absolute_seqno(0)
    , sliding_window_items()
    {}
    uint64_t window_size;
    uint64_t bytes_pushed;
    uint64_t first_unacked_absolute_seqno;//第一个为应答的
    std::map<uint64_t,ItemInSlidingWindow> sliding_window_items;//(absolute_seqno)索引, tcp_segments, 第一个就最早未被应答的, 应答了就清除出去 
    uint64_t available_capacity(){return window_size>bytes_pushed?window_size-bytes_pushed:0;}//有可能里边还有数据呢, 但是window_size被ack变成0了
};


//状态机, 用来确认什么时候发SYN, 什么时候发FIN, 是根据收到的SYNAck从Close变成ESTABLISHED, FIN由outbound_stream.is_finish决定
enum class TCPState {
    CLOSED,
    // LISTEN,
    SYN_SENT,
    // SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    // CLOSE_WAIT,
    CLOSING,
    // LAST_ACK,
    TIME_WAIT
};

class TCPSender
{
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;

public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( uint64_t initial_RTO_ms, std::optional<Wrap32> fixed_isn );

  /* Push bytes from the outbound stream */
  void push( Reader& outbound_stream );

  /* Send a TCPSenderMessage if needed (or empty optional otherwise) */
  std::optional<TCPSenderMessage> maybe_send();

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage send_empty_message() const; //发一个只有Header,Payload为空的消息,用来让receiver来产生ack, 不需要管是不是发送成功了, 只需要管序列号正确即可

  /* Receive an act on a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called. */
  void tick( uint64_t ms_since_last_tick );

  /* Accessors for use in testing */
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?

private:
  uint64_t current_time;//当前的时间, 每隔一段时间, tick会被调用, 告诉距离上次过去了多少毫秒, 2^64毫秒是一个非常大的值, 不担心溢出
  uint64_t current_retransmission_timeout;//RTO 在window_size不是0的时候*2(因为"指数后退"), 
  uint64_t _sequence_numbers_in_flight;//outstanding sequence numbers
  uint64_t _consecutive_retransmissions_in_current_window;//当前窗口中连续重传发生的次数

  uint64_t next_absolute_sequence_number;//下一条要发送的数据的abs_seqno
  SlidingWindow sliding_window;//滑动窗口
  std::queue<uint64_t> output_queue;//给maybe_send()用的队列, 要发送了从队列里拿索引, 从滑动窗口里找

  TCPState tcp_state;
  void save_to_sliding_window_and_push_in_send_queue(TCPSenderMessage msg);
  // TCPSenderMessage to_tcp_sender_message(){TCPSenderMessage msg; msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ ); return msg;}
  TCPSenderMessage to_tcp_sender_message(std::string data,bool syn=false, bool fin=false){ TCPSenderMessage msg;msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ );msg.SYN=syn;msg.FIN =fin;msg.payload = data;return msg;}
  TCPSenderMessage to_tcp_sender_message(bool syn=false, bool fin=false){ TCPSenderMessage msg;msg.seqno = Wrap32::wrap( next_absolute_sequence_number, isn_ );msg.SYN=syn;msg.FIN =fin;return msg;}
};
