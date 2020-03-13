/**
 * Copyright 2020 NEC Laboratories Europe GmbH
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 *    3. Neither the name of NEC Laboratories Europe GmbH nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY NEC Laboratories Europe GmbH AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NEC Laboratories
 * Europe GmbH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO,  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once
#include <stdint.h>
#include <stdio.h>
#include <veintrin.h>

namespace Ve {
class VmReg {
 public:
  VmReg() {}
  VmReg(const __vm256& m) { reg = m; }
  VmReg(bool (&maskbits)[256]) {
    for (int i = 0; i < 4; i++) {
      uint64_t mask64 = 0;
      for (int j = 0; j < 64; j++) {
        mask64 |= maskbits[i * 64 + j] ? (1UL << (63 - j)) : 0;
      }
      reg = _ve_lvm_mmss(reg, i, mask64);
    }
  }
  void WriteToMem(uint64_t* buf) {
    for (int i = 0; i < 4; i++) {
      buf[i] = _ve_svm_sms(reg, i);
    }
  }
  void Dump() {
    bool maskbits[256];
    for (int i = 0; i < 4; i++) {
      uint64_t mask64 = _ve_svm_sms(reg, i);
      for (int j = 0; j < 64; j++) {
        maskbits[i * 64 + j] = (mask64 & (1UL << (63 - j))) != 0;
      }
    }
    printf("0b");
    for (int i = 0; i < 256; i++) {
      putchar(maskbits[i] ? '1' : '0');
    }
    printf("\n");
  }
  uint64_t GetLastFlaggedBit() { return _ve_tovm_sm(reg); }
  uint64_t CountFlags() { return _ve_pcvm_sm(reg); }
  VmReg operator|(const VmReg& m) { return VmReg(_ve_orm_mmm(reg, m.reg)); }
  const __vm256& GetReg() const { return reg; }

 private:
  __vm256 reg;
};
class VrReg {
 public:
  VrReg() {}
  VrReg(const __vr& vr) { reg = vr; }
  VrReg(int64_t i) { reg = _ve_vbrd_vs_i64(i); }
  VrReg(const uint64_t* buf) { reg = _ve_vld_vss(8, buf); }
  VrReg(const char* buf) { reg = _ve_vld_vss(8, buf); }
  void WriteToMem(uint64_t* buf) { _ve_vst_vss(reg, 8, buf); }
  void WriteToMem(char* buf) { _ve_vst_vss(reg, 8, buf); }
  void SetElement(int index, uint64_t value) {
    reg = _ve_lsv_vvss(reg, index, value);
  }
  uint64_t GetElement(int index) { return _ve_lvs_svs_u64(reg, index); }
  static VrReg SeqInit() { return VrReg(_ve_vseq_v()); }
  static VrReg PtrInit(VrReg ptr) { return Ve::VrReg(_ve_vgt_vv(ptr.reg)); }
  void Dump() {
    for (int i = 0; i < 256; i++) {
      printf("%d> 0x%lx\n", i, GetElement(i));
    }
  }
  VrReg MoveWithPos(int i) const { return VrReg(_ve_vmv_vsv(i, reg)); }
  VrReg operator&(const VrReg& r) const {
    return VrReg(_ve_vand_vvv(reg, r.reg));
  }
  VrReg& operator&=(const VrReg& r) {
    reg = (VrReg(reg) & r).reg;
    return *this;
  }
  VrReg operator&(const uint64_t& i) { return VrReg(_ve_vand_vsv(i, reg)); }
  VrReg& operator&=(const uint64_t& i) {
    reg = (VrReg(reg) & i).reg;
    return *this;
  }
  VrReg operator|(const VrReg& r) { return VrReg(_ve_vor_vvv(reg, r.reg)); }
  VrReg& operator|=(const VrReg& r) {
    reg = (VrReg(reg) | r).reg;
    return *this;
  }
  VrReg operator>>(const uint64_t& i) { return VrReg(_ve_vsrl_vvs(reg, i)); }
  VrReg& operator>>=(const uint64_t& i) {
    reg = (VrReg(reg) >> i).reg;
    return *this;
  }
  VrReg operator<<(const uint64_t& i) { return VrReg(_ve_vsll_vvs(reg, i)); }
  VrReg& operator<<=(const uint64_t& i) {
    reg = (VrReg(reg) << i).reg;
    return *this;
  }
  VrReg operator+(const uint64_t& i) { return VrReg(_ve_vaddul_vsv(i, reg)); }
  VrReg& operator+=(const uint64_t& i) {
    reg = (VrReg(reg) + i).reg;
    return *this;
  }
  VrReg operator+(const VrReg& r) { return VrReg(_ve_vaddul_vvv(r.reg, reg)); }
  VrReg& operator+=(const VrReg& r) {
    reg = (VrReg(reg) + r).reg;
    return *this;
  }
  VrReg operator-(const VrReg& r) { return VrReg(_ve_vsubul_vvv(reg, r.reg)); }
  VrReg& operator-=(const VrReg& r) {
    reg = (VrReg(reg) - r).reg;
    return *this;
  }
  VrReg operator/(const uint64_t& i) { return VrReg(_ve_vdivul_vvs(reg, i)); }
  VrReg& operator/=(const uint64_t& i) {
    reg = (VrReg(reg) / i).reg;
    return *this;
  }
  VrReg operator*(const uint64_t& i) { return VrReg(_ve_vmulul_vsv(i, reg)); }
  VrReg& operator*=(const uint64_t& i) {
    reg = (VrReg(reg) * i).reg;
    return *this;
  }
  VrReg operator%(const uint64_t& i) {
    return VrReg(reg) - ((VrReg(reg) / i) * i);
  }
  VrReg& operator%=(const uint64_t& i) {
    reg = (VrReg(reg) % i).reg;
    return *this;
  }
  VmReg Equal(const uint64_t& i) {
    __vr vr = _ve_vcmpul_vsv(i, reg);
    return VmReg(_ve_vfmkl_mcv(0x3, vr));
  }
  VmReg Equal(const VrReg& r) {
    __vr vr = _ve_vcmpul_vvv(r.GetReg(), reg);
    return VmReg(_ve_vfmkl_mcv(0x4, vr));
  }
  void TakeCompressedResult(VrReg source, VmReg mask) {
    reg = _ve_vcp_vvmv(source.GetReg(), mask.GetReg(), reg);
  }
  void TakeExpandedResult(VrReg source, VmReg mask) {
    reg = _ve_vex_vvmv(source.GetReg(), mask.GetReg(), reg);
  }
  const __vr& GetReg() const { return reg; }

 private:
  __vr reg;
};
}  // namespace Ve
