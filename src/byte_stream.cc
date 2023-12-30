#include <stdexcept>

#include "byte_stream.hh"

using namespace std;

//ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}
ByteStream::ByteStream(uint64_t capacity)
  : capacity_(capacity),
  saved_buffer(),  // 初始化为空的 std::queue<std::string>
  used_(0),        // 初始化为 0
  total_bytes_push(0),  // 初始化为 0
  total_bytes_pop(0),   // 初始化为 0
  closed(false),   // 初始化为 false，表示流最初是打开的
  error(false)     // 初始化为 false，表示最初没有错误
{}

void Writer::push( string data )
{
  // Your code here.
  if(data.length()==0){
    return;
  }
  if(used_+data.length()<=capacity_){
    saved_buffer.push(data);
    used_+=data.length();
    total_bytes_push+=data.length();
  }else{
    int len=capacity_-used_;
    string tmp=data.substr(0,len);
    saved_buffer.push(tmp);
    used_=capacity_;
    total_bytes_push+=len;
    set_error();
    // closed= true;
  }
}
void Writer::close()
{
  closed= true;
}

void Writer::set_error()
{
  error= true;
}

bool Writer::is_closed() const
{
  // Your code here.
  return closed;
}

uint64_t Writer::available_capacity() const
{
  // Your code here.
  return capacity_-used_;
}

uint64_t Writer::bytes_pushed() const
{
  // Your code here.
  return total_bytes_push;
}

string_view Reader::peek() const
{
  // Your code here.
  return saved_buffer.front();
}

bool Reader::is_finished() const
{
  // Your code here.
  return closed&(total_bytes_pop==total_bytes_push);
}

bool Reader::has_error() const
{
  // Your code here.
  return error;
}

void Reader::pop( uint64_t len )
{
  // Your code here.
  uint64_t sum=0;
  while (true)
  {
    if (used_<=0){
      break;
    }else{
      if(!saved_buffer.empty()){

        uint64_t front_len=saved_buffer.front().length();
        sum+=front_len;

        if(sum<=len){
          /*如果当前字符串的长度加上已经推出去的长度不超过需要推出的长度, 先推出这个字符串*/
          used_-=front_len;
          total_bytes_pop+=front_len;
          saved_buffer.pop();
        }else{
          /*如果当前字符串的长度加上已经推出的长度比要推出的长, 就把不超过长度的推出去(存后边的, 推出去前边的, 就是取后几位)*/
          uint64_t dif=sum-len;
          string tmp_str=saved_buffer.front().substr(front_len-dif);
          saved_buffer.front()=tmp_str;
          used_-=front_len-dif;
          total_bytes_pop+=front_len-dif;
          break;
        }
      }else{
        break;
      }
    }
  }
}




uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  return used_;
}

uint64_t Reader::bytes_popped() const
{
  // Your code here.
  return total_bytes_pop;
}
