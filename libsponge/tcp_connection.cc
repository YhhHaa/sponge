#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _intervals; }

void TCPConnection::segment_received(const TCPSegment &seg) { 
	// ****************************************************************
	// ************************** 变量跟踪开始 **************************
	// 每接受一个报文，重设intervals
	_intervals = 0;
	// ************************** 变量跟踪结束 **************************
	// ****************************************************************

	// ****************************************************************
	// ************************** 接受报文开始 **************************
	/* 
	• if the rst (reset) flag is set, sets both the inbound and outbound streams to the error
	state and kills the connection permanently. Otherwise it. . .
	• gives the segment to the TCPReceiver so it can inspect the fields it cares about on
	incoming segments: seqno, syn , payload, and fin .
	• if the ack flag is set, tells the TCPSender about the fields it cares about on incoming
	segments: ackno and window size.
	• if the incoming segment occupied any sequence numbers, the TCPConnection makes
	sure that at least one segment is sent in reply, to reflect an update in the ackno and
	window size.
	• There is one extra special case that you will have to handle in the TCPConnection’s
	segment received() method: responding to a “keep-alive” segment. The peer may
	choose to send a segment with an invalid sequence number to see if your TCP imple-
	mentation is still alive (and if so, what your current window is). Your TCPConnection
	should reply to these “keep-alives” even though they do not occupy any sequence
	numbers. Code to implement this can look like this:
	*/

	if (!active()) return;

	// 1. 如果是rst报文，则进入unclean shutdown
	if (seg.header().rst) {
		_unclean_shutdown(true);
		return;
	}

	// 2. receiver接收到报文后，会根据syn，seqno，payload，fin对数据进行组装，
	// 其一共有四个状态LISTEN|SYN_RECV|FIN_RECV|ERROR。
	_receiver.segment_received(seg);
	if (_inbound_done() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }

	// 3. 如果是syn报文
	if (seg.header().syn) {
		if (_sender.next_seqno_absolute() == 0) { // 处于LISTEN
			_sender.fill_window();
			_send_segments(true);
		} else if (seg.header().ack && _sender.next_seqno_absolute() == _sender.bytes_in_flight()) { // 处于SYN_SENT
			_sender.ack_received(seg.header().ackno, seg.header().win);
			if (_sender.segments_out().empty()) _sender.send_empty_segment();
			_send_segments(true);
		} else if (_sender.next_seqno_absolute() == _sender.bytes_in_flight()) { // 处于SYN_SENT同时连接
			_sender.ack_received(seg.header().ackno, seg.header().win);
			TCPSegment syn_ack_seg;
			if (_sender.segments_out().empty()) { // 只需要回复ack就行，有点奇怪？
				// syn_ack_seg.header().syn = true;
				// syn_ack_seg.header().seqno = WrappingInt32(_sender.next_seqno().raw_value() - 1);
				_sender.segments_out().push(syn_ack_seg);
			}
			_send_segments(true);
		}
		return;
	}

	// 4. sender接受报文后，会根据ackno，window_size调整自己的发送窗口，
	// 然后发送缓冲区数据。一共有7个状态CLOSED|SYN_SENT|SYN_ACKED|SYN_ACKED|
	// FIN_SENT|FIN_ACKED
	if (seg.header().ack) {
		_sender.ack_received(seg.header().ackno, seg.header().win);
		
		_send_segments(true);
	}

	// 5. 接收到keep-alive报文
	if (_receiver.ackno().has_value() && seg.length_in_sequence_space() == 0 && \
		seg.header().seqno == _receiver.ackno().value() - 1) {

		_sender.send_empty_segment();
		_send_segments(true);
	}

	// ************************** 接受报文结束 **************************
	// ****************************************************************

	// ****************************************************************
	// ************************** 发送报文开始 **************************
	/* 
	• any time the TCPSender has pushed a segment onto its outgoing queue, having set the
	fields it’s responsible for on outgoing segments: (seqno, syn , payload, and fin ).
	• Before sending the segment, the TCPConnection will ask the TCPReceiver for the fields
	it’s responsible for on outgoing segments: ackno and window size. If there is an ackno,
	it will set the ack flag and the fields in the TCPSegment.基本所有报文都有ACK，除了一开始的
	情况。
	*/

	/* 发送RST报文的情况，RST可以通过send_empty_segment()或者fill_window()中拿到
	1. If the sender has sent too many consecutive retransmissions without success (more
	than TCPConfig::MAX RETX ATTEMPTS, i.e., 8).
	2. If the TCPConnection destructor is called while the connection is still active
	(active() returns true).
	*/

	if (seg.length_in_sequence_space() > 0) {
		if (_sender.segments_out().empty()) {
			_sender.send_empty_segment();
		}
		_send_segments(true);
	}
	// ************************** 发送报文结束 **************************
	// ****************************************************************
	
	
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
	// 写入缓冲区数据并尽可能发送报文
    size_t has_written = _sender.stream_in().write(data);
	_sender.fill_window();
	_send_segments(true);
	return has_written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
	/* 
	• tell the TCPSender about the passage of time.
	• abort the connection, and send a reset segment to the peer (an empty segment with
	the rst flag set), if the number of consecutive retransmissions is more than an upper
	limit TCPConfig::MAX RETX ATTEMPTS.
	• end the connection cleanly if necessary (please see Section 5)
		- unclean shutdown
		- clean shutdown
			- 原则1-3满足后linger停止：如果linger为true，需要至少自从上次接受到报文后10次超时时间进行lingering然后停止
			- 原则1-3满足后passive停止：如果linger为false，直接停止
	*/

	if (!active()) return;

	// 1. 让sender确定是否需要重传
	_sender.tick(ms_since_last_tick);

	// 2. 判断是否需要unclean shutdown
	if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
		_unclean_shutdown(true);
		return;
	} else { // 否则正常重传
		_send_segments(true);
	}

	// 3. 判断是否需要clean shutdown
	_intervals += ms_since_last_tick;
	if (_inbound_done() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }
	if (_inbound_done() && _outbound_done()) {
		if (_linger_after_streams_finish && _intervals >= 10 * _cfg.rt_timeout) {
			_active = false;
		} else if (!_linger_after_streams_finish) {
			_active = false;
		}
	}
}

void TCPConnection::end_input_stream() {
	// 停止输出字节流
	_sender.stream_in().end_input();
	// 发送FIN报文
	_sender.fill_window();
	_send_segments(true);
}

void TCPConnection::connect() {
	if (!active()) return;
	
	/* 通过构造SYN数据报发起主动或同时连接请求。*/
	_sender.fill_window();
	_send_segments(false);
}

void TCPConnection::_unclean_shutdown(bool send_flag) {
	/* 接收到或发送了RST报文，进入unclean shutdown */
	if (send_flag) {
		TCPSegment rst_seg;
		if (_sender.segments_out().empty()) {
			_sender.send_empty_segment();
		}
		rst_seg = _sender.segments_out().front();
		_sender.segments_out().pop();

		rst_seg.header().rst = true;
		_sender.segments_out().push(rst_seg);
		_send_segments(true);
	}

	// 中断输入输出流
	_receiver.stream_out().set_error();
	_sender.stream_in().set_error();

	// active始终返回fasle
	_active = false;
}

void TCPConnection::_send_segments(bool ack_flag) {
	if (ack_flag && !_receiver.ackno().has_value()) return;
	// 封装函数：发送sender的数据报
	while (!_sender.segments_out().empty()) {
		// 获取需要发送的数据报
		TCPSegment seg = _sender.segments_out().front();

		// 如果不是fst发送，将其添加ack+ackno+win
		if (ack_flag) {
			seg.header().ack = true;
			seg.header().ackno = _receiver.ackno().value();
			seg.header().win = _receiver.window_size();
		}

		// 将其发送
		_segments_out.push(seg);
		_sender.segments_out().pop();
	}

	if (_inbound_done() && _outbound_done()) {
		if (_linger_after_streams_finish && _intervals >= 10 * _cfg.rt_timeout) {
			_active = false;
		} else if (!_linger_after_streams_finish) {
			_active = false;
		}
	}
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
			// 需要重置rst的第二种情况，active()为true时，调用析构函数
			_unclean_shutdown(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
