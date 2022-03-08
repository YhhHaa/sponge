#include "stream_reassembler.hh"

#include <algorithm>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool sort_function(Substrings seg1, Substrings seg2) { return seg1.index < seg2.index; }

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _buffer(), _capacity(capacity), _next_number(0), _unassembled_bytes(0) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
/* 如果其包含_next_number则将包含的内容写入_output中, 然后更新end_input以及_next_number(write)
** 如果没有包含_next_number, 则将其放入_buffer中, 然后调整_buffer的最长前缀(push_segment)
*/
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
	if (index >= _next_number + capacity_left()) return;
    if (not write_substrings(data, index, eof)) {
        Substrings segment = {data, index, eof};
        push_substrings(segment);
    }
}

/* 判断数据是否可以写入, 如果不可以返回false, 如果可以写入则写入并更新eof以及_next_number */
bool StreamReassembler::write_substrings(const string &data, const size_t index, const bool eof) {
    if (data.empty() && !eof) {
        return true;
    }

	if (eof && data.size() == 0 && index == _next_number) {
		_output.end_input();
		return true;
	}

    size_t wrtsz, real_wrtsz;

    if (index <= _next_number && _next_number <= index + data.size() - 1) {
        wrtsz = (index + data.size() - 1) - _next_number + 1;
        real_wrtsz = _output.write(data.substr(_next_number - index, wrtsz));

        if (eof && real_wrtsz == wrtsz)
            _output.end_input();

        if (real_wrtsz != wrtsz) {
            Substrings segment = {
                data.substr(_next_number - index + real_wrtsz, data.size() - 1 - _next_number - real_wrtsz + 1),
                _next_number + real_wrtsz,
                eof};
            push_substrings(segment);
        }

        _next_number += real_wrtsz;

        find_substrings();
        return true;
    }
    return false;
}

void StreamReassembler::find_substrings() {
    size_t wrtsz, real_wrtsz;

	vector<Substrings>::iterator it = _buffer.begin();

	while (it != _buffer.end()) {
		if (it->index <= _next_number && _next_number <= it->index + it->data.size() - 1) { // 合适的段写入_output
            wrtsz = (it->index + it->data.size() - 1) - _next_number + 1;
            real_wrtsz = _output.write(it->data.substr(_next_number - it->index, wrtsz));
            if (it->eof && real_wrtsz == wrtsz) _output.end_input();
            _next_number += real_wrtsz;
			it = _buffer.erase(it);
        } else if (it->index + it->data.size() - 1 < _next_number) { // 不合适的段删除
			it = _buffer.erase(it);
		} else {
			it++;
		}
	}
}

void StreamReassembler::push_substrings(Substrings &segment) {
    if (segment.index + segment.data.size() - 1 < _next_number)
        return;
    if (segment.index <= _next_number) {
        segment.data = segment.data.substr(_next_number - segment.index,
                                           segment.index + segment.data.size() - 1 - _next_number + 1);
        segment.index = _next_number;
    }

    if (_buffer.empty()) {
        _buffer.push_back(segment);
        return;
    }

	vector<Substrings>::iterator it = _buffer.begin();
	while (true) {
		// 如果遍历到了最后，则将其插入后退出
		if (it == _buffer.end()) {
			_buffer.push_back(segment);
			return;
		}

		// 如果是segment是_buffer[i]的一个元素的中间元素直接退出
        if (segment.index >= it->index &&
            segment.index + segment.data.size() <= it->index + it->data.size()) {
            return;
        }

        // 如果是_buffer[i]是segment的内部元素, 将其删除
        if (it->index >= segment.index &&
            it->index + it->data.size() <= segment.index + segment.data.size()) {
			
			it = _buffer.erase(it);
            continue;
		}
			
        // 如果之前没有交集, 将其插入并退出
        if (segment.index + segment.data.size() - 1 < it->index) {
			_buffer.insert(it, segment);
			return;
        }

		// 如果是之后没有交集，则继续遍历
		if (segment.index > it->index + it->data.size() - 1) {
			it++;
			continue;
		}

        // 如果是_buffer[i]的左边和segment相交, 合并segment, 然后退出
        if (segment.index + segment.data.size() - 1 >= it->index &&
            segment.index + segment.data.size() - 1 <= it->index + it->data.size() - 1) {
            it->data =
                segment.data + it->data.substr(
					segment.index + segment.data.size() - 1 - it->index + 1,
					segment.data.size() - (it->index + it->data.size() - 1 - segment.index + 1));
			it->index = segment.index;
            return;
        }

        // 如果是_buffer[i]的右边和segment相交, 合并segment, 继续遍历
        if (segment.index >= it->index) {
            segment.data = it->data + 
						   segment.data.substr(
							   it->index + it->data.size() - 1 - segment.index + 1,
								segment.data.size() - (it->index + it->data.size() - 1 - segment.index + 1));
			segment.index = it->index;
			it = _buffer.erase(it);
            continue;
        }
	}
}

size_t StreamReassembler::unassembled_bytes() const { 
	size_t bytes = 0;
	for (size_t i = 0; i < _buffer.size(); i++) {
		bytes += _buffer[i].data.size();
	}
	return bytes;
}

size_t StreamReassembler::capacity_left() const { return _capacity - _output.buffer_size(); }

bool StreamReassembler::empty() const { return unassembled_bytes() > 0 ? false : true; }
