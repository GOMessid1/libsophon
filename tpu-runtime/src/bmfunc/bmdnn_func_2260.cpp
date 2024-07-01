#include "bmfunc/bmfunc.h"
#include <iostream>

namespace bmruntime {
extern "C" bm_status_t bm_send_api_to_core(
  bm_handle_t  handle,
  int api_id,
  const u8     *api,
  u32          size,
  int          core_id);

void bmdnn_func_2260::fill_api_info(const tpu_net_info_t &net_info,
                                    api_info_t &api_info) {
  BMRT_ASSERT_INFO(net_info.neuron_start_addr.size() == 1,
                   "only support one neuron addr");
  const std::vector<tpu_tensor_info_t> &input_info = net_info.input_info;
  const std::vector<tpu_tensor_info_t> &output_info = net_info.output_info;
  api_info.api_data.resize(net_info.core_commands.size());
  int base_message_id = 0;
  for (auto core_id : net_info.core_list) {
    base_message_id |= (1 << core_id);
  }

  for (size_t core_idx = 0; core_idx < net_info.core_list.size(); core_idx++) {
    const std::vector<tpu_cmd_info_t> &cmd_info =
        net_info.core_commands[core_idx].cmd_info;

    u32 api_buffer_size =
        sizeof(int) +
        (input_info.size() * (sizeof(u64) * 2 + sizeof(u32))) + // input
        sizeof(int) +
        (output_info.size() * (sizeof(u64) * 2 + sizeof(u32))) + // output
        sizeof(u64) * 2 +
        (sizeof(int) * 2 + sizeof(u32) * 2) * cmd_info.size() + sizeof(int) +
        2 * sizeof(u64) + sizeof(int) + // base message id
        2 * sizeof(u64); // hau_cmd_addr, sdma_cmd_addr
    api_info.api_id.push_back(BM_API_ID_MULTI_FULLNET);
    api_info.api_data[core_idx].assign(api_buffer_size, 0);
    api_info.input_addr_offset.assign(input_info.size(), 0);
    api_info.output_addr_offset.assign(output_info.size(), 0);

    void *p_api = api_info.api_data[core_idx].data();
    // input global offset process
    *(int *)p_api = input_info.size();
    p_api = (int *)p_api + 1;
    for (size_t i = 0; i < input_info.size(); ++i) {
      const auto &info = input_info.at(i);
      api_info.input_addr_offset.at(i) =
          (uint8_t *)p_api - (uint8_t *)(api_info.api_data.data());
      *(u64 *)p_api = info.user_global_addr;
      p_api = (u64 *)p_api + 1;
      if (core_idx > 0) {
        /// If the bmodel use multi core, we only move the user's input data to
        /// compiled ddr once.
        *(u64 *)p_api = info.user_global_addr;
      } else {
        *(u64 *)p_api = info.compiled_global_addr;
      }
      p_api = (u64 *)p_api + 1;
      *(u32 *)p_api = bmrt_data_type_size((bm_data_type_t)info.dtype) *
                      (info.n * info.c * info.h * info.w);
      p_api = (u32 *)p_api + 1;
    }

    // output global offset process
    *(int *)p_api = output_info.size();
    p_api = (int *)p_api + 1;
    for (size_t i = 0; i < output_info.size(); ++i) {
      const auto &info = output_info.at(i);
      api_info.output_addr_offset.at(i) =
          (uint8_t *)p_api - (uint8_t *)(api_info.api_data.data());
      *(u64 *)p_api = info.user_global_addr;
      p_api = (u64 *)p_api + 1;
      if (core_idx > 0) {
        /// If the bmodel use multi core, we only move the user's input data to
        /// compiled ddr once.
        *(u64 *)p_api = info.user_global_addr;
      } else {
        *(u64 *)p_api = info.compiled_global_addr;
      }
      p_api = (u64 *)p_api + 1;
      *(u32 *)p_api = bmrt_data_type_size((bm_data_type_t)info.dtype) *
                      (info.n * info.c * info.h * info.w);
      p_api = (u32 *)p_api + 1;
    }

    // memcpy cmd offset and num
    *(u64 *)p_api = net_info.core_commands[core_idx].bdc_cmd_addr;
    p_api = (u64 *)p_api + 1;
    *(u64 *)p_api = net_info.core_commands[core_idx].gdma_cmd_addr;
    p_api = (u64 *)p_api + 1;
    *(int *)p_api = cmd_info.size();
    p_api = (int *)p_api + 1;
    for (size_t i = 0; i < cmd_info.size(); i++) {
      const tpu_cmd_info_t info = cmd_info.at(i);
      *(int *)p_api = info.bdc_cmd_num;
      p_api = (int *)p_api + 1;
      *(int *)p_api = info.gdma_cmd_num;
      p_api = (int *)p_api + 1;
      *(u32 *)p_api = info.bdc_cmd_byte_size;
      p_api = (u32 *)p_api + 1;
      *(u32 *)p_api = info.gdma_cmd_byte_size;
      p_api = (u32 *)p_api + 1;
    }

    *((u64 *)p_api) = net_info.coeff_start_addr;
    p_api = ((u64 *)p_api) + 1;
    *((u64 *)p_api) = net_info.neuron_start_addr[0];
    p_api = ((u64 *)p_api) + 1;
    *((int *)p_api) = base_message_id;
    p_api = ((u32 *)p_api) + 1;

    *((u64 *)p_api) = net_info.core_commands[core_idx].hau_cmd_addr;
    p_api = ((u64 *)p_api) + 1;
    *((u64 *)p_api) = net_info.core_commands[core_idx].sdma_cmd_addr;
    p_api = ((u64 *)p_api) + 1;
  }
}
bm_status_t
bmdnn_func_2260::_bmdnn_multi_fullnet_(bm_handle_t handle,
                                       const tpu_net_info_t &net_info) {
  BMRT_ASSERT_INFO(handle, "handle shouldn't be NULL\n");

  api_info_t api_info;
  fill_api_info(net_info, api_info);
  bm_status_t status = BM_SUCCESS;
  if (api_info.api_data[0].size()<MAX_API_MSG_SIZE){
    for (size_t core_idx = 0; core_idx < net_info.core_list.size(); core_idx++) {
      bm_status_t core_status = bm_send_api_to_core(
        handle, (bm_api_id_t)api_info.api_id[0],
        api_info.api_data[core_idx].data(),
        api_info.api_data[core_idx].size(),
        net_info.core_list.at(core_idx));
      if (BM_SUCCESS != core_status) {
        status = (status == BM_SUCCESS) ? core_status : status;
        BMRT_LOG(WRONG, "bm_send_api failed, api id:%d, status:%d",
                BM_API_ID_MULTI_FULLNET, core_status);
        }
    }
  } else {
    std::vector<bm_device_mem_t> api_mem(net_info.core_list.size());
    #pragma pack(1)
    typedef struct{
        u32 input_num = 0;
        u64 cmd_addr;
        u64 cmd_size;
      }long_cmd_param_t;
    #pragma pack()
    for (size_t core_idx = 0; core_idx < net_info.core_list.size(); core_idx++) {
      u32 malloc_size = api_info.api_data[core_idx].size();
      bm_status_t mem_status = bm_malloc_device_byte(handle, &api_mem[core_idx], malloc_size);
      if (mem_status != BM_SUCCESS) {
          status = (status == BM_SUCCESS) ? mem_status : status;
          BMRT_LOG(WRONG, "bm_malloc_device_byte failed, malloc mem:%d", malloc_size);
          }
      long_cmd_param_t new_api;
      auto data = api_info.api_data[core_idx].data();
      bm_status_t s2d_status = bm_memcpy_s2d(handle, api_mem[core_idx], (void*)data);
      new_api.cmd_addr = api_mem[core_idx].u.device.device_addr;
      printf("command_addr runtime: %lld\n",new_api.cmd_addr);
      new_api.cmd_size = api_info.api_data[core_idx].size();
      if (BM_SUCCESS != s2d_status) {
        status = (status == BM_SUCCESS) ? s2d_status : status;
        BMRT_LOG(WRONG, "bm_memcpy_s2d failed, ret = %d\n", s2d_status);
      }
      bm_status_t core_status = bm_send_api_to_core(
          handle, (bm_api_id_t)api_info.api_id[0],
          (u8 *)(&new_api),
          sizeof(new_api),
          net_info.core_list.at(core_idx));
      if (BM_SUCCESS != core_status) {
        status = (status == BM_SUCCESS) ? core_status : status;
        BMRT_LOG(WRONG, "bm_send_api failed, api id:%d, status:%d",
                BM_API_ID_MULTI_FULLNET, status);
      }
  }
    for (size_t core_idx = 0; core_idx < net_info.core_list.size(); core_idx++) {
      bm_status_t core_status = bm_thread_sync_from_core(handle, core_idx);
      if (core_status != BM_SUCCESS) {
        status = (status == BM_SUCCESS) ? core_status : status;
        BMRT_LOG(WRONG, "bm_thread_sync_from_core failed, core_idx:%d", core_idx);
        }
      }
    for (size_t core_idx = 0; core_idx < net_info.core_list.size(); core_idx++) {
      bm_free_device(handle, api_mem[core_idx]);
    }
  }
  return status;
}

bm_status_t bmdnn_func_2260::_bmdnn_dynamic_fullnet_(
        bm_handle_t handle,
        unsigned long long compiled_ir_global_addr,
        unsigned int compiled_ir_length, //unit dword
        unsigned int input_num,
        const unsigned long long *input_addrs,
        const int * const * input_shapes,
        const int * input_elem_nums,
        const int * input_dtype_and_dims,
        unsigned int output_num,
        const unsigned long long *output_addrs,
        unsigned long long apd_ctx_start,
        std::vector<unsigned long long> apd_ctx_mem_borders,
        std::vector<unsigned long long> apd_ctx_mem_offset,
        unsigned long long apd_coeff_mem_offset,
        unsigned long long apd_io_start,
        unsigned long long apd_io_mem_offset,
        bool get_output_shape,
        unsigned long long output_shape_global_addr,
        const std::vector<int32_t> &core_list)
{
    BMRT_ASSERT_INFO(core_list.size() == 1, "Dynamic compile do not support tensor parallel\n");
    BMRT_ASSERT_INFO(handle,"handle shouldn't be NULL\n");
    BMRT_ASSERT_INFO(
        apd_ctx_mem_borders.size() == apd_ctx_mem_offset.size(),
        "ctx borders and offset should have same size");

     size_t ctx_num = apd_ctx_mem_borders.size();
     u32 api_buffer_size = sizeof(u64) +sizeof(u32) +  // compiled_ir addr, length
                           // input num
                           sizeof(u32) +
                           //           input_addr    dtype_dims        dim_shape                    elem_num
                           input_num * (sizeof(u64) + sizeof(int) + sizeof(int) * BM_MAX_DIMS_NUM + sizeof(int)) +
                           // output num
                           sizeof(u32) +
                           //           output_addr
                           output_num * sizeof(u64) +
                           //get_output_shape, global_shape_mem_addr, apd_ctx_start, (ctx_num, apd_ctx_mem_borders, apd_ctx_mem_offset),
                           sizeof(u32) + sizeof(u64) + sizeof(u64) + ( sizeof(u32)+sizeof(u64)*ctx_num*2 ) +
                           //apd_coeff_mem_offset, apd_io_start, apd_io_mem_offset
                           sizeof(u64) + sizeof(u64) + sizeof(u64);

     if (api_buffer_size > MAX_API_MSG_SIZE) {
       //decrease the api buffer size
       for (u32 i = 0; i < input_num; ++i) {
         u32 cur_dim = (u32)(input_dtype_and_dims[i] & 0xFFFF);
         api_buffer_size -= (BM_MAX_DIMS_NUM - cur_dim) * sizeof(int);
       }
     }

     u8* api_buffer = new u8 [api_buffer_size];

     void* p_api = api_buffer;
     //compiled ir information
     *(u64*)p_api = compiled_ir_global_addr;
     p_api = (u64*)p_api + 1;
     *(u32*)p_api = compiled_ir_length;
     p_api = (u32*)p_api + 1;

     //input information
     *(u32*)p_api = input_num;
     p_api = (u32*)p_api + 1;

     for(u32 i = 0; i < input_num; ++i){
       *(u64*)p_api = input_addrs[i];
       p_api = (u64*)p_api + 1;

       *(u32*)p_api = input_dtype_and_dims[i];
       p_api = (u32*)p_api + 1;
       u32 cur_dim = (u32)(input_dtype_and_dims[i] & 0xFFFF);
       for(u32 j = 0; j < cur_dim; j++){
         *(u32 *)p_api = (u32)input_shapes[i][j];
         p_api = (u32 *)p_api + 1;
       }
       *(u32*)p_api = input_elem_nums[i];
       p_api = (u32*)p_api + 1;
     }
     //output information
     *(u32*)p_api = output_num;
     p_api = (u32*)p_api + 1;

     for(u32 i = 0; i < output_num; ++i){
       *(u64*)p_api = output_addrs[i];
       p_api = (u64*)p_api + 1;
     }
     //output shape info related
     *(u32*)p_api = (u32)get_output_shape;
     p_api = (u32*)p_api + 1;
     *(u64*)p_api = output_shape_global_addr;
     p_api = (u64*)p_api + 1;

     //The memory address in cmd gdma need to be offset when append context,here is the offset value.
     *(u64*)p_api = apd_ctx_start;
     p_api = (u64*)p_api + 1;

     *(u32*)p_api = ctx_num;
     p_api = (u32*)p_api + 1;

     for (size_t i = 0; i < ctx_num; ++i)
     {
       *(u64*)p_api = apd_ctx_mem_borders[i];
       p_api = (u64*)p_api + 1;
     }
     for (size_t i = 0; i < ctx_num; ++i)
     {
       *(u64*)p_api = apd_ctx_mem_offset[i];
       p_api = (u64*)p_api + 1;
     }

     *(u64*)p_api = apd_coeff_mem_offset;
     p_api = (u64*)p_api + 1;

     *(u64*)p_api = apd_io_start;
     p_api = (u64*)p_api + 1;
     *(u64*)p_api = apd_io_mem_offset;
     p_api = (u64*)p_api + 1;

    bm_status_t status;
    if (api_buffer_size<MAX_API_MSG_SIZE){
      status = bm_send_api(handle, (bm_api_id_t)BM_API_ID_DYNAMIC_FULLNET, api_buffer, api_buffer_size);
    } else {
      bm_device_mem_t api_mem;
      #pragma pack(1)
      typedef struct{
          u64 compiled_addr = 0;
          u64 cmd_addr;
          u64 cmd_size;
        }long_cmd_param_t;
      #pragma pack()
      bm_status_t mem_status = bm_malloc_device_byte(handle, &api_mem, api_buffer_size);
      if (mem_status != BM_SUCCESS) {
        BMRT_LOG(WRONG, "bm_malloc_device_byte failed, malloc mem:%d", api_buffer_size);
          }
      long_cmd_param_t new_api;
      bm_status_t s2d_status = bm_memcpy_s2d(handle, api_mem, (void*)api_buffer);
      new_api.cmd_addr = api_mem.u.device.device_addr;
      new_api.cmd_size = api_buffer_size;
      if (BM_SUCCESS != s2d_status) {
        BMRT_LOG(WRONG, "bm_memcpy_s2d failed, ret = %d\n", s2d_status);
        }
      status =
        bm_send_api(handle, (bm_api_id_t)BM_API_ID_DYNAMIC_FULLNET, (u8 *)(&new_api),sizeof(new_api));
    }

     if (BM_SUCCESS != status) {
       BMRT_LOG(WRONG, "bm_send_api failed, api id:%d, status:%d", BM_API_ID_DYNAMIC_FULLNET, status);
     } else {
       status = bm_sync_api(handle);
       if (BM_SUCCESS != status) {
         BMRT_LOG(WRONG, "bm_sync_api failed, api id:%d, status:%d", BM_API_ID_DYNAMIC_FULLNET, status);
       }
     }

     bm_gmem_arm_reserved_release(handle);

     delete[] api_buffer;
     return status;
}

bm_status_t  bmdnn_func_2260::_bmdnn_set_profile_enable_(bm_handle_t handle, unsigned int enable){
     BMRT_ASSERT_INFO(handle,"handle shouldn't be NULL\n");
     u32 api_buffer_size = sizeof(u32);
     u32 profile_enable = enable;
     bm_status_t status = bm_send_api(handle, (bm_api_id_t)BM_API_ID_SET_PROFILE_ENABLE, (u8*)&profile_enable, api_buffer_size);
     if (BM_SUCCESS != status) {
       BMRT_LOG(WRONG, "bm_send_api failed, api id:%d, status:%d", BM_API_ID_SET_PROFILE_ENABLE, status);
     }
     return status;
}
bm_status_t bmdnn_func_2260::_bmdnn_get_profile_data_(
        bm_handle_t handle,
        unsigned long long output_global_addr,
        unsigned int output_max_size,
        unsigned int byte_offset,
        unsigned int data_category //0: profile time records, 1: extra data
        ){
      BMRT_ASSERT_INFO(handle,"handle shouldn't be NULL\n");
#pragma pack(1)
     struct {
      u64 arm_reserved_addr;
      u64 output_global_addr;
      u32 output_size;
      u32 byte_offset;
      u32 data_category; //0: profile_data, 1: profile extra data
     } api_data;
#pragma pack()

     const u32 api_buffer_size = sizeof(api_data);

     api_data.arm_reserved_addr = -1;
     api_data.output_global_addr = output_global_addr;
     api_data.output_size = output_max_size;
     api_data.byte_offset = byte_offset;
     api_data.data_category = data_category;

     bm_api_id_t api_code = (bm_api_id_t)BM_API_ID_GET_PROFILE_DATA;
     bm_status_t status =
         bm_send_api(handle, api_code, (u8*)&api_data, api_buffer_size);
     if (BM_SUCCESS != status) {
       BMRT_LOG(WRONG, "bm_send_api failed, api id:%d, status:%d", api_code, status);
     } else {
       status = bm_sync_api(handle);
       if (BM_SUCCESS != status) {
         BMRT_LOG(WRONG, "bm_sync_api failed, api id:%d, status:%d", api_code, status);
       }
     }
     return status;
}

}
