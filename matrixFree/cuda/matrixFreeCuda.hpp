#pragma once

#include <assert.h>
#include <complex>

#include "../matrixFreeInterface/cuda_wrapper.hpp"
#include "../matrixFreeInterface/matrixFreeInterface.hpp"
#include "../matrixFreeInterface/template_wrapper.hpp"

template <class T>
class MatrixFreeCuda : public MatrixFreeInterface<T> {
 public:
  MatrixFreeCuda(T* H, std::size_t n, std::size_t maxBlock) : n_(n) {
    cuda_exec(cudaMalloc(&(V1_), n_ * maxBlock * sizeof(T)));
    cuda_exec(cudaMalloc(&(V2_), n_ * maxBlock * sizeof(T)));
    cuda_exec(cudaMalloc(&(H_), n_ * n_ * sizeof(T)));

    OrigH_ = H;

    std::size_t pitch_host = n_ * sizeof(T);
    std::size_t pitch_device = n_ * sizeof(T);

    cuda_exec(cudaSetDevice(0));
    cublasCreate(&handle_);
    cuda_exec(cudaStreamCreate(&stream_));
    cublasSetStream(handle_, stream_);

    cuda_exec(cudaMemcpy2D(H_, pitch_device, OrigH_, pitch_host, n_ * sizeof(T),
                           n_, cudaMemcpyHostToDevice));
  }

  ~MatrixFreeCuda() {}

  void preApplication(T* V, std::size_t locked, std::size_t block) {
    locked_ = locked;
    std::size_t pitch_host = n_ * sizeof(T);
    std::size_t pitch_device = n_ * sizeof(T);

    cuda_exec(cudaMemcpy2D(V1_, pitch_device, V + locked * n_, pitch_host,
                           n_ * sizeof(T), block, cudaMemcpyHostToDevice));
  }

  void preApplication(T* V1, T* V2, std::size_t locked, std::size_t block) {
    std::size_t pitch_host = n_ * sizeof(T);
    std::size_t pitch_device = n_ * sizeof(T);

    cuda_exec(cudaMemcpy2D(V2_, pitch_device, V2 + locked * n_, pitch_host,
                           n_ * sizeof(T), block, cudaMemcpyHostToDevice));

    this->preApplication(V1, locked, block);
  }

  void apply(T alpha, T beta, std::size_t offset, std::size_t block) {
    cublasTgemm(handle_, CUBLAS_OP_N, CUBLAS_OP_N, n_, block, n_, &alpha, H_,
                n_, V1_ + offset * n_, n_, &beta, V2_ + offset * n_, n_);
    std::swap(V1_, V2_);
  }

  bool postApplication(T* V, std::size_t block) {
    std::size_t pitch_host = n_ * sizeof(T);
    std::size_t pitch_device = n_ * sizeof(T);
    cuda_exec(cudaMemcpy2D(V + locked_ * n_, pitch_host, V1_, pitch_device,
                           n_ * sizeof(T), block, cudaMemcpyDeviceToHost));
    return false;
  }

  void shiftMatrix(T c) {
    for (std::size_t i = 0; i < n_; ++i) {
      OrigH_[i + i * n_] += c;
    }

    std::size_t pitch_host = n_ * sizeof(T);
    std::size_t pitch_device = n_ * sizeof(T);

    cuda_exec(cudaMemcpy2D(H_, pitch_device, OrigH_, pitch_host, n_ * sizeof(T),
                           n_, cudaMemcpyHostToDevice));
  }

  void applyVec(T* B, T* C) {
    T alpha = T(1.0);
    T beta = T(0.0);

    t_gemm(CblasColMajor, CblasNoTrans, CblasNoTrans, n_, 1, n_, &alpha, OrigH_,
           n_, B, n_, &beta, C, n_);
  }

  void get_off(CHASE_INT* xoff, CHASE_INT* yoff, CHASE_INT* xlen,
               CHASE_INT* ylen) const override {
    *xoff = 0;
    *yoff = 0;
    *xlen = n_;
    *ylen = n_;
  }

  T* get_H() const override { return OrigH_; }

 private:
  std::size_t n_;
  std::size_t locked_;
  T* V1_;
  T* V2_;
  T* H_;
  T* OrigH_;
  cudaStream_t stream_;
  cublasHandle_t handle_;
};
