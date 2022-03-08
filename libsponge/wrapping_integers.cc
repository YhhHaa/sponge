#include "wrapping_integers.hh"

#include <iostream>
// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t round_no = n % (1ull << 32);
    return WrappingInt32{isn.raw_value() + round_no};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint64_t abs_seqno = 0;
    uint64_t max_uint32 = 1ull << 32;
    uint64_t n_value = static_cast<uint64_t>(n.raw_value());
    uint64_t isn_value = static_cast<uint64_t>(isn.raw_value());

    // 对齐到n需要几步
    if (n_value > isn_value) {
        abs_seqno += n_value - isn_value;
    } else if (n_value < isn_value) {
        abs_seqno += max_uint32 - (isn_value - n_value);
    }
    if (checkpoint < abs_seqno) {
        return abs_seqno;
	}

    // 每次增加完一个轮回, 判断与checkpoint的距离
    while (true) {
        if (checkpoint == abs_seqno)
            return abs_seqno;
        else if (checkpoint > abs_seqno) {
            uint64_t times = (checkpoint - abs_seqno) / max_uint32;
            if (times == 0)
                times = 1;
            abs_seqno += times * max_uint32;
        } else if (checkpoint < abs_seqno) {
            if (abs_seqno - checkpoint > checkpoint - (abs_seqno - max_uint32))
                return abs_seqno - max_uint32;
            else
                return abs_seqno;
        }
    }
}
