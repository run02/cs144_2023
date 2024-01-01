#include "reassembler.hh"
#include <iostream> // 确保包含了iostream

using namespace std;

Reassembler::Reassembler()
  : pending_buffer()
  , _bytes_pending( 0 )
  , first_unassembled_index( 0 )
  , _EOF_index( 0 )
  , received_FIN( false )
  , available_capacity( 0 )
{}

void Reassembler::insert( uint64_t first_index, std::string data, bool is_last_substring, Writer& output )
{
  std::cout << "[开始插入] first_index: " << first_index << ", data length: " << data.length()
            << ", is_last_substring: " << is_last_substring << std::endl;
  available_capacity = output.available_capacity();
  std::cout << "[当前窗口大小] available_capacity: " << available_capacity << std::endl;
  
  //步骤一: 先处理data左边界的问题, 左边界一般是由于重发重叠导致的, 限制左边界在first_unassembled_index(包含)之后
  // 为什么先处理左边界而不是右边界? 因为如果窗口很小, 来了一个在左边界之外的很远的, 长度又很长的, 会被砍成只有窗口大小, 但是处理左边界的时候又会砍一刀, 其实是"没对齐,砍歪了"
  //因为first_unassembled_index左边的内容以及被pop出去了
  if (first_index >= first_unassembled_index) {
    std::cout << "[块完全未被pop出去] 不需要截取" << std::endl;
  }
  // 对于部分pop出去的块, 截取未被pop出去的部分
  else if ( (first_index < first_unassembled_index) 
        && (first_index+data.length()>first_unassembled_index)) {
    std::cout << "[块部分pop出去] 截取未被pop出去的部分, ";
    uint64_t cut_off_index =first_unassembled_index-first_index;
    uint64_t n=data.length()-cut_off_index ;
    data = data.substr( cut_off_index);//从第cut_off_index开始到字符串末尾, 也就取是后n位
    std::cout << "取后" <<n <<"位"<< std::endl;
    first_index = first_unassembled_index;
    // std::cout << "data :" <<data <<", first_index: "<<first_index<< std::endl;
  }
  // 对于已经pop出去的块, 忽略
  else if(first_index+data.length()<=first_unassembled_index){
    std::cout << "[块已被pop出去] 忽略" << std::endl;
    return;
  }else{
        std::cout << "[处理左边界碰到了意料之外奇怪的情况] " << std::endl;
  }

  //步骤二: 先处理data的右边界的问题, 限制data的长度在窗口范围(available_capacity)内
  // 如果全部超出, 忽略
  if ( first_index >= first_unassembled_index + available_capacity ) {
    std::cout << "[全部超出范围] 忽略插入" << std::endl;
    return;
  }
  // 如果部分超出, 去掉超出的部分, 留下剩余的部分
  else if ( (first_index < first_unassembled_index + available_capacity)
          &&((first_index + data.length()) > (first_unassembled_index + available_capacity)) ) {
    uint64_t out_of_range = first_index + data.length() - ( first_unassembled_index + available_capacity );
    uint64_t n = data.length() - out_of_range;
    std::cout << "[部分超出范围] 保留未超出部分: " << n << " bytes" << std::endl;
    data = data.substr( 0, n );
  }
  // 如果没有超出范围, 无需修改, 检查是否收到EOF
  else if ( (first_index < first_unassembled_index + available_capacity)
          &&((first_index + data.length()) <= (first_unassembled_index + available_capacity)) ){
    std::cout << "[没有超出范围] 检查FIN" << std::endl;
    if ( is_last_substring ) {
      std::cout << "[在范围内收到FIN] 标记EOF" << std::endl;
      received_FIN = true;
      _EOF_index = first_index + data.length();
    }
  }else{
     std::cout << "[处理右边界碰到了意料之外奇怪的情况] " << std::endl;
  }

  // 处理重叠的情况, 先考虑边界, 再考虑重叠, 前边以及处理过边界问题, 保证数据此时符合窗口大小, 且在窗口内了
  // 判断是否覆盖了现有的数据, 优先接受最新当前的数据
  // 修改它的结构
  std::cout << "[检查覆盖] 正在检查覆盖情况..." << std::endl;

  auto iter = pending_buffer.begin();  // 使用显式迭代器

  while (iter != pending_buffer.end()) {  // 使用while循环代替for循环
    auto current = iter++;  // 保存当前元素的迭代器，并预先递增it到下一个元素
    //如果左边被完全覆盖, 就丢弃
    if ((first_index >= current->first)
               && (first_index + data.length() <= current->first + current->second.length())) {
        data = "";
        std::cout << "[左边被完全覆盖] 清除data,退出遍历" << std::endl;
        break;  // 退出循环
    }//左边被不完全覆盖, 只覆盖了左边一点
    else if ((first_index > current->first)
               && (first_index < (current->first + current->second.length()))
               && (first_index + data.length() > current->first + current->second.length())) {
        std::cout << "[左边被不完全覆盖] 清除左边,继续遍历" << std::endl;
        uint64_t bytes_cut_off= current->first + current->second.length()-first_index;
        data=data.substr(bytes_cut_off);
        first_index=current->first + current->second.length();
    }//如果右边完全覆盖了某个块, 删除旧数据块, 后续会插入这个块实现更新
    else if ( (first_index <= current->first) 
          && ((first_index + data.length()) >= (current->first + current->second.length()))) {
        std::cout << "[右边完全覆盖] 删除旧数据块" << std::endl;
        _bytes_pending -= current->second.length();
        pending_buffer.erase(current);  // 删除当前元素，并不需要更新迭代器，因为它已经预先递增了
    }//如果右边只覆盖了块左边的一部分 
    else if ( (first_index <= current->first)
               && ((first_index + data.length()) > (current->first)) //大于左边
               && ((first_index + data.length()) < (current->first+current->second.length())) //小于右边
               ) {
        uint64_t beyond_cut_off = first_index + data.length() - current->first ;
        std::cout << "[部分覆盖] 截取未被覆盖的部分 退出遍历" << std::endl;
        //std::cout << "[部分覆盖] 截取当前部分的值, 更新新覆盖的位置的值, 退出遍历" << std::endl;
        data = data.substr(0, data.length()- beyond_cut_off);
        break;  // 退出循环
    }//如果暂时不产生重叠, 在左边或者右边
    else if ( ((first_index + data.length()) <= current->first)
              ||(first_index>(current->first+current->second.length()))) {
        std::cout << "[没有覆盖] 继续遍历检查下一个" << std::endl;
        // break;  // 退出循环
    } else{
        std::cout << "[处理重叠问题时碰到了意料之外奇怪的情况] " << std::endl;
    }
    
  }
    // 在pending_buffer中存""浪费index的内存, 并没有意义,
    // 但是''如果出现在first_unassembled_index上, 如果是EOF, 不处理会丢失状态
  if ( ( data.length() == 0 ) && ( first_index > first_unassembled_index ) ) {
    std::cout << "[忽略空数据] 不在first_unassembled_index" << std::endl;
    return;
  } else if ( ( data.length() == 0 ) && ( first_index == first_unassembled_index ) ) {
    std::cout << "[收到空数据] 在first_unassembled_index" << std::endl;
    if ( received_FIN ) {
      std::cout << "[接收到FIN] 关闭输出" << std::endl;
      output.close();
    }
    return;
  }

  std::cout << "[处理顺序块] 正在处理顺序块..." << std::endl;
  if ( first_index == first_unassembled_index ) {
    std::cout << "[顺序块匹配] 发现顺序块, 准备输出数据..." << std::endl;
    output.push( data );
    first_unassembled_index += data.length();

    // 检查是否构成连续的数据块, 就一直push连续的数据
    auto it = pending_buffer.find( first_unassembled_index );
    while ( it != pending_buffer.end() ) {
      std::cout << "[连续块处理] 输出连续块数据..." << std::endl;
      output.push( it->second ); 
      _bytes_pending -= it->second.length();
      first_unassembled_index += it->second.length();
      pending_buffer.erase( it );                          // 回收内存，这里直接使用迭代器
      it = pending_buffer.find( first_unassembled_index ); // 更新迭代器
    }
    // 如果pop到最后一个了, 就关闭
    if ( first_unassembled_index == _EOF_index &&received_FIN==true ) {
      std::cout << "[到达EOF] 最后一个字节已输出，关闭输出流..." << std::endl;
      output.close();
    }
  } else { // 不构成顺序块, 存起来
    std::cout << "[非顺序块] 存储非顺序块数据..." << std::endl;
    pending_buffer.insert( std::make_pair( first_index, data ) );
    _bytes_pending += data.length();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  return _bytes_pending;
}
