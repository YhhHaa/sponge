#include "byte_stream.hh"

#include <algorithm>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

/* 初始化变量 */
ByteStream::ByteStream(const size_t capacity)
    : _error(false), _eof_flag(false), _byte_deque(), _capacity(capacity), _bytes_written(0), _bytes_read(0) {}

size_t ByteStream::write(const string &data) {
    if (_eof_flag) {
        return 0;
    }

    size_t accepted_len = min(this->remaining_capacity(), data.size());
    for (size_t i = 0; i < accepted_len; i++) {
        _byte_deque.push_back(data[i]);
    }

    _bytes_written += accepted_len;

    return accepted_len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t pop_size = min(len, _byte_deque.size());
    return string(_byte_deque.begin(), _byte_deque.begin() + pop_size);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t remove_size = min(len, _byte_deque.size());
    for (size_t i = 0; i < remove_size; i++) {
        _byte_deque.pop_front();
    }
    _bytes_read += remove_size;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string res = this->peek_output(len);
    this->pop_output(len);
    return res;
}

void ByteStream::end_input() { _eof_flag = true; }

bool ByteStream::input_ended() const { return _eof_flag; }

size_t ByteStream::buffer_size() const { return _byte_deque.size(); }

bool ByteStream::buffer_empty() const { return _byte_deque.empty(); }

bool ByteStream::eof() const { return _eof_flag && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _byte_deque.size(); }
