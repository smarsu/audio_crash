// Copyright (c) 2020 smarsufan. All Rights Reserved.

#define VERSION "0.0.22"

#include <sys/resource.h> 

inline uint64_t tr() {
  struct rlimit lmt; 
  getrlimit(RLIMIT_STACK, &lmt); 
  return lmt.rlim_cur;
}

#include <sys/time.h>

inline double time() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000. + tv.tv_usec / 1000.;
}

class Buffer {
 public:
  Buffer(int size = 0) {
    resize(size);
  }

  ~Buffer() {
    if (data_) {
      delete[] data_;
    }
  }

  uint8_t *data() { return data_; }

  size_t size() { return size_; }

  size_t capacity() { return capacity_; }

  void resize(size_t size) {
    size_ = size;
    if (size_ > capacity_) {
      if (data_) {
        delete[] data_;
      }
      data_ = new uint8_t[size_];
      capacity_ = size_;
    }
  }

 private:
  uint8_t *data_{nullptr};
  size_t size_{0};
  size_t capacity_{0};
};
