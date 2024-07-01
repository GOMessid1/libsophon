/*
 * Copyright Bitmain Technologies Inc.
 * Written by:
 *   Pengchao Hu <pengchao.hu@bitmain.com>
 * Created Time: 2018-12-07 15:34
 */
#ifndef LIBBMODEL_HPP_
#define LIBBMODEL_HPP_

#include <stdint.h>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "model_generated.h"

namespace bmodel {
#ifdef __linux__
typedef struct {
  uint32_t magic;
  uint32_t header_size;
  uint32_t flatbuffers_size;
  uint32_t binary_size;
  uint32_t reserved[12];
} __attribute__((packed)) MODEL_HEADER_T;
#else
#pragma pack(push, 1)
typedef struct {
  uint32_t magic;
  uint32_t header_size;
  uint32_t flatbuffers_size;
  uint32_t binary_size;
  uint32_t reserved[12];
} MODEL_HEADER_T;
#pragma pack(pop)
#endif

typedef struct {
   uint64_t bd_cmd_mem_size;       // bd instruction total size
   uint64_t gdma_cmd_mem_size;     // gdma instruction total size
   uint64_t hau_cmd_mem_size;      // hau instruction total size
   uint64_t sdma_cmd_mem_size;     // sdma instruction totoal size
   uint64_t dynamic_ir_mem_size;   // dynamic ir total size
   uint64_t neuron_mem_size;       // total neuron mem
   uint64_t coeff_mem_size;        // total coeff size
   uint64_t middle_buffer_size;    // max input and output byte size
   uint64_t host_neuron_mem_size;  // total mem size for cpu layer IO on host
   uint64_t host_coeff_mem_size;   // total mem size for cpu layer coeff on host
} bmodel_mem_info_t;

const int SHA256_LEN = 32;
void CalcSha256(const uint8_t *buffer, uint64_t size, uint8_t sha256[SHA256_LEN]);

class ModelGen {
public:
  typedef struct {
    int64_t device_id;
    int64_t step;
    std::string main_name; // name for Cascade
  } CASCADE_INFO_T;

public:
  ModelGen(uint32_t reserved_size = 0x1000000);
  virtual ~ModelGen();
  flatbuffers::FlatBufferBuilder &Builder();
  Binary WriteBinary(size_t size, uint8_t *data);

  // add model elements
  void AddChip(const std::string &arch_name);
  void AddNumDevice(int num_device);
  void AddNet(const flatbuffers::Offset<Net> &net);
  void AddNet(std::string net_name, const flatbuffers::Offset<NetParameter> &parameter,
              uint32_t *net_idx = NULL, uint32_t *stage_idx = NULL,
              const bmodel::Cascade * cascade = NULL, int32_t addr_mode = 0);
  void AddNet(const std::string &net_name, const CASCADE_INFO_T &cascade,
              const flatbuffers::Offset<NetParameter> &parameter, int32_t addr_mode = 0);
  // void AddTpuModule(Binary tpu_module);
  void AddKernelModule(std::string &version, Binary &tpu_module);
  void AddCpuModule(std::string &version, Binary &lib_cpu);

  // finish and save to file
  void Finish(const std::string &filename);

  // finish and return size, but no save
  size_t Finish();
  void Save(const std::string &filename);  // save to file
  void Save(void *buffer);                 // save to buffer
  uint8_t *GetBufferPointer();

private:
  bool IsTensorConflict(const flatbuffers::Vector<flatbuffers::Offset<Tensor>> *,
                        const flatbuffers::Vector<flatbuffers::Offset<Tensor>> *);
  bool IsShapeSame(const Shape *, const Shape *);

  typedef struct {
    std::string name;
    CASCADE_INFO_T cascade;
    std::vector<flatbuffers::Offset<NetParameter>> parameters;
    int32_t addr_mode;
  } NET_INFO_T;

  typedef struct {
    std::string file_name;
    Binary binary;
  } KERNEL_MODULE_T;

  typedef struct {
    std::string file_name;
    Binary binary;
  } CPUOP_MODULE_T;

  std::string chip_;
  int num_device_;
  flatbuffers::FlatBufferBuilder builder_;
  std::vector<uint8_t> binary_;
  std::vector<Binary> binary_vector_;
  std::vector<NET_INFO_T> net_vector_;
  std::vector<flatbuffers::Offset<bmodel::Net>> nets_;
  uint64_t max_neuron_size_;
  // Binary tpu_module_;
  KERNEL_MODULE_T kernel_module_;
  CPUOP_MODULE_T cpuop_module_;
};

class ModelCtx {
 public:
  ModelCtx(const std::string &filename);
  ModelCtx(const void *bmodel_data, size_t size);
  virtual ~ModelCtx();
  operator bool();

  const Model *model();
  // read binary data to buffer
  void read_binary(const bmodel::Binary *binary, uint8_t *buffer);
  // read binary from offset
  void read_binary(const bmodel::Binary *binary, uint64_t offset, uint8_t *buffer, uint64_t size);
  // write buffer to binary
  void write_binary(const bmodel::Binary *binary, uint8_t *buffer);
  // write buffer to offset of binary
  void write_binary(const bmodel::Binary *binary, uint64_t offset,
                    uint8_t *buffer, uint64_t size);

  // model buffer data for parse
  const void *data() const;

  const MODEL_HEADER_T &header() const;

  bool get_weight(const std::string &net_name, int stage_idx, uint64_t offset,
                  Binary &bin, std::string &op_name) const;

  bmodel_mem_info_t get_bmodel_mem_info();
 protected:
  void update_bmodel();
  void update_net(const std::string &net_name,
                  const flatbuffers::Vector<flatbuffers::Offset<NetStatic>> *net_static);
  void update_net(const std::string &net_name,
                  const flatbuffers::Vector<flatbuffers::Offset<NetDynamic>> *net_dynamic);


 private:
  MODEL_HEADER_T header_;
  ModelGen *model_gen_;
  const Model *model_;
  void *model_buffer_;
  uint64_t binary_offset_;
  std::fstream file_;          // bmodel in file
  const void *bmodel_pointer_;  // bmodel in buffer
};

}  // namespace bmodel

#endif  // LIBBMODEL_HPP_
