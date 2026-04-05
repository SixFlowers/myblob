#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace myblob::utils {

template<typename T>
class DataVector {
private:
    uint64_t _capacity;
    uint64_t _size;
    std::unique_ptr<T[]> _dataOwned;
    T* _data;

public:
    constexpr DataVector() : _capacity(0), _size(0), _data(nullptr) {}
    
    explicit constexpr DataVector(uint64_t cap) : _capacity(0), _size(0), _data(nullptr) {
        resize(cap);
    }
    
    constexpr DataVector(T* start, T* end) : _capacity(0), _size(0), _data(nullptr) {
        assert(end - start >= 0);
        resize(static_cast<uint64_t>(end - start));
        std::memcpy(data(), start, size() * sizeof(T));
    }
    
    constexpr DataVector(T* ptr, uint64_t cap) : _capacity(cap), _size(0) {
        _data = ptr;
    }
    
    DataVector(const DataVector& other) : _capacity(0), _size(0), _data(nullptr) {
        if (other._size > 0) {
            resize(other._size);
            std::memcpy(_data, other._data, other._size * sizeof(T));
        }
    }
    
    DataVector& operator=(const DataVector& other) {
        if (this != &other) {
            _dataOwned.reset();
            _data = nullptr;
            _capacity = 0;
            _size = 0;
            if (other._size > 0) {
                resize(other._size);
                std::memcpy(_data, other._data, other._size * sizeof(T));
            }
        }
        return *this;
    }
    
    DataVector(DataVector&& other) noexcept 
        : _capacity(other._capacity)
        , _size(other._size)
        , _dataOwned(std::move(other._dataOwned))
        , _data(other._data) {
        other._capacity = 0;
        other._size = 0;
        other._data = nullptr;
    }
    
    DataVector& operator=(DataVector&& other) noexcept {
        if (this != &other) {
            _capacity = other._capacity;
            _size = other._size;
            _dataOwned = std::move(other._dataOwned);
            _data = other._data;
            other._capacity = 0;
            other._size = 0;
            other._data = nullptr;
        }
        return *this;
    }
    
public:
    [[nodiscard]] constexpr T* data() { return _data; }
    [[nodiscard]] constexpr const T* cdata() const { return _data; }
    [[nodiscard]] constexpr uint64_t size() const { return _size; }
    [[nodiscard]] constexpr uint64_t capacity() const { return _capacity; }
    
    constexpr void clear() { _size = 0; }
    
    [[nodiscard]] constexpr bool owned() { return (_dataOwned || !_capacity); }
    
    constexpr void reserve(uint64_t cap) {
        if (cap > _capacity) {
            if (!_dataOwned && _capacity) {
                throw std::runtime_error("Pointer not owned, thus size is fixed!");
            }
            auto swap = std::unique_ptr<T[]>(new T[cap]());
            if (_data && _size) {
                std::memcpy(swap.get(), _data, _size * sizeof(T));
            }
            _dataOwned.swap(swap);
            _data = _dataOwned.get();
            _capacity = cap;
        }
    }
    
    constexpr void resize(uint64_t size) {
        if (size > _capacity) {
            reserve(size);
        }
        _size = size;
    }
    
    constexpr void push_back(const T& value) {
        if (_size >= _capacity) {
            resize(_capacity == 0 ? 1 : _capacity * 2);
        }
        _data[_size++] = value;
    }
    
    constexpr T& operator[](uint64_t index) { return _data[index]; }
    constexpr const T& operator[](uint64_t index) const { return _data[index]; }
    
    [[nodiscard]] constexpr std::unique_ptr<T[]> transferBuffer() {
        return std::move(_dataOwned);
    }
};

}  // namespace myblob::utils
