#include "reassembler.hh"
#include <iostream> // 确保包含了iostream

using namespace std;

Reassembler::Reassembler()
  : pending_buffer()
  , receive_window_size( 0 )
  ,
  // last_acceptable_bytes(0),
  last_bytes_received( 0 )
  , last_ordered_bytes( 0 )
  , _bytes_pending( 0 )
  , finish_flag( false )
  , index_of_end( 0 )
{}

void Reassembler::auto_push( uint64_t first_index, Writer& output, std::string current_data )
{
  // std::cout << "+++Auto-push called with first_index: " << first_index << ", current_data size: " <<
  // current_data.size() << std::endl;

  cout << "[AUTO PUSH] Start ==> | First index: " << first_index << ", Data size: " << current_data.size() << endl;

  uint64_t pushed_buffer_size = current_data.length();
  output.push( current_data );
  // std::cout << "Pushed initial data segment. Size: " << pushed_buffer_size << std::endl;
  last_ordered_bytes = first_index + pushed_buffer_size;
  // std::cout << "Last ordered: " << last_ordered_bytes << std::endl;

  uint64_t index = last_ordered_bytes;
  auto it = pending_buffer.find( index );

  while ( it != pending_buffer.end() ) {
    // std::cout << "Found pending buffer at index: " << index << ". Pushing its data." << std::endl;
    output.push( pending_buffer[index] );
    pushed_buffer_size = pending_buffer[index].length();
    pending_buffer.erase( index ); //回收内存
    index += pushed_buffer_size;
    last_ordered_bytes = index;
    _bytes_pending -= pushed_buffer_size;

    // std::cout << "Last ordered: " << last_ordered_bytes << ", Pending bytes: " << _bytes_pending << std::endl;

    it = pending_buffer.find( index ); // Update iterator for the next loop iteration
  }

  if ( finish_flag && last_ordered_bytes == index_of_end ) {
    output.close();
  }
  cout << "[AUTO PUSH] End <==| Last ordered: " << last_ordered_bytes << ", Pending bytes: " << _bytes_pending << endl;

}

void Reassembler::insert( uint64_t first_index, std::string data, bool is_last_substring, Writer& output )
{
  // std::cout << "===Insert called with first_index: " << first_index<<" ,data: \""<<data << "\", data size: " <<
  // data.size() << std::endl;

  if ( first_index < last_ordered_bytes ) {
    // overlap
    //  cout<<"overlap"<<endl;
    uint64_t dif = last_ordered_bytes - first_index;
    if ( dif >= data.length() ) {
      //如果这个字符串已经push出去了, 忽略重叠
      return;
    }
    //如果没处理完, 就继续处理
    data = data.substr( dif );
    first_index = last_ordered_bytes;
  } else {
    //检查一下长度, 不要额外覆盖到后边的
    for ( const auto& pair : pending_buffer ) {
      // cout << "Checking against buffer element with first index: " << pair.first << " and data: " << pair.second
      // << endl;
      //在最后一个的里边
      if ( first_index <= pair.first ) {
        // cout<<"first_index + data.length() :"<<first_index + data.length()<<endl;
        // cout<<"pair.first: "<<pair.first<<endl;
        // cout<<"first_index + data.length(): "<<first_index + data.length()<<endl;
        // cout<<" pair.first + pair.second.length(): "<< pair.first + pair.second.length()<<endl;
        if ( ( first_index + data.length() > pair.first )
             && ( first_index + data.length() < pair.first + pair.second.length() ) ) {
          // Partial overlap
          uint64_t overlapLength = first_index + data.length() - pair.first;
          // cout << "Partial overlap detected! Overlap length: " << overlapLength << endl;
          uint64_t newLength = data.length() - overlapLength;
          // cout << "Original data: \"" << data << "\" | Trimming to new length: " << newLength << endl;
          data = data.substr( 0, newLength );
          // cout << "Data after trimming: \"" << data << "\"" << endl;
          break; // Exiting after the first partial overlap is handled.
        } else if ( first_index + data.length() >= pair.first + pair.second.length() ) {
          // Full overlap
          // cout << "Full overlap detected! Removing entire block with first index: " << pair.first << endl;
          _bytes_pending -= pair.second.length();
          pending_buffer.erase( pair.first );
          // cout << "Removed block. New bytes pending: " << _bytes_pending << endl;
        } else {
          // No recognizable overlap
          // cout << "No overlap or unrecognized overlap pattern with current buffer element." << endl;
          // cout << "Continuing to next buffer element if any." << endl;
        }
      } else {
        //在外边, 如果被之前的已经包围了的一条
        if ( pair.first + pair.second.length() >= first_index + data.length() ) {
          if ( is_last_substring ) {
            finish_flag = true;
            index_of_end = first_index + data.length();
            //如果收到结束信号, 就记录一下, 等数据都收完了, 就结束.
          }

          return;
        }
      }
    }
  }
  // std::cout << "===++after repaired first_index: " << first_index<<" ,data: \""<<data << "\", data size: " <<
  // data.size() << std::endl;

  if ( is_last_substring ) {
    finish_flag = true;
    index_of_end = first_index + data.length();
    //如果收到结束信号, 就记录一下, 等数据都收完了, 就结束.
  }

 
  receive_window_size = output.available_capacity();
  // last_acceptable_bytes = last_ordered_bytes + receive_window_size;
  last_bytes_received = first_index + data.length();
  // cout<<"receive window size: "<<receive_window_size<<endl;
  // std::cout << "Last bytes received: " << last_bytes_received << std::endl;
  // std::cout << "Last ordered bytes: " << last_ordered_bytes << std::endl;

  if ( last_bytes_received - last_ordered_bytes > receive_window_size ) {
    uint64_t n = data.length() - ( ( last_bytes_received - last_ordered_bytes ) - receive_window_size );
    // std::cout << "Data beyond window size, trimming to " << n << " bytes" << std::endl;
    if ( n > 0 ) {
      std::string temp_str = data.substr( 0, n );
      auto_push( first_index, output, temp_str );
    }

  } else { // 如果是在窗口范围内的
    // 如果乱序块还没形成, 还是头一个, 可直接push
    if ( last_ordered_bytes == first_index ) {
      // std::cout << "In order chunk. Pushing directly." << std::endl;
      auto_push( first_index, output, data );
    } else {
      // 来了之后不构成顺序块, 就先存着
      if ( first_index != last_ordered_bytes + pending_buffer[last_ordered_bytes].length() ) {
        auto it = pending_buffer.find( first_index );
        // 如果是不重复的,就插入,
        if ( it == pending_buffer.end() ) {
          // std::cout << "Out of order chunk. Buffering." << std::endl;
          pending_buffer.insert( std::make_pair( first_index, data ) );
          _bytes_pending += data.length();
        } else {
          //如果是重复的, 就修改
          pending_buffer[first_index] = data;
        }
      } else {
        // 构成了顺序块, 就push出去.
        // std::cout << "Formed an in-order chunk. Pushing." << std::endl;
        auto_push( first_index, output, data );
      }
    }
  }
  // cout<<"``````"<<endl;
}

uint64_t Reassembler::bytes_pending() const
{
  return _bytes_pending;
}
