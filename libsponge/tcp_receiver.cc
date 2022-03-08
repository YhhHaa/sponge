#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

/*
• Set the Initial Sequence Number if necessary. The sequence number of the first-
arriving segment that has the SYN flag set is the initial sequence number. You’ll want
to keep track of that in order to keep converting between 32-bit wrapped seqnos/acknos
and their absolute equivalents. (Note that the SYN flag is just one flag in the header.
The same segment could also carry data and could even have the FIN flag set.)
• Push any data, or end-of-stream marker, to the StreamReassembler. If the
FIN flag is set in a TCPSegment’s header, that means that the last byte of the payload
is the last byte of the entire stream. Remember that the StreamReassembler expects
stream indexes starting at zero; you will have to unwrap the seqnos to produce these.
*/
void TCPReceiver::segment_received(const TCPSegment &seg) {
    // 如果没有初始化的情况下没收到SYN, 忽略他们.
    if (not _SYN_flag and not seg.header().syn)
        return;

    // 初始化ISN, ISN可能是0, 也可能是任意一个uin32_t的数
    if (not _SYN_flag and seg.header().syn) {
        _ISN = seg.header().seqno;
        _SYN_flag = true;
    }

    /* 通过32位相对seqno复原64位绝对stream_index
    1. seqno: 将其转换成相应的stream_index
    2. ISN: 转换过程中, 需要的uin32_t一开始的初值
    3. _checkpoint: 转换出的结果应该最接近的uint64_t, 值实际上是_bytes_written
    需要考虑的特殊情况:
    1. 确保seqno是第一个字节的seqno, 有可能会有SYN占用了一个seqno
	// 如果seqno等于ISN，即"abc"的第一个字符的seqno为ISN。
	// 有两种情况，一种是过了n轮的绝对stream_index，这种情况下checkpoint必然很大，在while循环中找到
	// 还有一种是过了至少1轮的绝对stream_index，这种情况下checkpoint却是没有过了至少1轮，这是错误的
    */
    // 获得第一个字节的seqno
    WrappingInt32 seqno(seg.header().seqno.raw_value());
    if (seg.header().syn) {
        seqno = WrappingInt32(1 + seg.header().seqno.raw_value());
	}
	if (seqno == _ISN && _reassembler.stream_out().bytes_written() < (1ull << 32)) { // 排除非法情况
		return;
	}
    // 获得真实的stream_index
    uint64_t stream_index = unwrap(seqno, _ISN, _reassembler.stream_out().bytes_written()) - 1;

    // 传输数据
    string substrings = string(seg.payload().str().data(), seg.payload().size());
    _reassembler.push_substring(substrings, stream_index, seg.header().fin);
}

/* 1. 返回接收方需要的下一个seq, 是随机开始的32位绝对seq
> 若ISN未设置返回empty */
optional<WrappingInt32> TCPReceiver::ackno() const {
    if (_SYN_flag) {
        if (_reassembler.stream_out().input_ended())
            return WrappingInt32(wrap(_reassembler.stream_out().bytes_written() + 1, _ISN)) + 1;  // 因为FIN也占一个
        else
            return WrappingInt32(wrap(_reassembler.stream_out().bytes_written() + 1, _ISN));
    } else
        return {};
}

/* 返回capacity-_output的大小 */
size_t TCPReceiver::window_size() const { return _reassembler.capacity_left(); }
