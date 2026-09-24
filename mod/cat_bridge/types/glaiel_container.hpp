#pragma once

#include "utilities/memory.hpp"

#include <cstdint>

// Reconstructions of Mewgenics structures.
//
// Containers library.
//
// polymeric 2026

template<typename T>
struct podvector { // Wookash stream
    uint32_t capacity_;
    uint32_t size_;

    T *data_;

    const T *begin() const {
        return this->data_;
    }

    const T *end() const {
        return this->data_ + this->size_;
    }

    T *begin() {
        return this->data_;
    }

    T *end() {
        return this->data_ + this->size_;
    }

    void push_back(const T &val) {
        if(this->size_ == this->capacity_) {
            uint32_t new_capacity = static_cast<uint32_t>(this->capacity_ * 1.5f);
            if (new_capacity < 2) {
                new_capacity = 2;
            }

            this->data_ = static_cast<T*>(host_realloc(this->data_, static_cast<size_t>(new_capacity) * sizeof(T)));

            this->capacity_ = new_capacity;
        }

        this->data_[this->size_] = val;
        this->size_++;
    }
};

template<typename T>
struct flatset { // Wookash stream
    podvector<T> sorted_;
    podvector<T> back_;
    podvector<T> unsorted_;
    podvector<T> append_;
    bool needs_flatten;

    void insert(const T &val) {
        this->unsorted_.push_back(val);
    }
};

template<typename T, int32_t C>
struct ConstEvalArray {
    T data[C];
    int32_t size;

    constexpr ConstEvalArray() :
        data{}, size(0)
    {}

    constexpr void push_back(const T &val) {
        data[size] = val;
        size++;
    }
};
