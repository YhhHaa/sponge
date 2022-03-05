#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

class Timer {
    /* 用于启动定时与停止定时
    1. 每当有包含seqno的segment被发送但是计时器没有启动，则启动计时器在RTO毫秒后过期
    2. 当所有outstanding的segments被接受，则停止计时器
    3. 如果tick中timer到期，tick函数所做
    */
  private:
    size_t _round_time;
    bool _start_flag;

  public:
    Timer(const size_t rto_time) : _round_time(rto_time), _start_flag(false) {}
    bool time_status() { return _round_time == 0 && _start_flag; }
    bool time_start_status() { return _start_flag; }
    void time_start(size_t rto_time) {
        _round_time = rto_time;
        _start_flag = true;
    }
    void time_end() {
        _round_time = 0;
        _start_flag = false;
    }
    void time_flow(size_t tick_time) {
        if (_start_flag)
            _round_time = _round_time >= tick_time ? _round_time - tick_time : 0;
    }
};

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
/*
完成的目标
1. 将ByteStream中的内容拆分成segments发送，每个segment需要填写的内容有
》》sequence number，SYN flag，Fin flag，payload。
2. 读取来自于receiver的segments，需要读取的内容有
》》ackno，window size
需要的操作
1. 读取来自于receiver的两个信息
2. 调用fill_window根据来自receiver的信息尽可能地发送大的多的来自于ByteStream的数据报
？？直到接受窗口满了或者ByteStream空了
    1. 发送的segments但还没接受的被跟踪
    2. 重发超时的segments
类关键的作用
1. tick函数，被上层每隔一段时间调用，用于定时操作
2. _segments_out队列用于发送
 */
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};
    std::queue<TCPSegment> _segments_flying{};

    //! retransmission timer for the connection
    Timer _timer;            // 用于计时使用
    unsigned int _rto_time;  // rto超时时间
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno;
    bool _syn_flag;
    bool _fin_flag;

    // 接收方相关
    uint16_t _window_size;  // 接收方窗口大小
    bool _recv_zero_flag;   // 接收方窗口大小为0

    uint64_t _flying_seqnos;   // 在空中还没确认的seqnos
    unsigned int _cnt_rtrans;  // 连续重传次数统计

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
