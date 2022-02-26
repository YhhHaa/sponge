#include "stream_reassembler.hh"
#include <algorithm>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool sort_function(Segment seg1, Segment seg2) { return seg1.index < seg2.index; }

StreamReassembler::StreamReassembler(const size_t capacity)
	: _output(capacity), _buffer(), _capacity(capacity), _next_number(0), _unassembled_bytes(0) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
/* 如果其包含_next_number则将包含的内容写入_output中, 然后更新end_input以及_next_number(write)
** 如果没有包含_next_number, 则将其放入_buffer中, 然后调整_buffer的最长前缀(push_segment)
 */
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
	if (not write_segment(data, index, eof)) {
		Segment segment = { data, index, eof };
		push_segment(segment);
	}
}

/* 判断数据是否可以写入, 如果不可以返回false, 如果可以写入则写入并更新eof以及_next_number */
bool StreamReassembler::write_segment(const string &data, const size_t index, const bool eof) {
	if (data.empty() && eof) {
		_output.end_input();
		return true;
	} else if (data.empty()) {
		return true;
	}

	size_t wrtsz, real_wrtsz;

	if (index <= _next_number && _next_number <= index + data.size() - 1) {
		wrtsz = (index + data.size() - 1) - _next_number + 1;
		real_wrtsz = _output.write(data.substr(_next_number - index, wrtsz));

		if (eof && real_wrtsz == wrtsz) _output.end_input();

		if (real_wrtsz != wrtsz) {
			Segment segment = { data.substr(_next_number - index + real_wrtsz, data.size() - 1 - _next_number - real_wrtsz + 1), _next_number + real_wrtsz, eof };
			push_segment(segment);
		}

		_next_number += real_wrtsz;

		find_segment();
		return true;
	}
	return false;
}

void StreamReassembler::find_segment() {
	size_t wrtsz, real_wrtsz;
	std::vector<Segment> new_buffer;

	_unassembled_bytes = 0;

	for (size_t i = 0; i < _buffer.size(); i++) {
		if (_buffer[i].index <= _next_number && _next_number <= _buffer[i].index + _buffer[i].data.size() - 1) {
			wrtsz = (_buffer[i].index + _buffer[i].data.size() - 1) - _next_number + 1;
			real_wrtsz = _output.write(_buffer[i].data.substr(_next_number - _buffer[i].index, wrtsz));
			if (_buffer[i].eof && real_wrtsz == wrtsz) _output.end_input();
			_next_number += real_wrtsz;
		} else if (_next_number < _buffer[i].index) {
			new_buffer.push_back(_buffer[i]);
			_unassembled_bytes += _buffer[i].data.size();
		}
	}
	_buffer = new_buffer;
}

void StreamReassembler::push_segment(Segment &segment) {
	if (segment.index + segment.data.size() - 1 < _next_number) return;
	if (segment.index <= _next_number) {
		segment.data = segment.data.substr(_next_number - segment.index, segment.index + segment.data.size() - 1 - _next_number + 1);
		segment.index = _next_number;
	}

	_unassembled_bytes = 0;

	if (_buffer.empty()) {
		_buffer.push_back(segment);
		_unassembled_bytes += segment.data.size();
		return;
	}

	std::vector<Segment> new_buffer;
	bool middle_flag = false;

	for (size_t i = 0; i < _buffer.size(); i++) {
		// 如果是segment是_buffer[i]的一个元素的中间元素, 把_buffer[i]放入new_buffer, 同时标记不要把segment放进去
		if (segment.index >= _buffer[i].index && segment.index + segment.data.size() <= _buffer[i].index + _buffer[i].data.size()) {
			new_buffer.push_back(_buffer[i]);
			_unassembled_bytes += _buffer[i].data.size();
			middle_flag = true;
			continue;
		}
		// 如果是_buffer[i]是segment的内部元素, 不管
		if (_buffer[i].index >= segment.index && _buffer[i].index + _buffer[i].data.size() <= segment.index + segment.data.size()) continue;
		// 如果没有交集, 则放入new_buffer
		if (segment.index > _buffer[i].index + _buffer[i].data.size() - 1 || segment.index + segment.data.size() -1 < _buffer[i].index) {
			new_buffer.push_back(_buffer[i]);
			_unassembled_bytes += _buffer[i].data.size();
			continue;
		}
		// 如果是_buffer[i]的左边和segment相交, 合并segment, 继续遍历寻找没有交集的
		if (segment.index + segment.data.size() - 1 >= _buffer[i].index && segment.index + segment.data.size() - 1 <= _buffer[i].index + _buffer[i].data.size() - 1) {
			_buffer[i].data = segment.data + _buffer[i].data.substr(
				segment.index + segment.data.size() - 1 - _buffer[i].index + 1,
				segment.data.size() - (_buffer[i].index + _buffer[i].data.size() - 1 - segment.index + 1));
			_buffer[i].index = segment.index;
			segment = _buffer[i];
			continue;
		}
		// 如果是_buffer[i]的右边和segment相交, 合并segment, 继续遍历
		if (segment.index >= _buffer[i].index) {
			_buffer[i].data = _buffer[i].data + segment.data.substr(
				_buffer[i].index + _buffer[i].data.size() - 1 - segment.index + 1,
				segment.data.size() - (_buffer[i].index + _buffer[i].data.size() - 1 - segment.index + 1));
			segment = _buffer[i];
			continue;
		}
	}
	if (not middle_flag) {
		new_buffer.push_back(segment);
		_unassembled_bytes += segment.data.size();
	}
	_buffer = new_buffer;
	sort(_buffer.begin(), _buffer.end(), sort_function);
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

size_t StreamReassembler::capacity_left() const { return _capacity - _output.buffer_size() - unassembled_bytes(); }

bool StreamReassembler::empty() const { return unassembled_bytes() > 0 ? false : true; }
