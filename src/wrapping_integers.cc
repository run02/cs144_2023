#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return Wrap32 { uint32_t((zero_point.raw_value_+n)%(uint64_t(1)<<32)) };
}

// #define DEBUG_PRINT
// #ifdef DEBUG_PRINT
// #include <iostream> 
// #define PRINT(label, value) std::cout << (label) << ": " << (value) << std::endl
// #else
// #define PRINT(label, value) // Nothing
// #endif

uint64_t Wrap32::unwrap(Wrap32 zero_point, uint64_t checkpoint) const
{
    const uint64_t TWO_32 = uint64_t(1) << 32;
    uint64_t possible_absolute_seqno = (raw_value_ >= zero_point.raw_value_) ? (raw_value_ - zero_point.raw_value_): uint64_t(raw_value_) + TWO_32 - zero_point.raw_value_;
    uint64_t round=checkpoint/TWO_32;
    uint64_t loop_begin=TWO_32*(round>0?round-1:0);
    uint64_t left_possible = possible_absolute_seqno+loop_begin; //一定要从适当的地方开始, 因为范围就在n-1, n, n+1 之内, 如果不从n开始, 碰到较大的absolute_sequence_number, 会非常耗时
    uint64_t right_possible = left_possible + TWO_32;
    if(left_possible>checkpoint){ //处理第一轮的情况
      return left_possible;
    }
    while ((right_possible < checkpoint)&&(right_possible<=UINT64_MAX-TWO_32+1)){ //这里假设不超出2^64的范围, 如果超出之后这里也只会取每超出的序号, 2^64是2097152(TB), 能超过这个序号就见鬼了 
      left_possible += TWO_32;
      right_possible += TWO_32;
    }
    return {(checkpoint - left_possible) > (right_possible - checkpoint) ? right_possible : left_possible};
}