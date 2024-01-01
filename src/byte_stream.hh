#pragma once

#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>

class Reader;
class Writer;
/*
 * 一开始理解出了些问题, 我以为是要做一个类似于char* 之类的缓存. 这里抽象出的接口主要是
 * 实现写到缓存, 然后可以用队列的方式读出来(FIFO),
 * 以及包括一些状态, 是否写满了, 有多少空间还能用, 是否关闭(Writer写完了), 是否完成(Writer写完,Reader读完)
 * 以及一些操作, 进队列, 出队列, 报错, 写多少字节
 * 有些别扭的是这里的接口规划的是queue<string>(可能是方便一条一条读出来?), 但是又提供了读几个字节的接口,
 * 一开始不太理解接口的含义啥的, 没琢磨明白, 后来知道要用queue<string>之后才想明白怎么个事
 * */
class ByteStream
{
protected:
  uint64_t capacity_;
  // Please add any additional state to the ByteStream here, and not to the Writer and Reader interfaces.
  std::queue<std::string> saved_buffer;
  uint64_t used_;
  uint64_t total_bytes_push;
  uint64_t total_bytes_pop;
  /*bool finished; //total_bytes_push==total_bytes_pop&closed==true*/
  bool closed;
  bool error;

public:
  explicit ByteStream( uint64_t capacity );

  // Helper functions (provided) to access the ByteStream's Reader and Writer interfaces
  Reader& reader();
  const Reader& reader() const;
  Writer& writer();
  const Writer& writer() const;
};

class Writer : public ByteStream
{
public:
  void push( std::string data ); // Push data to stream, but only as much as available capacity allows.

  void close();     // Signal that the stream has reached its ending. Nothing more will be written.
  void set_error(); // Signal that the stream suffered an error.

  bool is_closed() const;              // Has the stream been closed?
  uint64_t available_capacity() const; // How many bytes can be pushed to the stream right now?
  uint64_t bytes_pushed() const;       // Total number of bytes cumulatively pushed to the stream
};

class Reader : public ByteStream
{
public:
  std::string_view peek() const; // Peek at the next bytes in the buffer
  void pop( uint64_t len );      // Remove `len` bytes from the buffer

  bool is_finished() const; // Is the stream finished (closed and fully popped)?
  bool has_error() const;   // Has the stream had an error?

  uint64_t bytes_buffered() const; // Number of bytes currently buffered (pushed and not popped)
  uint64_t bytes_popped() const;   // Total number of bytes cumulatively popped from stream
};

/*
 * read: A (provided) helper function thats peeks and pops up to `len` bytes
 * from a ByteStream Reader into a string;
 */
void read( Reader& reader, uint64_t len, std::string& out );
