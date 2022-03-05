#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _timer(retx_timeout)
    , _rto_time(retx_timeout)
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _next_seqno(0)
    , _syn_flag(false)
    , _fin_flag(false)
    , _window_size(1)
    , _recv_zero_flag(false)
    , _flying_seqnos(0)
    , _cnt_rtrans(0) {}

uint64_t TCPSender::bytes_in_flight() const { return _flying_seqnos; }

void TCPSender::fill_window() {
    /* 从ByteStream拿根据receiver的参数数据报发送数据不大于TCPConfig::MAC_PAYLOAD_SIZE
    ？？如果在收到来自receiver的ACK前，认为window size=1
    ？？如果是第一次发送，只发送SYN和seqno
    1. 每当一个数据报被发送了需要被跟踪
    2. 每当一个数据报发送了，如果timer未启动，则以RTO时间启动
    》》如果window_size是0，则当做是1处理 */

    // 如果第一次发送，那么要发送一个syn报文
    if (not _syn_flag) {
        _syn_flag = true;

        // 构造报文
        TCPSegment syn_seg;
        syn_seg.header().syn = true;
        syn_seg.header().seqno = _isn;

        // 发送该报文
        _segments_out.push(syn_seg);
        _segments_flying.push(syn_seg);

        // 统计变量
        _window_size -= syn_seg.length_in_sequence_space();
        _next_seqno += syn_seg.length_in_sequence_space();
        _flying_seqnos += syn_seg.length_in_sequence_space();
        if (not _timer.time_start_status())
            _timer.time_start(_rto_time);

        return;
    }

    // 如果缓冲区结束，那么不再发送了。
    if (_fin_flag)
        return;

    // 权衡情况：
    // 1. eof：window_size还有剩余，允许发送eof
    // 2. 没到eof：可能是buffer size不为空，但是eof设置了。也可能是buffer size不为空，但是eof没设置。
    size_t left_size = min(static_cast<size_t>(_window_size), _stream.buffer_size());

    // 1. 如果buffer_size为0且到了最后，那么只发送一个fin报文
    if (_window_size >= 1 && _stream.eof()) {
        _fin_flag = true;

        // 构造报文
        TCPSegment seg;
        seg.header().fin = true;
        seg.header().seqno = wrap(_next_seqno, _isn);

        // 发送报文
        _segments_out.push(seg);
        _segments_flying.push(seg);

        // 统计变量
        _window_size -= static_cast<uint16_t>(seg.length_in_sequence_space());
        _next_seqno += seg.length_in_sequence_space();
        _flying_seqnos += seg.length_in_sequence_space();
        if (not _timer.time_start_status())
            _timer.time_start(_rto_time);

    } else {  // 2.
              // 如果由上层调用，说明需要填满当前窗口，需要在buffer_size，最大报文长度，接受窗口长度之间权衡需要发送的报文数量
        // 但是要根据最大报文长度，可能要分多个报文进行发送
        while (left_size > 0) {
            size_t send_size = 0;
            if (left_size >= TCPConfig::MAX_PAYLOAD_SIZE) {  // 如果比最大还大，此时只发送一个最大报文
                send_size = TCPConfig::MAX_PAYLOAD_SIZE;
                left_size -= TCPConfig::MAX_PAYLOAD_SIZE;
            } else {  // 如果比最大还小，那么只发送该数量
                send_size = left_size;
                left_size = 0;
            }

            // 根据长度构造需要的seg
            TCPSegment seg;
            seg.header().seqno = wrap(_next_seqno, _isn);
            seg.payload() = _stream.read(send_size);
            //	此时需要判断是否到了末尾且窗口大小允许发送Fin，如果不允许则需要等下次
            if (_stream.eof() && (_window_size > seg.length_in_sequence_space())) {
                seg.header().fin = true;
                _fin_flag = true;
            }

            // 发送该报文
            _segments_out.push(seg);
            _segments_flying.push(seg);

            // 统计变量
            _window_size -= static_cast<uint16_t>(seg.length_in_sequence_space());
            _next_seqno += seg.length_in_sequence_space();
            _flying_seqnos += seg.length_in_sequence_space();
            if (not _timer.time_start_status())
                _timer.time_start(_rto_time);
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    /* 该方法接受来自receiver的ackno以及window size。如果ackno的absolute比之前来的大，则
    1. 设置RTO为初始值RTO
    2. 如果此时外面还有发送但是没有确认的数据段，重启timer
    ？？如果所有的已发送的数据报都被确认了，停止timer
    3. 重置连续的重传次数为0

    需要做的事
    1. 通过ackno移除outstanding的segments
    2. 发送segments填满window */

    // 将相对的seqno转换成绝对的seqno，代表接收方下一个期待的seqno
    uint64_t abs_seq = unwrap(ackno, _isn, _next_seqno);

    // 如果接受的abs_seq小于等于下一个将发送的，那么代表有一部分接受到了，但是还有一部分需要重发。同时排除错误情况。
    if (_segments_flying.empty())
        return;

    TCPSegment lst_seg = _segments_flying.front();
    if (abs_seq <= _next_seqno && abs_seq >= unwrap(lst_seg.header().seqno, _isn, _next_seqno)) {
        // 删除维护的正在飞行的seg
        while (!_segments_flying.empty()) {
            lst_seg = _segments_flying.front();
            if (unwrap(lst_seg.header().seqno, _isn, _next_seqno) + lst_seg.length_in_sequence_space() <= abs_seq) {
                _flying_seqnos -= lst_seg.length_in_sequence_space();
                _segments_flying.pop();

                // 重发统计
                _rto_time = _initial_retransmission_timeout;
                _timer.time_start(_rto_time);
                _cnt_rtrans = 0;
            } else {
                break;
            }
        }

        // 自己重新调用fill_window，对包进行重发
        if (window_size < _flying_seqnos) {  // 如果window_size比外界还在发送的seqnos小，说明此时不要传输包了
            _window_size = 0;
            _recv_zero_flag = true;
            return;
        } else {  // 否则说明接收方能容纳更多的包，将发送窗口更新为接受窗口
            if (window_size == 0) {
                _window_size = 1;
                _recv_zero_flag = true;
            } else {
                _window_size = window_size;
                _recv_zero_flag = false;
            }
        }
        fill_window();
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    /* 该方法每隔ms_since_last_tick毫秒就会被调用一次。
    如果超时timer到了则需要做的事情有：
    1. 重传最早的还没完全确认的数据段
    2. 如果window size不是0，跟踪连续的重传次数并使RTO翻倍
    3. 重启timer并让它在RTO时间后过期，用于管理超时
    */

    // 如果第一次没有启动计时器，则以rto时间启动
    if (not _timer.time_start_status()) {
        _timer.time_start(_rto_time);
    }
    // 更新计时器
    _timer.time_flow(ms_since_last_tick);

    // 如果计时器时间到了
    // 首先判断是否还有飞行中的数据
    if (_segments_flying.empty()) {
        _timer.time_end();
        return;
    }
    // 如果没有的话，就可以进行超时判断
    if (_timer.time_status()) {
        // 重传最早期的数据报
        _segments_out.push(_segments_flying.front());
        if (!_recv_zero_flag) {
            // 跟踪重传次数与更新rto时间
            _cnt_rtrans += 1;
            _rto_time *= 2;
        }
        // 重启计时器
        _timer.time_end();
        _timer.time_start(_rto_time);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _cnt_rtrans; }

void TCPSender::send_empty_segment() {
    /* 发送一个占据0seqno的segment，其seqno设置正确，无需跟踪 */
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}
