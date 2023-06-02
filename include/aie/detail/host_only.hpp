
#ifndef AIE_DETAIL_HOST_ONLY_HPP
#define AIE_DETAIL_HOST_ONLY_HPP

#if defined(__AIE_EMULATION__) || defined(__AIE_FALLBACK__) || defined(__SYCL_DEVICE_ONLY__)
#error "should only be used on the host side of hardware execution"
#endif

#include "common.hpp"
#include "device_and_host.hpp"
#include "sync.hpp"

namespace aie::detail {

using device_tile_impl = device_tile_impl_fallback;

struct device_mem_handle_impl : device_mem_handle_impl_fallback {
  /// Although it require using some preprocessor. having access to the handle
  /// enable writing a much wider range of services. so it is left public
  xaie::handle handle;
  device_mem_handle_impl(xaie::handle h) : handle(h) {}
  void memcpy_h2d(generic_ptr<void> dst, void* src, uint32_t size) {
    handle.on(dst.ptr.get_dir()).memcpy_h2d(dst.ptr, src, size);
  }
  void memcpy_d2h(void* dst, generic_ptr<void> src, uint32_t size) {
    handle.on(src.ptr.get_dir()).memcpy_d2h(dst, src.ptr.get_offset(), size);
  }
};

using device_mem_handle = device_mem_handle_adaptor<device_mem_handle_impl>;

namespace aiev1 = xaie::aiev1;

struct device_impl : device_impl_fallback {
  struct handle_impl {
    /// The host needs to set up the device when executing on real device.
    /// The following XAie_SetupConfig and XAie_InstDeclare are actually macros
    /// that declare variables, in our case member variables. The "xaie::"
    /// before them is because their type is in the namespace "xaie::". This
    /// declares \c aie_config of type \c xaie::XAie_Config.
    xaie::XAie_SetupConfig(aie_config, aiev1::dev_gen, aiev1::base_addr,
                           aiev1::col_shift, aiev1::row_shift,
                           aiev1::num_hw_col, aiev1::num_hw_row,
                           aiev1::num_shim_row, aiev1::mem_tile_row_start,
                           aiev1::mem_tile_row_num, aiev1::aie_tile_row_start,
                           aiev1::aie_tile_row_num);
    /// This declares \c aie_inst of type \c xaie::XAie_InstDeclare
    xaie::XAie_InstDeclare(aie_inst, &aie_config);
    handle_impl(int x, int y) {
      TRISYCL_XAIE(xaie::XAie_CfgInitialize(&aie_inst, &aie_config));
    }
  };

  handle_impl impl;
  std::vector<void*> memory;
  int sizeX;
  int sizeY;

  void*& get_mem(hw::position pos) {
    assert(pos.x < sizeX && pos.y < sizeY);
    return memory[pos.x * sizeY + pos.y];
  }

  xaie::handle get_handle(hw::position pos) {
    return xaie::handle(xaie::acap_pos_to_xaie_pos(pos), &impl.aie_inst);
  }

  device_impl(int x, int y)
      : impl(x, y) {
    /// Cleanup device if the previous use was not cleanup
    TRISYCL_XAIE(xaie::XAie_Finish(&impl.aie_inst));

    /// Re-access the device after the cleanup.
    impl = handle_impl(x, y);
    /// Request access to all tiles.
    TRISYCL_XAIE(xaie::XAie_PmRequestTiles(&impl.aie_inst, nullptr, 0));
    // Initialize all the tiles with their network connections first
    memory.assign(x * y, nullptr);
    sizeX = x;
    sizeY = y;
    // service_system = service::host_side { sizeX, sizeY, get_handle({ 0, 0 }) };
  }
  void add_storage(hw::position pos, void* storage) { get_mem(pos) = storage; }

  /// this will retrun a handle to the synchronization barrier between the
  /// device and the host.
  soft_barrier::host_side get_barrier(int x, int y) {
    return { get_handle({ x, y }),
             (uint32_t)(hw::offset_table::get_service_record_begin_offset() +
                        offsetof(service_device_side, barrier)) };
  }

  ~device_impl() { TRISYCL_XAIE(xaie::XAie_Finish(&impl.aie_inst)); }
  template <typename ServiceTy> void wait_all(ServiceTy&& service_data) {
    trisycl::detail::no_log_in_this_scope nls;
    int addr = hw::offset_table::get_service_record_begin_offset();
    /// This count the number of kernel that indicated they finished
    /// executing. any kernel can signal it finished executing just once
    /// because it stop executing or get stuck in an infinite loop after that.
    /// so it is not needed to keep track of which kernel stoped executing
    /// just how many.
    int done_counter = 0;
    xaie::handle h = get_handle({ 0, 0 });
    do {
      for (int x = 0; x < sizeX; x++)
        for (int y = 0; y < sizeY; y++) {
          /// We process at least one request per device.
          bool chain = false;
          /// while the device asks to chain requests.
          do {
            /// If try_arrive returns true the device has written data and is
            /// waitng on the host to act on it
            if (!get_barrier(x, y).try_arrive())
              continue;
            service_device_side ds = h.on(x, y).load<service_device_side>(addr);

            /// read if the device requested to chain the is request.
            chain = ds.chained_request;

            /// done is always at index 0 and is handled inline
            if (ds.index == 0)
              done_counter++;
            ServiceTy::service_list_t::for_any(ds.index, [&]<typename T> {
              using info = service_info<T>;
              auto data = h.on(x, y).load<typename info::data_t>(ds.data);
              if constexpr (!info::is_void_ret) {
                auto ret =
                    std::get<T>(service_data.data)
                        .act_on_data(x, y, device_mem_handle(h.on(x, y)),
                                     data);
                h.on(x, y).store(ds.ret, ret);
              } else {
                std::get<T>(service_data.data)
                    .act_on_data(x, y, h.on(x, y), data);
              }
            });
            get_barrier(x, y).wait();
          } while (chain);
        }
      /// Wait until all kernels have finished.
    } while (done_counter < sizeX * sizeY);
  }
};
} // namespace aie::detail

#endif
